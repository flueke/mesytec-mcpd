# Changelog for mesytec-mcpd

## v0.9-rc

- MDLL: fix swapped x/y position fields when decoding neutron events. Decoded
  data and histograms from earlier versions have x and y swapped.

- MDLL: `mdll_set_spectrum()` now takes 16 bit shift and offset values for the
  upcoming FW0106. Incompatible with MDLL_v1 and MDLL_v2 < FW0106.

- New readout and processing core shared by mcpd-cli and the python bindings:
  `Daq` class (readout/replay, listfile writing, per source stats, MDLL and
  MCPD histograms) and a `PacketConsumer` interface. mcpd-cli was refactored to
  use it.

- New python GUI `mesytec-mpsd-gui` (install with `mesytec-mcpd[gui]`):
  - run control, listfile writing and replay
  - per source stats table and histogram views
  - device parameter tree with a text filter
  - embedded python console, GUI state follows DAQ changes made from it
  - setup save/load, restores the last used setup and the UI layout on start

- New python port of mcpd-cli: `mcpd-cli-py`.

- Python packaging:
  - numpy and click are now regular dependencies, the `[cli]` extra is gone
  - version scheme changed to `no-guess-dev`, versions map directly to git commits
  - wheels are built via cibuildwheel, Windows wheel fixes

## v0.8

- Improved mcpd-cli: more stats, better reporting, improved error handling

- Improved python bindings: all core mcpd/mdll/mpsd functions are now also available from python.


## v0.7

- Fix the GetVersion command for MDLL-v1: the response is too short. Fix is to
  accept the short response and leave the FPGA firmware field set to 0. There
  won't be a firmware fix as MDLL-v1 is not supported anymore and it's a minor
  issue.

- Some work done on python binding code: there's a standalone mesytec_mcpd python
  module and mcpd-cli can embed the python interpreter and execute user defined
  script in the 'replay' and 'readout' commands.

## v0.6

- mcpd-cli: improve root support and listfile handling
  - add support for mdll root histograms
  - flush the root file periodically
  - add --overwrite-listfile option
