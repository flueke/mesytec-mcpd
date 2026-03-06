import logging
import boost_histogram as bh
import mesytec_mcpd_py as mcpd
import pyqtgraph as pg
import numpy as np
import sys

from pyqtgraph.Qt import QtCore, QtWidgets
from pyqtgraph.Qt.QtCore import Signal, Slot
from pyqtgraph.Qt.QtGui import QCloseEvent
from pyqtgraph.dockarea.Dock import Dock
from pyqtgraph.dockarea.DockArea import DockArea
import pyqtgraph.parametertree.parameterTypes as pTypes
from pyqtgraph.parametertree import Parameter, ParameterTree

from rich.logging import RichHandler
from time import perf_counter

# Taken from the pyqtgraph examples.utils file.
class FrameCounter(QtCore.QObject):
    sigFpsUpdate = QtCore.Signal(object)

    def __init__(self, interval=1000):
        super().__init__()
        self.count = 0
        self.last_update = 0
        self.interval = interval

    def update(self, count=1):
        self.count += count

        if self.last_update == 0:
            self.last_update = perf_counter()
            self.startTimer(self.interval)

    def timerEvent(self, evt):
        now = perf_counter()
        elapsed = now - self.last_update
        fps = self.count / elapsed
        self.last_update = now
        self.count = 0
        self.sigFpsUpdate.emit(fps)


class ReadoutWorker(QtCore.QObject):
    """
    Wraps the c++ mcpd.Readout object in a QObject to be used in a dedicated
    thread.  Polls for new packets in a tight loop and emits the 'new_packets'
    signal when new data is available. Also emits 'started' and 'stopped'
    signals when the readout is started and stopped.
    """

    new_packets = Signal(list)
    started = Signal()
    stopped = Signal()

    def __init__(self, readout: mcpd.Readout):
        super().__init__()
        self.readout = readout
        self.running = False

    @Slot()
    def run(self):
        self.running = True
        logging.debug(
            f"ReadoutWorker: calling readout.start(), thread={QtCore.QThread.currentThread()}"
        )
        self.readout.start()
        self.started.emit()

        logging.debug("ReadoutWorker: entering readout loop")

        while self.running:
            if self.readout.has_readout_exception():
                logging.error(
                    f"ReadoutWorker: exception in readout thread, stopping readout (e={self.readout.get_readout_exception()})"
                )
                break

            packets = self.readout.get_packets()

            if packets:
                logging.debug(f"ReadoutWorker: got {len(packets)} packets")
                self.new_packets.emit(packets)
            else:
                logging.debug("ReadoutWorker: no packets received, sleeping briefly")
                QtCore.QThread.msleep(10)

        logging.debug("ReadoutWorker: left loop, stopping readout")

        self.readout.stop()
        self.stopped.emit()

    @Slot()
    def stop(self):
        logging.debug("ReadoutWorker: stop requested")
        self.running = False


class PacketProcessor(QtCore.QObject):
    def __init__(self, readout_tree: ParameterTree):
        super().__init__()

        self.readout_tree = readout_tree
        self.device_params = dict() # device_id -> Parameter
        self.packetCounter = FrameCounter()
        self.eventCounter = FrameCounter()
        self.packetsPerSecond = 0
        self.eventsPerSecond = 0
        self.totalPackets = 0
        self.totalEvents = 0

        def update_packet_counter(fps):
            self.packetsPerSecond = fps

        def update_event_counter(fps):
            self.eventsPerSecond = fps

        self.packetCounter.sigFpsUpdate.connect(update_packet_counter)
        self.eventCounter.sigFpsUpdate.connect(update_event_counter)

    @Slot()
    def process_packets(self, packets: list[mcpd.DataPacket]):
        self.packetCounter.update(len(packets))
        for packet in packets:
            self.eventCounter.update(packet.event_count())

            if packet.device_id not in self.device_params:
                #logging.info(f"{packet.buffer_type=:#06x}, {packet.device_id=}, {packet.device_status=}")
                device_param = None
                if packet.buffer_type == 0x0001: # MCPD
                    device_param = Parameter.create(name=f"MCPD {packet.device_id}", type="group")
                    self.device_params[packet.device_id] = device_param
                    self.readout_tree.addParameters(device_param)
                elif packet.buffer_type == 0x0002: # MDLL
                    device_param = Parameter.create(name=f"MDLL {packet.device_id}", type="group")
                    self.device_params[packet.device_id] = device_param
                    self.readout_tree.addParameters(device_param)

                if device_param is not None:
                    device_param.addChild(Parameter.create(name="Histogram", type="bool", value=False))

            for event in packet.get_events():
                self.process_event(event)

    def process_event(self, event: mcpd.DecodedEvent):
        pass


