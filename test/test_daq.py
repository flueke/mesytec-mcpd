import os
import time

import numpy as np
import pytest

import mesytec_mcpd as mcpd
from fake_data import (
    MCPD_DATA_BUFFER_TYPE,
    PACKET_SIZE,
    DataSender,
    build_packet,
    mdll_neutron,
    mpsd_neutron,
    trigger_event,
)


def wait_for_packets(daq, n, timeout=2.0):
    deadline = time.monotonic() + timeout
    while daq.get_counters().packets < n:
        if time.monotonic() > deadline:
            raise TimeoutError(f"got {daq.get_counters().packets} of {n} packets")
        time.sleep(0.005)


def wait_for_replay(daq, timeout=2.0):
    deadline = time.monotonic() + timeout
    while daq.is_running():
        if time.monotonic() > deadline:
            raise TimeoutError("replay did not finish")
        time.sleep(0.005)
    daq.stop()


@pytest.fixture
def daq():
    d = mcpd.Daq(listen_port=0)
    yield d
    d.stop()


@pytest.mark.parametrize(
    "last,current,gap",
    [(0, 1, 0), (0, 2, 1), (65535, 0, 0), (65534, 1, 2), (5, 5, None), (10, 3, None), (0, 0x8000, None), (0, 0x8000 - 1, 0x7FFE)],
)
def test_buffer_number_gap(last, current, gap):
    assert mcpd.buffer_number_gap(last, current) == gap


def test_mdll_histograms_and_sources(daq):
    daq.start_readout()
    assert daq.local_port != 0

    with DataSender(daq.local_port, "127.0.0.2") as a, DataSender(daq.local_port, "127.0.0.3") as b:
        a.send([mdll_neutron(10, 100, 200), mdll_neutron(10, 100, 200), trigger_event()])
        b.send([mdll_neutron(255, 1023, 1023)], device_id=0)
        b.send([mdll_neutron(0, 0, 0)], device_id=1)
        wait_for_packets(daq, 3)

        stats = daq.get_source_stats()
        assert set(stats) == {(a.src_addr, 0), (b.src_addr, 0), (b.src_addr, 1)}

        sa = stats[(a.src_addr, 0)]
        assert sa.buffer_type == mcpd.constants.buffer_types.MdllDataBufferType
        assert (sa.packets, sa.events, sa.neutron_events, sa.trigger_events) == (1, 3, 2, 1)

        h = daq.get_mdll_histograms(a.src_addr, 0)
        assert h["amplitude"].shape == (256,)
        assert h["x"].shape == (1024,)
        assert h["xy"].shape == (1024, 1024)
        assert h["amplitude"][10] == 2 and h["amplitude"].sum() == 2
        assert h["x"][100] == 2 and h["y"][200] == 2
        assert h["xy"][200, 100] == 2 and h["xy"].sum() == 2

        hb = daq.get_mdll_histograms(b.src_addr, 0)
        assert hb["amplitude"][255] == 1
        assert hb["xy"][1023, 1023] == 1

        assert daq.get_mdll_histograms(b.src_addr, 7) is None
        assert daq.get_mcpd_histograms(a.src_addr, 0) is None
        assert mcpd.format_ipv4(a.src_addr) == "127.0.0.2"


def test_mcpd_histograms(daq):
    daq.start_readout()
    with DataSender(daq.local_port) as s:
        s.send(
            [mpsd_neutron(3, 7, 500, 1000), mpsd_neutron(3, 7, 500, 1000), mpsd_neutron(7, 31, 1023, 0), trigger_event()],
            device_id=4,
            buffer_type=MCPD_DATA_BUFFER_TYPE,
        )
        wait_for_packets(daq, 1)

        st = daq.get_source_stats()[(s.src_addr, 4)]
        assert st.buffer_type == MCPD_DATA_BUFFER_TYPE
        assert (st.neutron_events, st.trigger_events) == (3, 1)

        h = daq.get_mcpd_histograms(s.src_addr, 4)
        assert h["amplitude"].shape == (8, 32, 1024)
        assert h["position"].shape == (8, 32, 1024)
        assert h["amplitude"][3, 7, 500] == 2
        assert h["position"][3, 7, 1000] == 2
        assert h["amplitude"][7, 31, 1023] == 1
        assert h["position"][7, 31, 0] == 1
        assert h["amplitude"].sum() == 3 and h["position"].sum() == 3
        assert daq.get_mdll_histograms(s.src_addr, 4) is None


def test_header_fields(daq):
    daq.start_readout()
    with DataSender(daq.local_port) as s:
        s.send(
            [], device_id=3, run_id=42, device_status=0x5, header_timestamp=0x123456789A,
            params=(1, 2, 3, 0xFFFFFFFFFFFF), buffer_number=17,
        )
        wait_for_packets(daq, 1)
        st = daq.get_source_stats()[(s.src_addr, 3)]
        assert st.last_run_id == 42
        assert st.last_device_status == 0x5
        assert st.last_header_timestamp == 0x123456789A
        assert st.last_params == (1, 2, 3, 0xFFFFFFFFFFFF)
        assert st.last_buffer_number == 17


@pytest.mark.parametrize(
    "buffer_numbers,lost,jumps",
    [
        ([0, 1, 2, 3], 0, 0),
        ([0, 1, 3], 1, 0),
        ([10, 20], 9, 0),
        ([65534, 65535, 0, 1], 0, 0),
        ([65535, 1], 1, 0),
        ([5, 5], 0, 1),
        ([10, 3], 0, 1),
        ([0, 40000], 0, 1),
    ],
)
def test_packet_loss(daq, buffer_numbers, lost, jumps):
    daq.start_readout()
    with DataSender(daq.local_port) as s:
        for bn in buffer_numbers:
            s.send([], buffer_number=bn)
        wait_for_packets(daq, len(buffer_numbers))
        st = daq.get_source_stats()[(s.src_addr, 0)]
        assert (st.packets_lost, st.buffer_number_jumps) == (lost, jumps)


