import os
import time

import numpy as np
import pytest

import mesytec_mcpd as mcpd
from fake_mdll_data import (
    MCPD_DATA_BUFFER_TYPE,
    PACKET_SIZE,
    MdllDataSender,
    build_packet,
    mdll_neutron,
    trigger_event,
)


def wait_for_packets(daq, n, timeout=2.0):
    deadline = time.monotonic() + timeout
    while daq.get_counters().packets < n:
        if time.monotonic() > deadline:
            raise TimeoutError(f"got {daq.get_counters().packets} of {n} packets")
        time.sleep(0.005)


@pytest.fixture
def daq():
    d = mcpd.MdllDaq(listen_port=0)
    yield d
    d.stop()


def test_histograms_and_device_keys(daq):
    daq.start_readout()
    assert daq.local_port != 0

    with (
        MdllDataSender(daq.local_port, "127.0.0.2") as a,
        MdllDataSender(daq.local_port, "127.0.0.3") as b,
    ):
        a.send([mdll_neutron(10, 100, 200), mdll_neutron(10, 100, 200), trigger_event()])
        b.send([mdll_neutron(255, 1023, 1023)], device_id=0)
        b.send([mdll_neutron(0, 0, 0)], device_id=1)
        wait_for_packets(daq, 3)

        stats = daq.get_device_stats()
        assert set(stats) == {(a.src_addr, 0), (b.src_addr, 0), (b.src_addr, 1)}

        sa = stats[(a.src_addr, 0)]
        assert (sa.packets, sa.events, sa.neutron_events, sa.trigger_events) == (1, 3, 2, 1)

        h = daq.get_histograms(a.src_addr, 0)
        assert h["amplitude"].shape == (256,)
        assert h["x"].shape == (1024,)
        assert h["xy"].shape == (1024, 1024)
        assert h["amplitude"][10] == 2 and h["amplitude"].sum() == 2
        assert h["x"][100] == 2 and h["y"][200] == 2
        assert h["xy"][200, 100] == 2 and h["xy"].sum() == 2

        hb = daq.get_histograms(b.src_addr, 0)
        assert hb["amplitude"][255] == 1
        assert hb["xy"][1023, 1023] == 1

        assert daq.get_histograms(b.src_addr, 7) is None
        assert mcpd.format_ipv4(a.src_addr) == "127.0.0.2"


def test_header_fields(daq):
    daq.start_readout()
    with MdllDataSender(daq.local_port) as s:
        s.send(
            [], device_id=3, run_id=42, device_status=0x5, header_timestamp=0x123456789A,
            params=(1, 2, 3, 0xFFFFFFFFFFFF), buffer_number=17,
        )
        wait_for_packets(daq, 1)
        st = daq.get_device_stats()[(s.src_addr, 3)]
        assert st.last_run_id == 42
        assert st.last_device_status == 0x5
        assert st.last_header_timestamp == 0x123456789A
        assert st.last_params == (1, 2, 3, 0xFFFFFFFFFFFF)
        assert st.last_buffer_number == 17


@pytest.mark.parametrize(
    "buffer_numbers,lost",
    [
        ([0, 1, 2, 3], 0),
        ([0, 1, 3], 1),
        ([10, 20], 9),
        ([65534, 65535, 0, 1], 0),
        ([65535, 1], 1),
    ],
)
def test_packet_loss(daq, buffer_numbers, lost):
    daq.start_readout()
    with MdllDataSender(daq.local_port) as s:
        for bn in buffer_numbers:
            s.send([], buffer_number=bn)
        wait_for_packets(daq, len(buffer_numbers))
        st = daq.get_device_stats()[(s.src_addr, 0)]
        assert (st.packets_lost, st.buffer_number_jumps) == (lost, 0)


