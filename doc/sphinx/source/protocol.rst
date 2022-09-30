PSD+ network protocol
#########################################################

Command Packets
==================================================

Each command buffer consists of a buffer header (9 x 16 bit = 18 bytes) and a
trailing data block of variable length. The contents of the data block depends
on the individual commands.

The total length of a command buffer varies between 18 bytes (header only) up
to 1.500 bytes (limited by unfragmented Ethernet frame length) (Padding bytes
to keep Ethernet minimum buffer sizes must be added).

.. table:: Command Packet structure (in 16 bit words)
  :name: mcpd-command-packet-structure

  +---------------------------------+-------------------------+
  | Buffer Length (in 16 bit words) | Word 0                  |
  +---------------------------------+-------------------------+
  | Buffer Type                     |                         |
  +---------------------------------+-------------------------+
  | Header Length (in 16 bit words) |                         |
  +---------------------------------+-------------------------+
  | Buffer Number                   |                         |
  +---------------------------------+-------------------------+
  | Cmd                             |                         |
  +---------------------------------+-------------------------+
  | MCPD-ID                         | Status                  |
  +---------------------------------+-------------------------+
  | Header Timestamp Lo             |                         |
  +---------------------------------+-------------------------+
  | Header Timestamp Mid            |                         |
  +---------------------------------+-------------------------+
  | Header Timestamp Hi             | Word 8                  |
  +---------------------------------+-------------------------+
  | Command Checksum                | Word 9                  |
  +---------------------------------+-------------------------+
  | Data 0                          | Word 10                 |
  +---------------------------------+-------------------------+
  | Data 1                          |                         |
  +---------------------------------+-------------------------+
  | Data 2                          |                         |
  +---------------------------------+-------------------------+
  | ...                             |                         |
  +---------------------------------+-------------------------+
  | Trailing data                   |                         |
  +---------------------------------+-------------------------+
  | with                            |                         |
  +---------------------------------+-------------------------+
  | Variable length                 | Word (buffer length –1) |
  +---------------------------------+-------------------------+

Use and meaning of header data varies depending on sender. While MCPD-8 fills
in all values, some of them may be left uninitialized when sending a command
packet from a control pc to a MCPD-8.

* Buffer Type

  - 16 bit type descriptor
  - Bits 0 … 14 carry a version information and may be left blank in buffers sent by control pc.
  - Bit 15 = 0: data buffer
  - Bit 15 = 1: command buffer

* Header Length

  Length of header information in 16 bit words

* Buffer Number

  - Simple 16 bit counter to allow loss monitoring.
  - Separate counters for data and cmd buffers.
  - A control software could increment with each cmd issued.
  - MCPD-8 will increment its own counter with each cmd answered.

* Buffer Length

  - In multiple of 16 bit words. Spans from “Buffer Type” to last data word.

  - Only counts useful data words.

  - Padding bytes added to fulfill minimum Ethernet buffer size will not be
    counted.

* CMD-ID

  16 bit value representing the command that is answered / issued in this
  buffer. Please see the following chapter for a detailed description of the
  individual commands.

* MCPD-ID

  ID of the addressed / sending MCPD module, to be assigned during hardware
  initialisation.

* Status

  8 bit wide bit field for sync and start/stop status. Can be left blank in
  buffers sent by control pc.

  bit0: 0=DAQ stopped, 1=DAQ running

  bit3: 0=sync ok, 1=sync error

* Header Timestamp

  48 bits synchronized system timer (100 ns binning) generated shortly before
  Ethernet transmission. It allows to have a timed log of command
  communication. Can be left blank in buffers sent by control pc.

* Command Checksum:

  16 bit XOR checksum covering all words from “Buffer Type” (Word 0) to last data word (Word buffer length –1).

  Set checksum field to 0x0000 before calculation.

Commands
==================================================

