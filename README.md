**mesytec-mcpd - User space driver library for the
 [Mesytec PSD+ system](https://mesytec.com/products/neutron-scattering/MCPD-8.html).**

- [Installation](#installation)
- [Initial MCPD-8\_v1 setup](#initial-mcpd-8_v1-setup)
  - [Setup steps](#setup-steps)
- [Using the C++ interface](#using-the-c-interface)
  - [Quickstart](#quickstart)
    - [CMakeLists.txt](#cmakeliststxt)
    - [mcpd-example.cc](#mcpd-examplecc)
  - [Library Usage](#library-usage)
- [Using the mcpd-cli command line tool](#using-the-mcpd-cli-command-line-tool)
  - [Minimal DAQ setup using one MCPD-8 with two MPSD-8+ modules](#minimal-daq-setup-using-one-mcpd-8-with-two-mpsd-8-modules)
    - [Initialization](#initialization)
    - [Readout Process and DAQ controls](#readout-process-and-daq-controls)
    - [Listfile replay](#listfile-replay)

# Installation

The libraries source code is available on github: https://github.com/flueke/mesytec-mcpd

Building requires a recent version of cmake and a modern c++ compiler (c++14
support required).

The only (optional) external dependency is the ROOT framework. If ROOT is found
(using ``$ROOTSYS``) the ``mcpd-cli`` tool can create ROOT histograms from
received readout data.

Build steps:

     git clone https://github.com/flueke/mesytec-mcpd
     mkdir mesytec-mcpd/build
     cd mesytec-mcpd/build
     cmake -DCMAKE_BUILD_TYPE=Release ..
     make install

The above commands will build and install both the mesytec-mcpd library and the
mcpd-cli command line tool. You can add
``-DCMAKE_INSTALL_PREFIX=$HOME/local/mesytec-mcpd`` to the cmake command line
to change the installation path. Then use:

     export CMAKE_PREFIX_PATH=$HOME/local/mesytec-mcpd

so that cmake will be able to locate the installed library.

# Initial MCPD-8_v1 setup

Each MCPD-8_v1 in a setup needs a unique IP-address and ID. The default
IP-address is ``192.168.168.121``, the default ID is ``0``. These defaults can
be restored by pressing the reset button on the CPU board inside the MCPD NIM
case.

The steps below assume that your local network is ``10.11.12.0/255.255.255.0`` and
your machines IP-address in the local network is ``10.11.12.1``.

## Setup steps

1. Set your PCs network card to ``192.168.168.1/255.255.255.0``.

2. Connect MCPD-8 directly to your PCs network card.

3. Verify connectivity:

   - ``ping 192.168.168.121`` should see a response from the MCPD-8.
   - ``mcpd-cli version`` should be able to connect and read the CPU and FPGA firmware versions.

4. Use ``mcpd-cli`` to set a new IP-address and ID for the MCPD-8:

        mcpd-cli setup 10.11.12.100 0

   The command sets the address to ``10.11.12.100`` and the MCPD-ID to ``0``.

   Note that you will not get a response from the MCPD as it immediately uses
   its new IP-address which is on a different subnet.

5. Repeat the above steps for any additional MCPD-8 modules you want to use
   (connect the modules one by one). Choose unique IP-addresses and IDs for
   each module, e.g.:

       mcpd-cli setup 10.11.12.101 1
       mcpd-cli setup 10.11.12.102 2

6. Change your network card back to your local network: ``10.11.12.1/255.255.255.0``.

   You should now be able to reach the MCPD using the address set in step 4:

        ping 10.11.12.100

7. After moving the MCPD from its default network to your local network the
   data destination IP address has to be set once more as the MCPD still has
   the previous data destination *MAC-Address* stored. Use the following to
   update the data destination MAC address:

        mcpd-cli --address=10.11.12.100 setup 10.11.12.100 0

   This leaves the MCPD address and ID unchanged and sets the data destination
   address to the source computers address.

8. Repeat the above steps for any additional MCPD-8 modules using their
   respective IP-addresses and IDs.

Note: Alternatively instead of changing your networks IP-address to reach the
modules you could instead add static ARP entries using the MAC address printed
on a sticker on the CPU board inside the MCPD-8 NIM case.

The initial setup is done. You should be able to communicate with the MCPD-8
modules using their newly assigned IP-addresses. The changes made are permanently
stored in the flash memory of each module. Defaults can be restored by pressing
the reset button on the CPU board inside the MCPD NIM housing.

# Using the C++ interface

## Quickstart

A minimal CMake example project can be found under ``extras/cmake-example``.
This can serve as the basis for custom code. The example should work as long as
cmake is able to locate the installed mesytec-mcpd library. If using a
non-standard installation path you have to tell CMake about it:

    export CMAKE_PREFIX_PATH=$HOME/local/mesytec-mcpd

### CMakeLists.txt

    cmake_minimum_required(VERSION 3.12)
    project(mesytec-mcpd-cmake-example)

    find_package(mesytec-mcpd REQUIRED)

    add_executable(mcpd-example mcpd-example.cc)
    target_link_libraries(mcpd-example PRIVATE mesytec-mcpd::mesytec-mcpd)

### mcpd-example.cc

The example program below connects to a MCPD and attempts to read out the CPU
and FPGA version information.

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

## Library Usage

The main header to include is ``mesytec-mcpd.h``. This pulls in the other
required headers. All objects live in the ``mesytec::mcpd`` namespace.

Constants and core data structures can be found in ``mcpd_core.h``:

- ``CommandPacket`` is used for direct request/response communication.

- ``DataPacket`` carries DAQ readout data. Use ``get_event_count()`` to get the
  number of events contained in a DataPacket. Then call ``get_event()`` to
  extract the specified ``DecodedEvent`` event from a data packet.

Socket abstractions can be found in ``util/udp_sockets.h``. To create a command
socket for the MCPD use ``connect_udp_socket()``. To create a listening socket
for DAQ data call ``bind_udp_socket()``.

MCPD and MPSD related functions are contained in ``mcpd_functions.h``. Most
commands are implemented by a specific function, e.g ``mcpd_start_daq()``. These
functions take a MCPD command socket as their first argument and an MCPD ID value as
their second argument. Possible other arguments are used to fill the outgoing
request ``CommandPacket``.

Internally the command functions call ``command_transaction()`` which handles
protocol errors and retries.

Currently no dedicated readout functions are implemented. Instead create a
socket listening on the data port and call ``receive_one_packet()``
repeatedly:

    std::error_code ec = {};
    DataPacket dataPacket = {};
    size_t timeouts = 0;
    // Socket bound to local port 54322 on all interfaces.
    int dataSock = bind_udp_socket(54322, &ec);

    if (ec) return 1;

    while (true)
    {
       size_t bytesTransferred = 0u;

       ec = receive_one_packet(
           dataSock,
           reinterpret_cast<u8 *>(&dataPacket), sizeof(dataPacket),
           bytesTransferred, DefaultReadTimeout_ms);

       if (ec)
       {
           if (ec == std::errc::interrupted)
               break;

           if (ec != SocketErrorType::Timeout)
           {
               spdlog::error("readout: error reading from network: {} ({}, {})",
                             ec.message(), ec.value(), ec.category().name());
               return 1;
           }
           else
               ++timeouts;
       }

       if (bytesTransferred)
       {
          const auto eventCount = get_event_count(dataPacket);

          // Decode and print each incoming event
          for(size_t ei=0; ei<eventCount; ++ei)
          {
             auto event = decode_event(dataPacket, ei);
             spdlog::info("{}", to_string(event));
          }
       }
    }

Also see the mcpd-cli source code under ``extras/mcpd-cli/mcpd-cli.cc``.

# Using the mcpd-cli command line tool

It is possible to setup and run a DAQ using only the ``mcpd-cli`` tool without
having to write any custom code. The CLI tool allows to initialze MCPD-8 and
connected MPSD-8+ modules, start a DAQ run and write the readout data to a
listfile. If ROOT support is enabled histograms for amplitudes, positions and
times can be automatically created and filled by the readout process.

``mcpd-cli`` uses the following enviroment variables if defined:

* ``MCPD_ADDRESS`` is the ip-address/hostname of the MCPD to connect to (e.g. ``192.168.168.121``.
* ``MCPD_ID`` is the ID assigned in the setup step.

These values are used as the ``--address`` and ``--id`` parameters of
``mcpd-cli`` if not explicitly specified.

## Minimal DAQ setup using one MCPD-8 with two MPSD-8+ modules

### Initialization

    # Set the runId for the next DAQ run
    mcpd-cli --address=10.11.12.100 --id=0 runid 1

    # Set thresholds for MPSDs on bus 0 and 1 to 0
    mcpd-cli --address=10.11.12.100 --id=0 mpsd_set_threshold 0 0
    mcpd-cli --address=10.11.12.100 --id=0 mpsd_set_threshold 1 0

    # enable pulser, mpsd=0, channel=0, pos=2 (middle), amplitude=128, state=on
    mcpd-cli --address=10.11.12.100 --id=0 mpsd_set_pulser 0 0 2 128 on

    # enable pulser, mpsd=1, channel=0, pos=1 (right), amplitude=64, state=on
    mcpd-cli --address=10.11.12.100 --id=0 mpsd_set_pulser 1 0 1 64 on

### Readout Process and DAQ controls

In a second terminal start the readout process:

    mcpd-cli --address=10.11.12.100 --id=0 readout --duration=60 --listfile=mcpd-run1.mcpdlst

This process will run for 60 seconds or until canceled via ``ctrl-c``. If ROOT support is enabled you can use::

    mcpd-cli --address=10.11.12.100 --id=0 readout --duration=60 --listfile=mcpd-run1.mcpdlst --root-histo-file=mcpd-run1-histos.root

to write out ROOT histograms.

In the first terminal tell the MCPD-8 to start the DAQ:

    mcpd-cli --address=10.11.12.100 --id=0 daq start

Readout data should now arrive at the readout process. ``mcpd-cli readout``
does listen on the specified data port (default is 54321) but accepts packets
from all sources. This means the readout process can handle data coming from
multiple MCPD-8 modules as long as they have unique IDs set.

### Listfile replay

To replay data from listfile use:

    mcpd-cli replay --listfile=mcpd-run1.mcpdlst

The replay command can also generate root histograms:

    mcpd-cli replay --listfile=mcpd-run1.mcpdlst --root-histo-file=mcpd-replay1-histos.root