@pytest.mark.parametrize("buffer_numbers", [[5, 5], [10, 3], [0, 40000]])
def test_buffer_number_jumps_not_counted_as_loss(daq, buffer_numbers):
    daq.start_readout()
    with MdllDataSender(daq.local_port) as s:
        for bn in buffer_numbers:
            s.send([], buffer_number=bn)
        wait_for_packets(daq, len(buffer_numbers))
        st = daq.get_device_stats()[(s.src_addr, 0)]
        assert (st.packets_lost, st.buffer_number_jumps) == (0, 1)


def test_loss_tracked_per_device(daq):
    daq.start_readout()
    with MdllDataSender(daq.local_port) as s:
        for bn in range(5):
            s.send([], device_id=0, buffer_number=bn)
            s.send([], device_id=1, buffer_number=100 + bn)
        wait_for_packets(daq, 10)
        stats = daq.get_device_stats()
        assert stats[(s.src_addr, 0)].packets_lost == 0
        assert stats[(s.src_addr, 1)].packets_lost == 0


def test_reset_stats_resets_loss_baseline(daq):
    daq.start_readout()
    with MdllDataSender(daq.local_port) as s:
        s.send([mdll_neutron(1, 2, 3)], buffer_number=100)
        wait_for_packets(daq, 1)
        daq.reset_stats()
        s.send([], buffer_number=0)
        wait_for_packets(daq, 1)
        st = daq.get_device_stats()[(s.src_addr, 0)]
        assert (st.packets, st.packets_lost) == (1, 0)
        assert daq.get_histograms(s.src_addr, 0)["amplitude"][1] == 1

        daq.clear_histograms()
        assert daq.get_histograms(s.src_addr, 0)["amplitude"].sum() == 0

        daq.clear_devices()
        assert daq.get_device_stats() == {}


def test_invalid_and_non_mdll_packets(daq):
    daq.start_readout()
    with MdllDataSender(daq.local_port) as s:
        good = build_packet([mdll_neutron(1, 1, 1)] * 4)
        s.send_raw(good[:-2])  # bufferLength larger than received size
        s.send_raw(b"\x00" * 10)  # shorter than the header
        s.send([mdll_neutron(1, 1, 1)], buffer_type=MCPD_DATA_BUFFER_TYPE)
        wait_for_packets(daq, 3)
        c = daq.get_counters()
        assert c.invalid_packets == 2
        assert c.non_mdll_packets == 1
        assert daq.get_device_stats() == {}


def test_listfile_write_and_replay(daq, tmp_path):
    listfile = str(tmp_path / "run.mcpdlst")
    daq.start_readout(listfile)

    with MdllDataSender(daq.local_port) as s:
        for i in range(10):
            s.send([mdll_neutron(i, i * 10, i * 20)] * 5, device_id=i % 2, buffer_number=i // 2)
        wait_for_packets(daq, 10)
        live = {k[1]: daq.get_histograms(*k) for k in daq.get_device_stats()}

    daq.stop()
    assert os.path.getsize(listfile) == 10 * PACKET_SIZE

    with pytest.raises(RuntimeError, match="already exists"):
        daq.start_readout(listfile)

    replay = mcpd.MdllDaq()
    replay.start_replay(listfile)
    deadline = time.monotonic() + 2
    while replay.is_running() and time.monotonic() < deadline:
        time.sleep(0.005)
    replay.stop()

    stats = replay.get_device_stats()
    assert set(stats) == {(0, 0), (0, 1)}
    assert all(st.packets_lost == 0 for st in stats.values())
    for dev_id, h in live.items():
        rh = replay.get_histograms(0, dev_id)
        for name in ("amplitude", "x", "y", "xy"):
            np.testing.assert_array_equal(h[name], rh[name])


def test_replay_missing_file_raises():
    with pytest.raises(RuntimeError, match="Failed to open"):
        mcpd.MdllDaq().start_replay("/nonexistent/file.mcpdlst")


def test_restart(daq):
    for _ in range(3):
        daq.start_readout()
        assert daq.is_running()
        daq.stop()
        assert not daq.is_running()
        assert daq.local_port == 0