.. table:: MCPD command list
  :name: mcpd-command-list

  +---------------------+------------+------------------------------------------------------------+
  | Command Name        | Command Id | Notes                                                      |
  +=====================+============+============================================================+
  | Reset               | 0          | Stop DAQ, reset counters/timers. Master only.              |
  +---------------------+------------+------------------------------------------------------------+
  | StartDAQ            | 1          | Start DAQ. Master only.                                    |
  +---------------------+------------+------------------------------------------------------------+
  | StopDAQ             | 2          | Stop DAQ and counters/timers. Master only.                 |
  +---------------------+------------+------------------------------------------------------------+
  | ContinueDAQ         | 3          | Restart DAQ. Master only.                                  |
  +---------------------+------------+------------------------------------------------------------+
  | SetId               | 4          | Set a unique mcpd id.                                      |
  +---------------------+------------+------------------------------------------------------------+
  | SetProtoParams      | 5          | Set network parameters.                                    |
  +---------------------+------------+------------------------------------------------------------+
  | SetTiming           | 6          | Set bus timing role and termination.                       |
  +---------------------+------------+------------------------------------------------------------+
  | SetClock            | 7          | Set master clock value.                                    |
  +---------------------+------------+------------------------------------------------------------+
  | SetRunId            | 8          | Set the next DAQ run id.                                   |
  +---------------------+------------+------------------------------------------------------------+
  | SetCell             | 9          | Counter/ADC cell setup.                                    |
  +---------------------+------------+------------------------------------------------------------+
  | SetAuxTimer         | 10         | Aux timer setup.                                           |
  +---------------------+------------+------------------------------------------------------------+
  | SetParam            | 11         | Data parameter setup.                                      |
  +---------------------+------------+------------------------------------------------------------+
  | GetParams           | 12         | Read current parameter values.                             |
  +---------------------+------------+------------------------------------------------------------+
  | SetGain             | 13         | Set per channel MPSD gain.                                 |
  +---------------------+------------+------------------------------------------------------------+
  | SetThreshold        | 14         | Set per MPSD threshold.                                    |
  +---------------------+------------+------------------------------------------------------------+
  | SetPulser           | 15         | Set per channel MPSD pulser.                               |
  +---------------------+------------+------------------------------------------------------------+
  | SetMpsdMode         | 16         | Untested: set either position or amplitude mode.           |
  +---------------------+------------+------------------------------------------------------------+
  | SetDAC              | 17         | Set output DAC values.                                     |
  +---------------------+------------+------------------------------------------------------------+
  | SendSerial          | 18         | // Note: not implemented in the firmware                   |
  +---------------------+------------+------------------------------------------------------------+
  | ReadSerial          | 19         | // Note: not implemented in the firmware                   |
  +---------------------+------------+------------------------------------------------------------+
  | ScanPeriphery       | 20         | // undocumented                                            |
  +---------------------+------------+------------------------------------------------------------+
  | SetTTLOutputs       | 21         | // undocumented                                            |
  +---------------------+------------+------------------------------------------------------------+
  | GetBusCapabilities  | 22         | // untested: MCPD-8 eventbus format                        |
  +---------------------+------------+------------------------------------------------------------+
  | SetBusCapabilities  | 23         | // untested: MCPD-8 eventbus format                        |
  +---------------------+------------+------------------------------------------------------------+
  | GetMpsdParams       | 24         | Reads MPSD eventbus and firmware information.              |
  +---------------------+------------+------------------------------------------------------------+
  | SetFastTxMode       | 25         | // undocumented                                            |
  +---------------------+------------+------------------------------------------------------------+
  | ReadIds             | 36         | Undocumented: scans the busses for MPSD-8 modules.         |
  +---------------------+------------+------------------------------------------------------------+
  | GetVersion          | 51         | Read MCPD CPU and FPGA firmware revision.                  |
  +---------------------+------------+------------------------------------------------------------+



Reset
--------------------------------------------------

Running DAQ will be stopped. All counters and timers will be reset to 0. Reset
signal is propagated over the sync line. Thus it is not necessary to send a
reset signal to each individual MCPD-8. MCPD-8 not set as master will refuse
command.



StartDAQ
--------------------------------------------------

Start DAQ starts the data acquisition system.
All timers (master timer + auxiliary timers) start / continue running.
Neutron and trigger events will be filled into data buffers.

Start signal is propagated over the sync line. Thus it is not necessary to send
a start signal to each individual MCPD-8.  MCPD-8 not set as master will refuse
command.



StopDAQ
--------------------------------------------------

