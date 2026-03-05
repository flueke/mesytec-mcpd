#!/usr/bin/env python3

from PySide6.QtGui import QCloseEvent
import boost_histogram as bh
import logging
import mesytec_mcpd as mcpd
import sys

import pyqtgraph as pg
from pyqtgraph.Qt import QtCore, QtWidgets
from pyqtgraph.Qt.QtCore import Signal, Slot
from pyqtgraph.dockarea.Dock import Dock
from pyqtgraph.dockarea.DockArea import DockArea
import pyqtgraph.parametertree.parameterTypes as pTypes
from pyqtgraph.parametertree import Parameter, ParameterTree
from rich.logging import RichHandler

logging.basicConfig(
    level="NOTSET", format="%(name)s %(message)s", datefmt="[%X]", handlers=[RichHandler()]
)

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
        logging.debug(f"ReadoutWorker: calling readout.start(), thread={QtCore.QThread.currentThread()}")
        self.readout.start()
        logging.debug(f"ReadoutWorker: emitting started() signal")
        self.started.emit()
        logging.debug("ReadoutWorker: entering readout loop")
        while self.running:
            logging.warning("ping")

            if self.readout.has_readout_exception():
                logging.error(f"ReadoutWorker: exception in readout thread, stopping readout (e={self.readout.get_readout_exception()})")
                break
            else:
                logging.debug("ReadoutWorker: no exception in readout thread")

            packets = self.readout.get_packets()

            if packets:
                logging.debug(f"ReadoutWorker: got {len(packets)} packets")
                self.new_packets.emit(packets)
            else:
                logging.debug("ReadoutWorker: no packets received, sleeping briefly")
                QtCore.QThread.msleep(10)

        logging.debug("ReadoutWorker: left loop, stopping readout")
        self.readout.stop()
        logging.debug("ReadoutWorker: readout stopped, emitting stopped signal")
        self.stopped.emit()

    @Slot()
    def stop(self):
        logging.debug("ReadoutWorker: stop requested")
        self.running = False

class PacketProcessor(QtCore.QObject):
    def __init__(self):
        super().__init__()

    @Slot()
    def process_packets(self, packets: list[mcpd.DataPacket]):
        for packet in packets:
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
        layout.addWidget(self.start_button)
        layout.addWidget(self.stop_button)
        layout.addWidget(self.label_status)
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
        self.dock_readout_stats = Dock("Readout Stats")
        self.dock_histograms = Dock("Histograms")
        self.dock_logWindow = Dock("Log Window")
        self.dock_plots = Dock("Plot")

        self.dockArea.addDock(self.dock_readout_control, "left")
        self.dockArea.addDock(self.dock_readout_stats, "bottom")
        self.dockArea.addDock(self.dock_histograms, "right", self.dock_readout_control)
        self.dockArea.addDock(self.dock_logWindow, "right", self.dock_readout_stats)
        self.dockArea.addDock(self.dock_plots, "right")

        self.readout_control_widget = ReadoutControlWidget()
        self.dock_readout_control.addWidget(self.readout_control_widget)
        self.readout_control_widget.start.connect(self.start_readout)
        self.readout_control_widget.stop.connect(self.stop_readout)

        logging.debug(f"MainWindow thread={QtCore.QThread.currentThread()}")

        return

        # Create plots
        self.amp_plot = pg.PlotWidget(title="Amplitude Histogram")
        self.pos_plot = pg.PlotWidget(title="Position Histogram")
        self.rate_plot = pg.PlotWidget(title="Rate Plot")

        # Set up histograms
        self.amp_hist = pg.BarGraphItem(x=[], height=[], width=0.8, brush='r')
        self.pos_hist = pg.BarGraphItem(x=[], height=[], width=0.8, brush='b')
        self.rate_curve = self.rate_plot.plot([], [], pen='g')

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
        self.packet_processor = PacketProcessor()
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
    def handle_packets(self, packets: list[mcpd.DataPacket]):
        logging.info(f"Received {len(packets)} packets")

    @Slot()
    def start_readout(self):
        if not self.readout_thread.isRunning():
            self.readout_thread.start()

    @Slot()
    def stop_readout(self):
        if self.readout_thread.isRunning():
            #logging.debug("MainWindow: stop_readout called. stopping readout worker")
            self.readout_worker.stop()
            #logging.debug("MainWindow: stop_readout called. stopping readout thread")
            self.readout_thread.quit()
            #logging.debug("MainWindow: stop_readout called. waiting for readout thread to finish")
            self.readout_thread.wait()
            #logging.debug("MainWindow: stop_readout called. readout thread stopped")

    def closeEvent(self, event: QCloseEvent) -> None:
        logging.debug("MainWindow: closeEvent called, stopping readout thread if running")
        self.stop_readout()
        super().closeEvent(event)

if __name__ == "__main__":
    mcpd.init()
    mcpd.set_log_level("trace")

    app = QtWidgets.QApplication([])
    mainwin = MainWindow(mcpd.Readout())
    mainwin.show()

    # Start the Qt event loop
    ret = app.exec()
    logging.debug(f"Qt event loop exited with code {ret}")
    sys.exit(ret)
