import mesytec_mcpd as mcpd
import time
mcpd.set_log_level("trace")
rdo = mcpd.Readout()
rdo.start()
time.sleep(1)
# For a clean shutdown either rdo.stop() is needed or the object has to be deleted.
del rdo
#rdo.stop()