Stop DAQ stops the data acquisition system.
All timers (master timer + auxiliary timers) stop running.

Stop signal is propagated over the sync line. Thus it is not necessary to send
a stop signal to each individual MCPD-8.  MCPD-8 not set as master will refuse
command.


ContinueDAQ
--------------------------------------------------

Continue DAQ restarts the data acquisition system.
All timers (master timer + auxiliary timers) will continue running.

Stop signal is propagated over the sync line. Thus it is not necessary to send
a stop signal to each individual MCPD-8.  MCPD-8 not set as master will refuse
command.


SetId
--------------------------------------------------

.. table:: SetId arguments
  :name: SetId-arguments

  +----------+-------------+
  | SetId arguments        |
  +----------+-------------+
  | DataWord | Contents    |
  +==========+=============+
  | 10       | ID (0..255) |
  +----------+-------------+


Each MCPD in a setup is given an individual ID number (8 bit). The ID is part
of the header of every data / cmd packet the MCPD emits. Thus data can be
assigned to a defined part of the readout system during data processing.

It is in the responsibility of the user (= frontend programmer) to keep IDs
unique throughout the readout system.


SetProtoParams
--------------------------------------------------

Allows changing the MCPD network protocol settings.

.. table:: SetProtoParams arguments
  :name: SetProtoParams-arguments

  +----------+----------------------------------------------+
  | SetProtoParams arguments                                |
  +==========+==============================================+
  | DataWord | Contents                                     |
  +----------+----------------------------------------------+
  | 10       | MCPD ip 0                         (e.g. 192) |
  +----------+----------------------------------------------+
  | 11       | MCPD ip 1                         (e.g. 168) |
  +----------+----------------------------------------------+
  | 12       | MCPD ip 2                         (e.g. 168) |
  +----------+----------------------------------------------+
  | 13       | MCPD ip 3                         (e.g. 121) |
  +----------+----------------------------------------------+
  | 14       | Data sink ip 0                               |
  +----------+----------------------------------------------+
  | 15       | Data sink ip 1                               |
  +----------+----------------------------------------------+
  | 16       | Data sink ip 2                               |
  +----------+----------------------------------------------+
  | 17       | Data sink ip 3                               |
  +----------+----------------------------------------------+
  | 18       | Cmd UPD Port                                 |
  +----------+----------------------------------------------+
  | 19       | Data UPD Port                                |
  +----------+----------------------------------------------+
  | 20       | Cmd pc ip 0                                  |
  +----------+----------------------------------------------+
  | 21       | Cmd pc ip 1                                  |
  +----------+----------------------------------------------+
  | 22       | Cmd pc ip 2                                  |
  +----------+----------------------------------------------+
  | 23       | Cmd pc ip 3                                  |
  +----------+----------------------------------------------+

* MCPD ip:

  The IP address of the MCPD can be remotely changed. A hardware button on the
  CPU module of the MCPD allows a reset to the factory address 192.168.168.121.
  (This address is also mentioned as an example above to explain byte order).

  MCPD ip address will not be modified if MCPD ip 0 (Word 10) is set to zero.

* Data sink ip:

  Also the destination ip address for data packages can be set individually.
  (If no address is set: the address of the cmd pc is used automatically).

  Address will not be modified if Data sink ip 0 (Word 14) is set to zero.

  If ip0 as well as ip1 are set to 0, the address of the pc sending this
  command will be used automatically from out of the ip protocol. This allows
  to set the address to the sending pc without knowing its address explicitly.

* Cmd pc ip:

  This allows to set a defined address for the pc that will send the cmds. No
  other pc will then be able to take control over the system unless the new
  address is published by the current cmd pc.

  Address will not be modified if Cmd pc ip 0 (Word 20) is set to zero.

  If Cmd pc ip0 as well as Cmd Pc ip1 are set to 0, the address of the pc
  sending this command will be used automatically from out of the ip protocol.
  This allows to set the address to the sending pc without knowing its address
  explicitly.

* UDP ports

  MCPD-8 is able to use individually set UPD port numbers, possibly different
  ones for cmd and data. No change if fields are set to zero.

The following table gives an overview of the possible settings:

.. table:: SetProtoParams behavior

  +---------------------------------+-------+-------------------------------------------------------+
  | Field(s)                        | Value | Meaning                                               |
  +=================================+=======+=======================================================+
  | MCPD ip0                        | 0     | Do not change MCPD ip address                         |
  +---------------------------------+-------+-------------------------------------------------------+
  |                                 | > 0   | Set MCPD ip address to values in word 10 ... 13       |
  +---------------------------------+-------+-------------------------------------------------------+
  | Data sink ip0                   | 0     | Do not change Data sink ip address                    |
  +---------------------------------+-------+-------------------------------------------------------+
  |                                 | > 0   | Set data sink ip address to values in word 14 ... 17  |
  +---------------------------------+-------+-------------------------------------------------------+
  | Data sink ip0 and Data sink ip1 | 0     | Set data sink ip address to address of cmd sending pc |
  +---------------------------------+-------+-------------------------------------------------------+
  | Cmd pc ip0                      | 0     | Do not change Data sink ip address                    |
  +---------------------------------+-------+-------------------------------------------------------+
  |                                 | > 0   | Set cmd pc ip address to values in word 20 ... 23     |
  +---------------------------------+-------+-------------------------------------------------------+
  | Cmd pc ip0 and Cmd pc ip1       | 0     | Set cmd pc ip address to address of cmd sending pc    |
  +---------------------------------+-------+-------------------------------------------------------+
  | Udp port                        | 0     | Do not modify                                         |
  +---------------------------------+-------+-------------------------------------------------------+
  |                                 | > 0   | Set to given value                                    |
  +---------------------------------+-------+-------------------------------------------------------+


SetTiming
--------------------------------------------------

Sets timing properties:

- Please make sure that only one MCPD-8 is set as sync master!

- Sync bus has to be terminated at both ends – master is terminated
  automatically, last slave on bus has to be terminated.

.. table:: SetTiming arguments
  :name: SetTiming-arguments

  +------+----------------------+
  | Word | Contents             |
  +======+======================+
  | 10   | Timing / Sync master |
  |      | (0: MCPD is slave,   |
  |      | 1: MCPD is master)   |
  +------+----------------------+
  | 11   | Sync bus termination |
  |      | (0 = on, 1 = off)    |
  +------+----------------------+


SetClock
--------------------------------------------------

.. table:: SetClock arguments
  :name: SetClock-arguments

  +------+----------------------------+
  | Word | Contents                   |
  +======+============================+
  | 10   | Master clock, bits 0 … 15  |
  +------+----------------------------+
  | 11   | Master clock, bits 16 … 31 |
  +------+----------------------------+
  | 12   | Master clock, bits 32 …47  |
  +------+----------------------------+

Master clock can be set to any value if desired. Normally, a reset is initiated
before a new run and all counters are set to zero during this reset
automatically. Only if another run start time than zero is desired, this
registers must be set.

SetRunId
--------------------------------------------------

.. table:: SetRunId arguments
  :name: SetRunId-arguments

  +------+----------------------------+
  | Word | Contents                   |
  +======+============================+
  | 10   | RunId                      |
  +------+----------------------------+

Set value for the header field “Run ID”. Can be set to any desired value.

The master MCPD-8 distributes its Run ID over the sync bus. Thus it’s only
necessary to set the Run Id at the master module.


SetCell
--------------------------------------------------

.. table:: SetCell arguments
  :name: SetCell-arguments

  +------+---------------------------------------------------+
  | Word | Contents                                          |
  +======+===================================================+
  | 10   | Cell #:                                           |
  |      |                                                   |
  |      | - 0 … 3: monitor / chopper inputs 1…4             |
  |      | - 4, 5: dig. backpanel inputs 1, 2                |
  |      | - 6, 7: ADC 1, 2                                  |
  +------+---------------------------------------------------+
  | 11   | Trigger source:                                   |
  |      |                                                   |
  |      | - 0 = no trigger                                  |
  |      | - 1 … 4: trigger on aux timer 1… 4                |
  |      | - 5, 6: trigger on rising edge at rear input 1, 2 |
  |      | - 7: trigger from compare register                |
  |      |   (7 only for counter cells)                      |
  +------+---------------------------------------------------+
  | 12   | Compare register, numerical value n:              |
  |      |                                                   |
  |      | - 0 … 20: trigger on bit n = 1                    |
  |      | - 21: trigger on counter overflow                 |
  |      | - 22: trigger on rising edge of input             |
  |      |   (can be left blank for ADC cells)               |
  +------+---------------------------------------------------+