class ReadoutControlWidget(QtWidgets.QWidget):
    start = Signal()
    stop = Signal()

    def __init__(self):
        super().__init__()
        layout = QtWidgets.QVBoxLayout()
        self.start_button = QtWidgets.QPushButton("Start Readout")
        self.stop_button = QtWidgets.QPushButton("Stop Readout")
        self.stop_button.setEnabled(False)
        self.label_status = QtWidgets.QLabel("Status: Stopped")
        self.label_stats = QtWidgets.QLabel("Stats: N/A")
        layout.addWidget(self.start_button)
        layout.addWidget(self.stop_button)
        layout.addWidget(self.label_status)
        layout.addWidget(self.label_stats)
        self.setLayout(layout)
        self.start_button.clicked.connect(self.start)
        self.stop_button.clicked.connect(self.stop)


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self, readout: mcpd.Readout):
        super().__init__()
        self.readout = readout
        self.setup_ui()
        self.setup_readout()

    def setup_ui(self):
        self.dockArea = DockArea()
        self.setCentralWidget(self.dockArea)
        self.setStatusBar(QtWidgets.QStatusBar())
        self.setMenuBar(QtWidgets.QMenuBar())

        menuFile = self.menuBar().addMenu("&File")
        menuFile.addAction("E&xit", self.close, QtCore.Qt.CTRL | QtCore.Qt.Key_Q)

        self.setWindowTitle("MPSD DAQ")
        self.resize(800, 600)

        self.dock_readout_control = Dock("Readout Control")
        self.dock_readout_devices = Dock("Readout Devices")
        self.dock_plots = Dock("Plot")

        self.dockArea.addDock(self.dock_readout_control, "left")
        self.dockArea.addDock(self.dock_readout_devices, "bottom", self.dock_readout_control)
        self.dockArea.addDock(self.dock_plots, "right")

        self.readout_control_widget = ReadoutControlWidget()
        self.dock_readout_control.addWidget(self.readout_control_widget)
        self.readout_control_widget.start.connect(self.start_readout)
        self.readout_control_widget.stop.connect(self.stop_readout)

        self.readout_root = Parameter.create(name="Readout Devices", type="group", children=[])
        self.readout_tree = ParameterTree()
        self.readout_tree.setParameters(self.readout_root, showTop=False)
        self.dock_readout_devices.addWidget(self.readout_tree)

        logging.debug(f"MainWindow thread={QtCore.QThread.currentThread()}")

        return

        # Create plots
        self.amp_plot = pg.PlotWidget(title="Amplitude Histogram")
        self.pos_plot = pg.PlotWidget(title="Position Histogram")
        self.rate_plot = pg.PlotWidget(title="Rate Plot")

        # Set up histograms
        self.amp_hist = pg.BarGraphItem(x=[], height=[], width=0.8, brush="r")
        self.pos_hist = pg.BarGraphItem(x=[], height=[], width=0.8, brush="b")
        self.rate_curve = self.rate_plot.plot([], [], pen="g")

        self.amp_plot.addItem(self.amp_hist)
        self.pos_plot.addItem(self.pos_hist)

        self.start_button = QtWidgets.QPushButton("Start")
        self.start_button.clicked.connect(self.start_readout)
        self.stop_button = QtWidgets.QPushButton("Stop")
        self.stop_button.clicked.connect(self.stop_readout)

        button_layout = QtWidgets.QHBoxLayout()
        button_layout.addWidget(self.start_button)
        button_layout.addWidget(self.stop_button)

        # Layout
        layout = QtWidgets.QVBoxLayout()
        layout.addWidget(self.amp_plot)
        layout.addWidget(self.pos_plot)
        layout.addWidget(self.rate_plot)
        layout.addLayout(button_layout)

        central_widget = QtWidgets.QWidget()
        central_widget.setLayout(layout)
        self.setCentralWidget(central_widget)

    def setup_readout(self):
        self.readout_worker = ReadoutWorker(self.readout)
        self.readout_thread = QtCore.QThread()
        self.readout_thread.setObjectName("ReadoutThread")
        self.readout_worker.moveToThread(self.readout_thread)
        self.readout_thread.started.connect(self.readout_worker.run)
        self.packet_processor = PacketProcessor(self.readout_tree)
        self.readout_worker.new_packets.connect(self.packet_processor.process_packets)

        def on_readout_started():
            self.statusBar().showMessage("Readout started")
            logging.info("Readout started")
            self.readout_control_widget.label_status.setText("Status: Running")
            self.readout_control_widget.start_button.setEnabled(False)
            self.readout_control_widget.stop_button.setEnabled(True)

        def on_readout_stopped():
            self.statusBar().showMessage("Readout stopped")
            logging.info("Readout stopped")
            self.readout_control_widget.label_status.setText("Status: Stopped")
            self.readout_control_widget.start_button.setEnabled(True)
            self.readout_control_widget.stop_button.setEnabled(False)

        self.readout_worker.started.connect(on_readout_started)
        self.readout_worker.stopped.connect(on_readout_stopped)

    @Slot()
    def start_readout(self):
        if not self.readout_thread.isRunning():
            self.readout_thread.start()

    @Slot()
    def stop_readout(self):
        if self.readout_thread.isRunning():
            self.readout_worker.stop()
            self.readout_thread.quit()
            self.readout_thread.wait()

    def closeEvent(self, event: QCloseEvent) -> None:
        logging.debug(
            "MainWindow: closeEvent called, stopping readout thread if running"
        )
        self.stop_readout()
        super().closeEvent(event)

    @Slot(object)
    def periodic_update(self):
        self.readout_control_widget.label_stats.setText(
            f"Packets/s: {self.packet_processor.packetsPerSecond:.2f}, Events/s: {self.packet_processor.eventsPerSecond:.2f}"
        )


def main():
    logging.basicConfig(
        level="INFO",
        format="%(name)s %(message)s",
        datefmt="[%X]",
        handlers=[RichHandler()],
    )

    mcpd.set_log_level("info")

    app = QtWidgets.QApplication([])
    mainwin = MainWindow(mcpd.Readout())
    mainwin.show()

    update_timer = QtCore.QTimer()
    update_timer.timeout.connect(mainwin.periodic_update)
    update_timer.start(500)

    ret = app.exec()
    logging.debug(f"Qt event loop exited with code {ret}")
    sys.exit(ret)


if __name__ == "__main__":
    main()
