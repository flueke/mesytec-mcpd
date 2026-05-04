# Changelog for mesytec-mcpd

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