This command configures the given counter cell:

One of six possible cells is addressed. The value of the according 21 bit
counter is transmitted as a trigger event when triggered.

Trigger source can be one of the digital inputs, one of the four auxiliary
timers or a special compare register.  Please note that the compare register
does not do a full compare, but checks for a ‘1’ at the given bit position,
allowing for triggers at multiples of 2.

Counter cells are intended to generate repeated trigger events. They can be
used e.g. for a continuous monitoring of counter values and ADC inputs.

Choosing the rising signal edge as trigger source enables to generate a (fully
timestamped) event e.g. for each chopper signal and allows precise chopper
timing calculation.



SetAuxTimer
--------------------------------------------------

.. table:: SetAuxTimer arguments
  :name: SetAuxTimer-arguments

  +------+--------------------------------------------------------------------+
  | Word | Contents                                                           |
  +======+====================================================================+
  | 10   | Timer #: (0 … 3)                                                   |
  +------+--------------------------------------------------------------------+
  | 11   | Capture register: (0 … 65.536)                                     |
  |      | Time base is 10 us, allowing for intervals from 10 us to 655,36 ms |
  +------+--------------------------------------------------------------------+

Auxiliary timer compare register is set to the given value.

An identical compare generates a trigger signal (that might be used in one of
the counter / ADC cells) and resets the counter to zero. Thus four independent
triggers with periods between 10 us and 655,36 ms are possible.


SetParam
--------------------------------------------------

.. table:: SetParam arguments
  :name: SetParam-arguments

  +------+-------------------------------------------------------------+
  | Word | Contents                                                    |
  +======+=============================================================+
  | 10   | Parameter: (0 … 3)                                          |
  +------+-------------------------------------------------------------+
  | 11   | Source:                                                     |
  |      |                                                             |
  |      | - 0 … 3: Monitor/Chopper inputs 1…4                         |
  |      | - 4, 5: backpanel TTL inputs 1, 2                           |
  |      | - 6: combination of all digital inputs, and both ADC values |
  |      | - 7: event counter                                          |
  |      | - 8: master clock                                           |
  +------+-------------------------------------------------------------+

Defines the counter source for the given parameter.

While 0 … 5 are real counters, 6 delivers a combination of the current status
of all defined inputs and 7, 8 get copies of the current value of event counter
or master clock.

All four Parameter values are transmitted with every data buffer, delivering a
continuous monitoring information.

GetParams
--------------------------------------------------

Requests all available parameter information.

.. table:: GetParams response buffer
  :name: GetParams-response

  +------+-----------------------+
  | Word | Contents              |
  +======+=======================+
  | 10   | ADC 1 (12 valid bits) |
  +------+-----------------------+
  | 11   | ADC 2 (12 valid bits) |
  +------+-----------------------+
  | 12   | DAC 1 (12 bits)       |
  +------+-----------------------+
  | 13   | DAC 2 (12 bits)       |
  +------+-----------------------+
  | 14   | TTL outputs (2 bits)  |
  +------+-----------------------+
  | 15   | TTL inputs (6 bits)   |
  +------+-----------------------+
  | 16   | Event counter Lo      |
  +------+-----------------------+
  | 17   | Event counter Mid     |
  +------+-----------------------+
  | 18   | Event counter Hi      |
  +------+-----------------------+
  | 19   | Parameter 0 Lo        |
  +------+-----------------------+
  | 20   | Parameter 0 Mid       |
  +------+-----------------------+
  | 21   | Parameter 0 Hi        |
  +------+-----------------------+
  | 22   | Parameter 1 Lo        |
  +------+-----------------------+
  | 23   | Parameter 1 Mid       |
  +------+-----------------------+
  | 24   | Parameter 1 Hi        |
  +------+-----------------------+
  | 23   | Parameter 2 Lo        |
  +------+-----------------------+
  | 26   | Parameter 2 Mid       |
  +------+-----------------------+
  | 28   | Parameter 2 Hi        |
  +------+-----------------------+
  | 29   | Parameter 3 Lo        |
  +------+-----------------------+
  | 30   | Parameter 3 Mid       |
  +------+-----------------------+
  | 31   | Parameter 3 Hi        |
  +------+-----------------------+