def test_loss_tracked_per_source(daq):
    daq.start_readout()
    with DataSender(daq.local_port) as s:
        for bn in range(5):
            s.send([], device_id=0, buffer_number=bn)
            s.send([], device_id=1, buffer_number=100 + bn)
        wait_for_packets(daq, 10)
        stats = daq.get_source_stats()
        assert stats[(s.src_addr, 0)].packets_lost == 0
        assert stats[(s.src_addr, 1)].packets_lost == 0


def test_reset_and_clear(daq):
    daq.start_readout()
    with DataSender(daq.local_port) as s:
        s.send([mdll_neutron(1, 2, 3)], buffer_number=100)
        wait_for_packets(daq, 1)
        daq.reset_stats()
        s.send([], buffer_number=0)
        wait_for_packets(daq, 2)
        st = daq.get_source_stats()[(s.src_addr, 0)]
        assert (st.packets, st.packets_lost, st.buffer_number_jumps) == (1, 0, 0)
        assert daq.get_mdll_histograms(s.src_addr, 0)["amplitude"][1] == 1

        daq.clear_histograms()
        assert daq.get_mdll_histograms(s.src_addr, 0)["amplitude"].sum() == 0

        daq.clear_sources()
        assert daq.get_source_stats() == {}
        assert daq.get_mdll_histograms(s.src_addr, 0) is None


def test_invalid_packets(daq):
    daq.start_readout()
    with DataSender(daq.local_port) as s:
        good = build_packet([mdll_neutron(1, 1, 1)] * 4)
        s.send_raw(good[:-2])  # bufferLength larger than received size
        s.send_raw(b"\x00" * 10)  # shorter than the header
        wait_for_packets(daq, 2)
        assert daq.get_counters().invalid_packets == 2
        assert daq.get_source_stats() == {}


def test_non_data_buffer_type_counts_no_events(daq):
    daq.start_readout()
    with DataSender(daq.local_port) as s:
        s.send([mdll_neutron(1, 1, 1)], buffer_type=0x8000)
        wait_for_packets(daq, 1)
        st = daq.get_source_stats()[(s.src_addr, 0)]
        assert (st.buffer_type, st.packets, st.events) == (0x8000, 1, 0)


def test_listfile_write_and_replay(daq, tmp_path):
    listfile = str(tmp_path / "run.mcpdlst")
    daq.start_readout(listfile)

    with DataSender(daq.local_port) as s:
        for i in range(10):
            s.send([mdll_neutron(i, i * 10, i * 20)] * 5, device_id=i % 2, buffer_number=i // 2)
        s.send([mpsd_neutron(1, 2, 3, 4)] * 7, device_id=5, buffer_type=MCPD_DATA_BUFFER_TYPE)
        wait_for_packets(daq, 11)
        live_mdll = {k[1]: daq.get_mdll_histograms(*k) for k in [(s.src_addr, 0), (s.src_addr, 1)]}
        live_mcpd = daq.get_mcpd_histograms(s.src_addr, 5)

    daq.stop()
    assert daq.local_port == 0
    assert os.path.getsize(listfile) == 11 * PACKET_SIZE
    assert daq.get_counters().listfile_bytes == 11 * PACKET_SIZE

    with pytest.raises(RuntimeError, match="already exists"):
        daq.start_readout(listfile)
    assert not daq.is_running()

    replay = mcpd.Daq()
    replay.start_replay(listfile)
    wait_for_replay(replay)

    stats = replay.get_source_stats()
    assert set(stats) == {(0, 0), (0, 1), (0, 5)}
    assert all(st.packets_lost == 0 for st in stats.values())
    for dev_id, h in live_mdll.items():
        rh = replay.get_mdll_histograms(0, dev_id)
        for name in ("amplitude", "x", "y", "xy"):
            np.testing.assert_array_equal(h[name], rh[name])
    rh = replay.get_mcpd_histograms(0, 5)
    np.testing.assert_array_equal(live_mcpd["amplitude"], rh["amplitude"])
    np.testing.assert_array_equal(live_mcpd["position"], rh["position"])


def test_histograms_survive_restart(daq):
    daq.start_readout()
    with DataSender(daq.local_port) as s:
        s.send([mdll_neutron(1, 2, 3)])
        wait_for_packets(daq, 1)
    daq.stop()
    daq.start_readout()
    with DataSender(daq.local_port) as s:
        s.send([mdll_neutron(1, 2, 3)])
        wait_for_packets(daq, 1)
        assert daq.get_mdll_histograms(s.src_addr, 0)["amplitude"][1] == 2


def test_replay_missing_file_raises():
    with pytest.raises(RuntimeError, match="Failed to open"):
        mcpd.Daq().start_replay("/nonexistent/file.mcpdlst")


def test_start_twice_raises(daq):
    daq.start_readout()
    with pytest.raises(RuntimeError, match="already running"):
        daq.start_readout()


def test_restart(daq):
    for _ in range(3):
        daq.start_readout()
        assert daq.is_running()
        daq.stop()
        assert not daq.is_running()
        assert daq.local_port == 0


def _free_udp_port():
    import socket

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("0.0.0.0", 0))
        return s.getsockname()[1]


def test_restart_on_fixed_port():
    daq = mcpd.Daq(listen_port=_free_udp_port())
    for _ in range(3):
        daq.start_readout()
        daq.stop()
