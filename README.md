# mesytec-mcpd

Driver library and tools for the
[mesytec PSD+ system](https://mesytec.com/products/neutron-scattering.html).

Components:

- **libmesytec-mcpd**: C++17 library implementing the MCPD command protocol, data
  packet decoding, readout/replay, listfile writing, per source statistics and
  histogramming.
- **mcpd-cli**: C++ command line tool for device setup, readout and replay.
  Optionally writes ROOT histograms and runs user python scripts.
- **mesytec_mcpd**: python bindings for the library (`pip install mesytec-mcpd`).
- **mcpd-cli-py**: python port of mcpd-cli, installed with the python package.
- **mesytec-mpsd-gui**: DAQ GUI with run control, stats, histograms and an embedded
  python console (`pip install 'mesytec-mcpd[gui]'`). Supports MCPD-8 with MPSD-8+ and MDLL.

Supported hardware:

| Device     | Notes |
|------------|-------|
| MCPD-8_v1  | Older variant, Ethernet connector on the front panel. Network settings stored in flash. |
| MCPD-8_v2  | Newer variant (2022+), Ethernet connector on the back panel. DHCP or static ARP. |
| MPSD-8+    | Connected to the MCPD-8 busses. |
| MSTD-16    | Connected to the MCPD-8 busses. |
| MDLL       | `mdll_set_spectrum()` uses 16 bit shift/offset values since v0.9, requires FW0106. |

Source code: https://github.com/mesytec/mesytec-mcpd<br>
Changelog: [CHANGELOG.md](https://github.com/mesytec/mesytec-mcpd/blob/main/CHANGELOG.md)<br>
Development notes: [DEVELOPMENT.md](https://github.com/mesytec/mesytec-mcpd/blob/main/DEVELOPMENT.md)

- [Quick start (python)](#quick-start-python)
- [Device network setup](#device-network-setup)
  - [MCPD-8\_v1](#mcpd-8_v1)
  - [MCPD-8\_v2](#mcpd-8_v2)
  - [Host setup](#host-setup)
- [Command line tools](#command-line-tools)
  - [Minimal DAQ setup: one MCPD-8 with two MPSD-8+](#minimal-daq-setup-one-mcpd-8-with-two-mpsd-8)
  - [Listfile replay](#listfile-replay)
  - [Python scripts in mcpd-cli](#python-scripts-in-mcpd-cli)
  - [ROOT histograms](#root-histograms)
  - [mcpd-cli-py](#mcpd-cli-py)
- [Python API](#python-api)
- [GUI](#gui)
- [C++ library](#c-library)
  - [Building from source](#building-from-source)
  - [Using the library from CMake](#using-the-library-from-cmake)
  - [Library overview](#library-overview)
- [Listfiles and additional tools](#listfiles-and-additional-tools)
- [License](#license)

# Quick start (python)

Requires python >= 3.12. Binary wheels are available for Linux x86_64, Windows
x64 and macOS (x86_64 and arm64).

```shell
pip install mesytec-mcpd            # library, mcpd-cli-py
pip install 'mesytec-mcpd[gui]'     # additionally installs the GUI dependencies
```

```python
import mesytec_mcpd as mcpd

conn = mcpd.McpdConnection("192.168.168.121", mcpd_id=0)
print(conn.get_version())
```

Start the GUI with `mesytec-mpsd-gui`, the python CLI with `mcpd-cli-py --help`.

# Device network setup

The MCPD-8 is controlled via UDP. Commands are sent to port 54321 on the MCPD,
readout data is sent by the MCPD to the *data destination* address and port
(default port 54321), configured via `setup` and `set_data_port`.

The examples below use `mcpd-cli`. `mcpd-cli-py` can be used instead, see
[mcpd-cli-py](#mcpd-cli-py) for the command name mapping.

## MCPD-8_v1

Each MCPD-8_v1 in a setup needs a unique IP-address and ID. The default
IP-address is `192.168.168.121`, the default ID is `0`. These defaults can be
restored by pressing the reset button on the CPU board inside the MCPD NIM
case. Settings are permanently stored in the flash memory of the module.

The steps below assume that your local network is `10.11.12.0/255.255.255.0` and
your machines IP-address in the local network is `10.11.12.1`.

1. Set your PCs network card to `192.168.168.1/255.255.255.0`.

2. Connect the MCPD-8 directly to your PCs network card.

3. Verify connectivity:

   - `ping 192.168.168.121` should see a response from the MCPD-8.
   - `mcpd-cli version` should connect and read the CPU and FPGA firmware versions.

4. Set a new IP-address and ID for the MCPD-8:

   ```shell
   mcpd-cli setup 10.11.12.100 0
   ```

   This sets the address to `10.11.12.100` and the MCPD-ID to `0`. You will not
   get a response from the MCPD as it immediately uses its new IP-address which
   is on a different subnet.

5. Repeat the above for any additional MCPD-8 modules (connect them one by one).
   Choose unique IP-addresses and IDs for each module, e.g.:

   ```shell
   mcpd-cli setup 10.11.12.101 1
   mcpd-cli setup 10.11.12.102 2
   ```

6. Change your network card back to your local network: `10.11.12.1/255.255.255.0`.
   The MCPD should now be reachable using the address set in step 4:

   ```shell
   ping 10.11.12.100
   ```

7. The MCPD still has the previous data destination *MAC-address* stored. Update
   it by running `setup` once more from the local network. This leaves the MCPD
   address and ID unchanged and sets the data destination to the computer
   running the command:

   ```shell
   mcpd-cli --address=10.11.12.100 setup 10.11.12.100 0
   ```

8. Repeat step 7 for any additional MCPD-8 modules using their respective
   IP-addresses and IDs.

Instead of changing your PCs network address to reach the modules you can add
static ARP entries (see [below](#manual-arp-entry)) using the MAC address printed
on a sticker on the CPU board inside the MCPD-8 NIM case.

## MCPD-8_v2

The MCPD-8_v2 has no flash to permanently store network settings. DHCP is used
to configure the IP-address and hostname of the module. If DHCP is not
available a static ARP entry can be used instead.

With the v2 only the data destination port can be changed via `setup`. The ID
is mirrored from the command packets, `setid` is not needed.

### DHCP

After powerup the MCPD-8_v2 requests an IPv4-address and the hostname
`mcpd-NNNN` via DHCP, where `NNNN` is the serial number displayed near the
Ethernet port of the module.

After the DHCP phase the module should be reachable via its hostname:

```shell
mcpd-cli --address mcpd-0012 version
```

`nslookup` and `ping` can also be used to verify that DHCP is working.

### Manual ARP entry

If DHCP cannot be used, associate the MAC-address of the MCPD-8 with an
IP-address in the operating systems ARP table. The MAC address is shown near the
Ethernet port and has the form `04:85:46:d4:NN:NN` where `NNNN` is the last part
of the serial number.

The examples below associate `192.168.100.42` with the MCPD. The IP-address has
to be part of your local network, otherwise the operating system does not know
how to reach the module. Root/admin permissions are required.

* Linux

  ```shell
  arp -s 192.168.100.42 04:85:46:d4:00:12
  ```

  To make the entry permanent (at least on debian and ubuntu) add a line like
  this to `/etc/ethers`:

  ```
  04:85:46:d4:00:12 192.168.100.42
  ```

* Windows: open a `cmd.exe` prompt with **Administrator** permissions:

  ```shell
  arp -s 192.168.100.42 04-85-46-d4-00-12
  ```

Verify connectivity with `mcpd-cli --address 192.168.100.42 version`.

## Host setup

- **Firewall**: incoming UDP packets on the data port (default 54321) must be
  allowed.

- **Socket receive buffer (Linux)**: the readout requests a 16 MiB socket
  receive buffer. Linux silently limits this to `net.core.rmem_max` (usually
  around 200 KiB), which can lead to packet loss at high data rates. Raise the
  limit:

  ```shell
  sysctl -w net.core.rmem_max=16777216
  ```

  To make it permanent add `net.core.rmem_max=16777216` to a file in
  `/etc/sysctl.d/`. The size actually granted is available from python via
  `Daq.socket_receive_buffer_size`.

# Command line tools

`mcpd-cli` (C++) and `mcpd-cli-py` (python) provide the same functionality with
slightly different command syntax. Both read the following environment
variables if the corresponding option is not given:

* `MCPD_ADDRESS`: ip-address/hostname of the MCPD (`--address`, default `192.168.168.121`)
* `MCPD_ID`: MCPD id (`--id`, default `0`)

Use `mcpd-cli --help` for a list of all commands and options.

## Minimal DAQ setup: one MCPD-8 with two MPSD-8+

### Initialization

```shell
export MCPD_ADDRESS=10.11.12.100 MCPD_ID=0

# Set the runId for the next DAQ run
mcpd-cli runid 1

# Set thresholds for MPSDs on bus 0 and 1 to 0
mcpd-cli mpsd_set_threshold 0 0
mcpd-cli mpsd_set_threshold 1 0

# Enable pulser: mpsd=0, channel=0, pos=2 (center), amplitude=128, state=on
mcpd-cli mpsd_set_pulser 0 0 2 128 on

# Enable pulser: mpsd=1, channel=0, pos=1 (right), amplitude=64, state=on
mcpd-cli mpsd_set_pulser 1 0 1 64 on
```

### Readout

```shell
mcpd-cli readout --duration=60 --listfile=mcpd-run1.mcpdlst
```

This sends the DAQ start command and receives data for 60 seconds or until
canceled via `ctrl-c`. The DAQ is not stopped at the end. Use `--no-start-daq`
to only receive data and control the DAQ separately, e.g. from a second
terminal:

```shell
mcpd-cli daq start
mcpd-cli daq stop
```

`mcpd-cli readout` listens on the data port (`--dataport`, default 54321) and
accepts packets from all sources. Data from multiple MCPD-8 modules can be
recorded by a single readout process as long as the modules have unique IDs.
Per source statistics are reported periodically (`--report-interval`).

Either `--listfile` or `--no-listfile` has to be given. Use
`--overwrite-listfile` to replace an existing file.

## Listfile replay

```shell
mcpd-cli replay --listfile=mcpd-run1.mcpdlst
```

`--print-packet-summary`, `--print-event-data` and `--print-raw-packet-data`
can be used with both `readout` and `replay` to inspect the data.

## Python scripts in mcpd-cli

If built with python support (the default) `mcpd-cli readout` and `mcpd-cli
replay` can run a user python script for each received packet and/or event:

```shell
mcpd-cli replay --listfile=mcpd-run1.mcpdlst --python-script=myscript.py -- arg1 arg2
```

Arguments after `--` are passed to the scripts `start()` function. See
[extras/mcpd-cli/mcpd-cli-python-example.py](https://github.com/mesytec/mesytec-mcpd/blob/main/extras/mcpd-cli/mcpd-cli-python-example.py)
for the script interface.

## ROOT histograms

If built with ROOT support (see [Building from source](#building-from-source))
`readout` and `replay` can fill amplitude, position and time histograms and
write them to a ROOT file:

```shell
mcpd-cli readout --duration=60 --listfile=mcpd-run1.mcpdlst --root-histo-file=mcpd-run1-histos.root
mcpd-cli replay --listfile=mcpd-run1.mcpdlst --root-histo-file=mcpd-replay1-histos.root
```

The file is flushed periodically (`--root-flush-interval`).
`--root-enable-graphs` / `--root-enable-mdll-graphs` additionally record values
over time. This uses a lot of memory.

## mcpd-cli-py

`mcpd-cli-py` groups commands by device and uses dashes instead of underscores.
Enum arguments use the member names of the python bindings, e.g. `Center`,
`On`, `Master`.

| mcpd-cli                          | mcpd-cli-py                              |
|-----------------------------------|------------------------------------------|
| `version`                         | `mcpd version`                           |
| `setup 10.11.12.100 0`            | `mcpd setup 10.11.12.100 0`              |
| `runid 1`                         | `mcpd runid 1`                           |
| `get_parameters`                  | `mcpd get-parameters`                    |
| `mpsd_set_threshold 0 0`          | `mpsd set-threshold 0 0`                 |
| `mpsd_set_pulser 0 0 2 128 on`    | `mpsd set-pulser 0 0 Center 128 On`      |
| `mstd_set_gain 0 0 10`            | `mstd set-gain 0 0 10`                   |
| `mdll_set_thresholds 10 10 20`    | `mdll set-thresholds 10 10 20`           |
| `daq start`                       | `daq start`                              |
| `readout ...` / `replay ...`      | `readout ...` / `replay ...`             |

Not available in mcpd-cli-py: `custom`, `--python-script`, ROOT output and the
`--print-*` options.

# Python API

The `mesytec_mcpd` module exposes the library via pybind11. Type stubs are
included (Linux and macOS wheels), use `help(mesytec_mcpd)` or your IDE for the
full API.

Main classes:

- `McpdConnection`: command socket to one MCPD. One method per device command,
  e.g. `get_version()`, `start_daq()`, `mpsd_set_gain()`, `mdll_set_thresholds()`.
  Errors are raised as `McpdError`.
- `Daq`: readout from the network or replay from a listfile. Writes listfiles,
  collects per source statistics and fills MDLL and MCPD histograms. Runs in a
  C++ thread, no python code is executed in the data path.
- `DataPacket`, `DecodedEvent`: packet and event decoding.
- `Readout`, `Replay`: lower level workers delivering packets to python via a
  queue, for custom per packet processing.

Readout example:

```python
import time
import mesytec_mcpd as mcpd

conn = mcpd.McpdConnection("192.168.168.121", mcpd_id=0)

daq = mcpd.Daq()                        # listens on UDP port 54321
daq.start_readout("run1.mcpdlst")
conn.start_daq()
time.sleep(10)
conn.stop_daq()
daq.stop()

for (src_addr, device_id), stats in daq.get_source_stats().items():
    print(mcpd.format_ipv4(src_addr), device_id, stats.packets, stats.events, stats.packets_lost)
    histos = daq.get_mdll_histograms(src_addr, device_id)
    if histos is not None:
        print(histos["xy"].shape)       # numpy arrays: amplitude, x, y, xy[y, x]
```

Replay works the same way using `daq.start_replay("run1.mcpdlst")`, poll
`daq.is_running()` to detect the end of the file. MCPD/MPSD histograms are
available via `daq.get_mcpd_histograms()` as `amplitude[mpsd, channel, bin]` and
`position[mpsd, channel, bin]`.

Library log messages are forwarded to the python `logging` module. Use
`mcpd.set_log_level("debug")` to change the library log level.

# GUI

```shell
pip install 'mesytec-mcpd[gui]'
mesytec-mpsd-gui [setup.json]
```

The GUI supports MCPD-8 (v1 and v2) with up to 8 MPSD-8+ modules and MDLL setups:

- run control, listfile writing and replay
- per source stats table
- MPSD histogram views (position, amplitude): channels of a bus stacked or
  overlaid, all channels as a 2D image
- MDLL histogram views (xy, x, y, amplitude)
- device parameter tree with a text filter, per bus MPSD settings and commands,
  bus scanning
- embedded python console

Setups are stored as json files. Without an argument the last used setup is
loaded, otherwise the default setup file in the users config directory
(`mesytec-mcpd/mpsd_gui_setup.json`). The window layout is restored on start.

The console namespace contains `mcpd` (the module), `daq` (the `Daq` instance
used by the GUI), `setup` and `mainwin`. Readouts started or stopped from the
console, e.g. `daq.stop()`, are reflected in the GUI.

# C++ library

## Building from source

Requirements: CMake >= 3.18 and a C++17 compiler (gcc, clang, msvc). With the
default options python development headers are required as well. spdlog, lyra
and pybind11 are included in the source tree.

```shell
git clone https://github.com/mesytec/mesytec-mcpd
cmake -S mesytec-mcpd -B mesytec-mcpd/build -DCMAKE_BUILD_TYPE=Release
cmake --build mesytec-mcpd/build
cmake --install mesytec-mcpd/build
```

Add `-DCMAKE_INSTALL_PREFIX=$HOME/local/mesytec-mcpd` to the first cmake
command to change the installation path.

CMake options:

| Option                 | Default          | Description |
|------------------------|------------------|-------------|
| `MCPD_ENABLE_PYTHON`   | `ON`             | Build the python module and embed python in mcpd-cli (`--python-script`). |
| `MCPD_BUILD_MCPD_CLI`  | `ON`             | Build mcpd-cli and the tools under `extras/`. |
| `MCPD_CLI_WITH_ROOT`   | `OFF`            | ROOT histogram output in mcpd-cli. ROOT is searched via `$ROOTSYS`. |
| `MCPD_BUILD_TESTS`     | `ON` (top-level) | Build the unit tests. Downloads googletest, set to `OFF` for offline builds. |

Installed are the shared library, headers, CMake config files, `mcpd-cli` and
the python module (under `lib/mesytec_mcpd`). Run the unit tests with `ctest`
from the build directory.

## Using the library from CMake

A minimal CMake example project is in
[extras/cmake-example](https://github.com/mesytec/mesytec-mcpd/tree/main/extras/cmake-example).
If the library is installed to a non-standard location tell CMake about it:

```shell
export CMAKE_PREFIX_PATH=$HOME/local/mesytec-mcpd
```

CMakeLists.txt:

```cmake
cmake_minimum_required(VERSION 3.12)
project(mesytec-mcpd-cmake-example)

find_package(mesytec-mcpd REQUIRED)

add_executable(mcpd-example mcpd-example.cc)
target_link_libraries(mcpd-example PRIVATE mesytec-mcpd::mesytec-mcpd)
```

mcpd-example.cc, connects to a MCPD and reads the CPU and FPGA version information:

```cpp
#include <iostream>
#include <mesytec-mcpd/mesytec-mcpd.h>

using namespace mesytec::mcpd;
using std::cout;
using std::cerr;
using std::endl;

int main(int argc, char *argv[])
{
    std::error_code ec = {};

    int mcpdCommandSocket = connect_udp_socket("192.168.168.121", McpdDefaultPort, &ec);

    if (ec)
    {
        cerr << "Error connecting to mcpd: " << ec.message() << std::endl;
        return 1;
    }

    unsigned mcpdId = 0u;

    McpdVersionInfo vi = {};

    ec = mcpd_get_version(mcpdCommandSocket, mcpdId, vi);

    if (ec)
    {
        cerr << "Error reading MCPD version info: " << ec.message() << std::endl;
        return 1;
    }

    cout << "MCPD version info: CPU=" << vi.cpu[0] << "." << vi.cpu[1]
        << ", FPGA=" << vi.fpga[0] << "." << vi.fpga[1] << endl;

    return 0;
}
```

Without CMake pass the flags manually. The library has to be found at runtime,
e.g. via `LD_LIBRARY_PATH` or an rpath:

```shell
PREFIX=$HOME/local/mesytec-mcpd
g++ -std=c++17 -I$PREFIX/include mytool.cc -o mytool -L$PREFIX/lib -lmesytec-mcpd -Wl,-rpath,$PREFIX/lib
```

## Library overview

The main header is `<mesytec-mcpd/mesytec-mcpd.h>`, all objects live in the
`mesytec::mcpd` namespace.

- [mcpd_core.h](https://github.com/mesytec/mesytec-mcpd/blob/main/src/mesytec-mcpd/mcpd_core.h):
  constants and protocol structures. `CommandPacket` for request/response
  communication, `DataPacket` for readout data. `get_event_count()` and
  `decode_event()` extract `DecodedEvent`s from a data packet.

- [mcpd_functions.h](https://github.com/mesytec/mesytec-mcpd/blob/main/src/mesytec-mcpd/mcpd_functions.h),
  [mdll_functions.h](https://github.com/mesytec/mesytec-mcpd/blob/main/src/mesytec-mcpd/mdll_functions.h):
  one function per device command, e.g. `mcpd_start_daq()`. The first argument
  is the MCPD command socket, the second the MCPD ID. Internally
  `command_transaction()` handles protocol errors and retries.

- [util/udp_sockets.h](https://github.com/mesytec/mesytec-mcpd/blob/main/src/mesytec-mcpd/util/udp_sockets.h):
  `connect_udp_socket()` creates a command socket, `bind_udp_socket()` a data
  socket, `receive_one_packet()` reads a single packet.

- [daq.h](https://github.com/mesytec/mesytec-mcpd/blob/main/src/mesytec-mcpd/daq.h):
  `Daq`, the standard readout setup also used by mcpd-cli and the python
  bindings: readout or replay in a worker thread, listfile writing, per source
  stats and MDLL/MCPD histograms.

- [readout_worker.h](https://github.com/mesytec/mesytec-mcpd/blob/main/src/mesytec-mcpd/readout_worker.h),
  [packet_io.h](https://github.com/mesytec/mesytec-mcpd/blob/main/src/mesytec-mcpd/packet_io.h):
  the building blocks of `Daq`. A `ReadoutWorker` reads from a `PacketSource`
  (`UdpPacketSource`, `ListfilePacketSource`), optionally writes to a
  `ListfileWriter` and passes valid packets to `PacketConsumer`s. Implement
  `PacketConsumer` for custom processing.

Readout using `Daq`:

```cpp
#include <chrono>
#include <iostream>
#include <thread>
#include <mesytec-mcpd/mesytec-mcpd.h>

using namespace mesytec::mcpd;

int main()
{
    std::error_code ec;
    int sock = connect_udp_socket("192.168.168.121", McpdDefaultPort, &ec);
    if (ec)
        return 1;

    Daq daq;
    daq.startReadout("run1.mcpdlst");
    mcpd_start_daq(sock, 0);
    std::this_thread::sleep_for(std::chrono::seconds(10));
    mcpd_stop_daq(sock, 0);
    daq.stop();

    for (const auto &[key, stats]: daq.stats().getStats())
        std::cout << format_ipv4(key.srcAddr) << " id=" << int(key.deviceId)
                  << " packets=" << stats.packets << " events=" << stats.events << "\n";
}
```

Also see the mcpd-cli source code in
[extras/mcpd-cli/mcpd-cli.cc](https://github.com/mesytec/mesytec-mcpd/blob/main/extras/mcpd-cli/mcpd-cli.cc).

# Listfiles and additional tools

Listfiles (`.mcpdlst`) contain the raw data packets as received from the
network. They can be replayed with `mcpd-cli replay`, `mcpd-cli-py replay`, the
GUI or `Daq.start_replay()`.

Additional tools built with mcpd-cli (not installed):

- `mdat2mcpdlst`: converts qmesydaq `.mdat` files to `.mcpdlst`.
- `parse_mcpd_packet_from_hex_string`: decodes and prints a MCPD packet given
  as a hex string, e.g. copied from wireshark.

[extras/scripts/mcpd-wireshark.lua](https://github.com/mesytec/mesytec-mcpd/blob/main/extras/scripts/mcpd-wireshark.lua)
is a wireshark dissector for the MCPD protocol.

# License

[Boost Software License 1.0](https://github.com/mesytec/mesytec-mcpd/blob/main/LICENSE)