SetGain
--------------------------------------------------

Set per channel MPSD gain.

.. table:: SetGain arguments
  :name: SetGain-arguments

  +------+--------------------------------------+
  | Word | Contents                             |
  +======+======================================+
  | 10   | MPSD device number (0 … 7)           |
  +------+--------------------------------------+
  | 11   | Channel within MPSD (0 … 7, 8 = all) |
  +------+--------------------------------------+
  | 12   | Gain value (0 … 255)                 |
  +------+--------------------------------------+

Each channel gain can be set individually. To facilitate a quick setup, using
channel number 8 will write the same gain value to all channels of the
addressed MPSD-8 module.

SetThreshold
--------------------------------------------------

Set MPSD threshold.

.. table:: SetThreshold arguments
  :name: SetThreshold-arguments

  +------+----------------------------+
  | Word | Contents                   |
  +======+============================+
  | 10   | MPSD device number (0 … 7) |
  +------+----------------------------+
  | 11   | Threshold value (0 … 255)  |
  +------+----------------------------+

Each peripheral module MPSD-8 has one common lower threshold for its window
discriminator. An 8 bit value is used to set the lower discriminator threshold.

SetPulser
--------------------------------------------------
.. table:: SetPulser arguments
  :name: SetPulser-arguments

  +------+-----------------------------------------------------------+
  | Word | Contents                                                  |
  +======+===========================================================+
  | 10   | MPSD device number (0 … 7)                                |
  +------+-----------------------------------------------------------+
  | 11   | Channel within MPSD (0 … 7)                               |
  +------+-----------------------------------------------------------+
  | 12   | Position within channel (0 = left, 1 = right, 2 = middle) |
  +------+-----------------------------------------------------------+
  | 13   | Pulser amplitude (0 … 255)                                |
  +------+-----------------------------------------------------------+
  | 14   | Pulser on/off (0 = off, 1 = on)                           |
  +------+-----------------------------------------------------------+

A builtin test pulser is useful to check electronics performance without the
need of “real” neutron events.

The pulser can be set to 3 positions (left, middle, right) in a psd channel.
Furthermore, the pulser amplitude can be controlled and pulser function can be
switched on/off.

Be sure to switch all pulsers off before starting neutron recording!

SetDAC
--------------------------------------------------
.. table:: SetDAC arguments
  :name: SetDAC-arguments

  +------+-----------------------+
  | Word | Contents              |
  +======+=======================+
  | 10   | DAC 0 (12 valid bits) |
  +------+-----------------------+
  | 11   | DAC 1 (12 valid bits) |
  +------+-----------------------+

MCPD-8 offers two DAC ports that can be set in a 12 bit range.

Full range output voltage is +/- 3V or 0…6 V, according to jumper setting in
MCPD-8.

.. SetTTLOutputs
.. --------------------------------------------------
.. .. table:: SetTTLOutputs arguments
..   :name: SetTTLOutputs-arguments

GetMpsdParams
--------------------------------------------------

Retrieves contents of MPSD-8 parameter registers.

.. table:: GetMpsdParams arguments
  :name: GetMpsdParams-arguments

  +------+-----------------------------+
  | Word | Contents                    |
  +======+=============================+
  | 10   | MPSD device number  (0 … 7) |
  +------+-----------------------------+

Response:

.. table:: GetMpsdParams response
  :name: GetMpsdParams-response

  +------+-----------------------------------------+
  | Word | Contents                                |
  +======+=========================================+
  | 10   | MPSD device number (0 …  7)             |
  +------+-----------------------------------------+
  | 11   | Eventbus transmit capabilities          |
  +------+-----------------------------------------+
  | 12   | Current eventbus fast tx format setting |
  +------+-----------------------------------------+
  | 13   | Firmware revision                       |
  +------+-----------------------------------------+

ReadId
--------------------------------------------------

Scans the data busses for connected MPSD modules.

Response:

.. table:: ReadId response
  :name: ReadId-response

  +------+-----------------------------------------+
  | Word | Contents                                |
  +======+=========================================+
  | 10   | MPSD bus1                               |
  +------+-----------------------------------------+
  | 11   | MPSD bus2                               |
  +------+-----------------------------------------+
  | ...  | ...                                     |
  +------+-----------------------------------------+
  | ...  | MPSD bus8                               |
  +------+-----------------------------------------+

GetVersion
--------------------------------------------------

Returns version information of MCPD-8 microcontroller and FPGA firmware.

.. table:: GetVersion response buffer
  :name: GetVersion-response

  +------+---------------------------------------------+
  | Word | Contents                                    |
  +======+=============================================+
  | 10   | Major CPU software version                  |
  +------+---------------------------------------------+
  | 11   | Minor CPU software version                  |
  +------+----------------------------+----------------+
  | 12   | Maj. FPGA ver.             | Min. FPGA ver. |
  +------+----------------------------+----------------+



Data Packets
==================================================

During data acquisition, the MCPD modules transmit a continuous stream of event
buffers.

Each event buffer consists of a buffer header (20 x 16 bit = 40 bytes) and a
variable number of events. Each event has a length of 48 bits = 6 bytes.

The total length of an event buffer varies between 40 bytes (header only) up to
1.500 bytes (limited by unfragmented Ethernet frame length).

.. table:: Data Packet structure (in 16 bit words)
  :name: mcpd-data-packet-structure

  +---------------------------------+-------------+
  | Buffer Length (in 16 bit words) | Word 0      |
  +---------------------------------+-------------+
  | Buffer Type                     |             |
  +---------------------------------+-------------+
  | Header Length (in 16 bit words) |             |
  +---------------------------------+-------------+
  | Buffer Number                   |             |
  +---------------------------------+-------------+
  | Run-ID                          |             |
  +---------------------------------+-------------+
  | MCPD-ID                         | Status      |
  +---------------------------------+-------------+
  | Header Timestamp Lo             |             |
  +---------------------------------+-------------+
  | Header Timestamp Mid            |             |
  +---------------------------------+-------------+
  | Header Timestamp Hi             |             |
  +---------------------------------+-------------+
  | Parameter 0 Lo                  |             |
  +---------------------------------+-------------+
  | Parameter 0 Mid                 |             |
  +---------------------------------+-------------+
  | Parameter 0 Hi                  |             |
  +---------------------------------+-------------+
  | Parameter 1 Lo                  |             |
  +---------------------------------+-------------+
  | Parameter 1 Mid                 |             |
  +---------------------------------+-------------+
  | Parameter 1 Hi                  |             |
  +---------------------------------+-------------+
  | Parameter 2 Lo                  |             |
  +---------------------------------+-------------+
  | Parameter 2 Mid                 |             |
  +---------------------------------+-------------+
  | Parameter 2 Hi                  |             |
  +---------------------------------+-------------+
  | Parameter 3 Lo                  |             |
  +---------------------------------+-------------+
  | Parameter 3 Mid                 |             |
  +---------------------------------+-------------+
  | Parameter 3 Hi                  | Word 20     |
  +---------------------------------+-------------+
  | Event 0 Lo                      | Word 21     |
  +---------------------------------+-------------+
  | Event 0 Mid                     |             |
  +---------------------------------+-------------+
  | Event 0 Hi                      |             |
  +---------------------------------+-------------+
  | ...                             |             |
  +---------------------------------+-------------+
  | Event n Lo                      |             |
  +---------------------------------+-------------+
  | Event n Mid                     |             |
  +---------------------------------+-------------+
  | Event n Hi                      | Word 20+3*n |
  +---------------------------------+-------------+

.. only:: html

   |


Event structure
--------------------------------------------------

Each event has a fixed 48 bit length. The contents differs according to the event id:

* ID = 0: Neutron data event
* ID = 1: Trigger data event

Neutron Event
~~~~~~~~~~~~~

.. table:: Neutron event structure
  :name: NeutronEvent-structure

  +----------------+---------------+------------+---------------+---------------+----------------+
  | MSB            |                                                            | LSB            |
  +================+===============+============+===============+===============+================+
  | ID (1 bit) = 0 | ModID (3 bit) | SlotID (5) | Amplitude(10) | Position (10) | Timestamp (19) |
  +----------------+---------------+------------+---------------+---------------+----------------+

* ID

  ID = 0 signalling a “neutron” event , resulting from a detector event at a
  peripheral modules like MPSD-8. (Monitor counter events e.g., that of course
  also are “neutron events” are generated at the MCPD-8, don’t carry a position
  information and are therefore regarded as “other events” in this context.)

  1 bit

* ModID

  Module ID of MPSD-8, determined by serial bus number (bus input at MCPD-8)

  3 bit

* Channel

  channel (slot) number in the MPSD module: [0…7] for MPSD-8 and MSTD-16

  5 bit (but 2 MSBs not used, only [2...0] are valid!)

* Amplitude:

  amplitude (energy) of the neutron event if protocol TPA is selected,
  otherwise = 0

  - MPSD-8+: 	10 valid bits
  - MPSD-8: 	8 valid bits, bits 0, 1 = 0!
  - MSTD-16+: 	9 valid bits [8...0]

* Position

  position of the neutron event

  10 bit

* Timestamp

  timing offset to the corresponding header timestamp

  event time = header timestamp + event timestamp

  19 bit


Address reconstruction
^^^^^^^^^^^^^^^^^^^^^^

The complete, two dimensional address of a neutron event consists of max. 16 +
10 bit and is composed by the following partial information:

* Channel (= individual detector tube):

  - MCPD-ID: MCPD- branch, if multiple			8 bit
  - ModID:   Bus number on identified MCPD-8		3 bit
  - SlotId:  Subchannel within identified MPSD-8	5 bit

.. table:: Channel Address

  +------------+-----------+-----------+
  | Bit 15 … 8 | Bit 7 … 5 | Bit 4 … 0 |
  +============+===========+===========+
  | MCPD-ID    | ModID     | SlotId    |
  +------------+-----------+-----------+


A system using only MPSD-8 can reduce the address length further:

  - ModID has only 3 valid bits
  - MCPD-ID normally doesn’t use the full 8 bit range (but is due to users
    definition!)


So a reduced calculated address format for a system using 4 MCPD-8 and a total
maximum of 4 (MCPD-8) x 8 (MPSD-8) x 8 (Detectors/MPSD-8) = 256 detectors could
look like this:

.. table:: Reduced Channel Address

  +-----------+-----------+-----------+
  | Bit 7 … 6 | Bit 5 … 3 | Bit 2 … 0 |
  +===========+===========+===========+
  | MCPD-ID   | ModID     | SlotId    |
  +-----------+-----------+-----------+


Trigger Event
~~~~~~~~~~~~~

Several trigger sources (counters, timers, digital inputs) can initiate a data
taking event. Triggered by levels or defined overflows of the trigger sources,
data are taken from the data sources and written to an event structure.
Possible trigger and data sources are timers, counters, and ADC values.

.. table:: Trigger event structure
  :name: TriggerEvent-structure

  +----------------+----------------+------------+---------------+----------------+
  | MSB            |                                             | LSB            |
  +================+================+============+===============+================+
  | ID (1 bit) = 1 | TrigID (3 bit) | DataID (4) | Data (21 bit) | Timestamp (19) |
  +----------------+----------------+------------+---------------+----------------+

* ID

  ID = 1 signalling a “not neutron” event (= generated centrally in MCPD-8).
  Possible trigger and data sources are:

  - Counters
  - Timers
  - Digital inputs

  1 bit

* TrigID

  Trigger ID characterizing the event trigger source:

  - 1 … 4: Timer 1 … 4
  - 5, 6: rear TTL input 1, 2
  - 7: compare register

* DataID

  DataID characterizing the data source.

  Data taking was initiated by the trigger source identified in TrigID, at the
  time “header timestamp + event timestamp”

  - 0 … 3: Monitor / Chopper input 1 … 4
  - 4, 5: rear TTL input 1, 2
  - 6, 7: ADC 1, 2

* Data

  Counter, Timer or ADC value of the identified data source

  21 bit (depending on source not necessarily all valid)

* Timestamp

  timing offset to the corresponding header timestamp

  event time = header timestamp + event timestamp

  19 bit
