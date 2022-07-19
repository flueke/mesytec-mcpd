#include <mesytec-mcpd/mesytec-mcpd.h>

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <Qt>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QThread>
#include <spdlog/sinks/dup_filter_sink.h>
#include <spdlog/sinks/qt_sinks.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <future>
#include <system_error>

#include "util/qt_logview.h"
#include "util/qt_str.h"
#include "util/qt_util.h"
#include "util/mesytec_spdlog_util.h"
#include "ui_mainwindow.h"
#include "mcpd_udp_gui.h"

using namespace mesytec::mcpd;

static const auto DefaultEditorContents =
R"(
# header
11			# buffer length in 16 bit words
0x0			# buffer type
10			# header length
1234		# buffer number
0			# cmd id
0x0			# mcpd id | status
0x0			# header timestamp lo
0x0			# header timestamp mid
0x0			# header timestamp hi
0x0			# checksum
# data
0xffff
)";

static const char *DefaultCommandHost = "mdll-0012";
static const int DefaultCommandPort = 32768;
static const int DefaultDataPort = 32769;

template<typename View>
void log_buffer(const std::shared_ptr<spdlog::logger> &logger,
                const spdlog::level::level_enum &level,
                const View &buffer,
                const std::string &header,
                const char *valueFormat = "  0x{:004X}"
                )
{
    if (!logger->should_log(level))
        return;

    logger->log(level, "begin buffer '{}' (size={})", header, buffer.size());

    for (const auto &value: buffer)
        logger->log(level, valueFormat, value);

    logger->log(level, "end buffer '{}' (size={})", header, buffer.size());
}

template<typename View, int Columns = 8>
void log_mcpd_buffer(const std::shared_ptr<spdlog::logger> &logger,
                     const spdlog::level::level_enum &level,
                     const View &buffer, const std::string &header,
                     const char *valueFormat = "0x{:004X}")
{
    if (!logger->should_log(level))
        return;

    logger->log(level, "begin buffer '{}' (size={})", header, buffer.size());

    int col = 0;
    auto it = std::begin(buffer);

    while (it != std::end(buffer))
    {
        std::string line = "  ";

        for (int col=0; (col<Columns || Columns == 0) && it != std::end(buffer); ++col, ++it)
        {
            line += fmt::format(valueFormat, *it) + ", ";
        }

        logger->log(level, line);
    }

    logger->log(level, "end buffer '{}' (size={})", header, buffer.size());
}

struct ConnectionInfo
{
    // cmd host and dest port. Not bound to a specific local port.
    QString cmdHost;
    u16 cmdPort;
    u16 dataPort; // bound local port to receive data on

    bool operator==(const ConnectionInfo &o)
    {
        return cmdHost == o.cmdHost
            && cmdPort == o.cmdPort
            && dataPort == o.dataPort;
    }

    bool operator!=(const ConnectionInfo &o)
    {
        return !(*this == o);
    }
};

struct McpdSockets
{
    int cmdSock = -1;
    int dataSock = -1;
};

struct LogViewAndControls
{
    QPlainTextEdit *logview;
    QCheckBox *cb_poll;
    QCheckBox *cb_logRawPackets;
    QCheckBox *cb_decodePackets;
};

struct Context
{
    ConnectionInfo conInfo; // current connection info
    McpdSockets sockets; // sockets setup using conInfo
    std::atomic<bool> socketsBusy;

    QString scriptsDir;
    Ui::MainWindow *mainUi;
    LogViewAndControls lvcCmd = {};
    LogViewAndControls lvcData = {};
};

ConnectionInfo get_connection_info(Ui::MainWindow *mainUi)
{
    ConnectionInfo result = {};
    result.cmdHost = mainUi->le_cmdHost->text();
    result.cmdPort = mainUi->spin_cmdPort->value();
    result.dataPort = mainUi->spin_dataPort->value();
    return result;
}

std::pair<McpdSockets, std::error_code> make_sockets(const ConnectionInfo &conInfo)
{
    McpdSockets sockets{};
    std::error_code ec;

    sockets.cmdSock = connect_udp_socket(conInfo.cmdHost.toStdString(), conInfo.cmdPort, &ec);

    if (!ec)
        sockets.dataSock = bind_udp_socket(conInfo.dataPort, &ec);

    return std::make_pair(sockets, ec);
}

void close_sockets(McpdSockets &sockets)
{
    if (sockets.cmdSock >= 0)
        close_socket(sockets.cmdSock);

    if (sockets.dataSock >= 0)
        close_socket(sockets.dataSock);

    sockets = {};
}

std::error_code update_connection(Context &context)
{
    auto newInfo = get_connection_info(context.mainUi);

    if (newInfo != context.conInfo)
    {
        close_sockets(context.sockets);
        context.conInfo = {};

        std::error_code ec;
        std::tie(context.sockets, ec) = make_sockets(newInfo);

        if (!ec)
            context.conInfo = newInfo;

        return ec;
    }

    return {};
}

// Strips the part of the input line that's inside a multiline comment.
// If the whole line is inside the return value will be empty.
QString handle_multiline_comment(QString line, bool &in_multiline_comment)
{
    QString result;
    result.reserve(line.size());

    s32 i = 0;

    while (i < line.size())
    {
        auto mid_ref = line.midRef(i, 2);

        if (in_multiline_comment)
        {
            if (mid_ref == "*/")
            {
                in_multiline_comment = false;
                i += 2;
            }
            else
            {
                ++i;
            }
        }
        else
        {
            if (mid_ref == "/*")
            {
                in_multiline_comment = true;
                i += 2;
            }
            else
            {
                result.append(line.at(i));
                ++i;
            }
        }
    }

    return result;
}

struct PreparsedLine
{
    QStringList parts;
    QString trimmed;
    u32 lineNumber;
};

// Get rid of comment parts and empty lines and split each of the remaining
// lines into space separated parts while keeping track of the correct input
// line numbers.
QVector<PreparsedLine> pre_parse(QTextStream &input)
{
    static const QRegularExpression reWordSplit("\\s+");

    QVector<PreparsedLine> result;
    u32 lineNumber = 0;
    bool in_multiline_comment = false;

    while (true)
    {
        auto line = input.readLine();
        ++lineNumber;

        if (line.isNull())
            break;

        line = handle_multiline_comment(line, in_multiline_comment);

        int startOfComment = line.indexOf('#');

        if (startOfComment >= 0)
            line.resize(startOfComment);

        line = line.trimmed();

        if (line.isEmpty())
            continue;

        auto parts = line.split(reWordSplit, QString::SkipEmptyParts);

        if (parts.isEmpty())
            continue;

        parts[0] = parts[0].toLower();

        result.push_back({ parts, line, lineNumber });
    }

    return result;
}

std::vector<u16> parse_packet_text(QTextStream &input)
{
    std::vector<u16> result;
    QVector<PreparsedLine> splitLines = pre_parse(input);

    int lineIndex = 0;

    while (lineIndex < splitLines.size())
    {
        auto &sl = splitLines.at(lineIndex);

        assert(!sl.parts.isEmpty());

        bool ok = true;
        u16 word = sl.parts[0].toUShort(&ok, 0);

        if (!ok)
            throw std::runtime_error(fmt::format("Error parsing 16 bit number from '{}' (line {})",
                                                 sl.trimmed.toStdString(), sl.lineNumber));

        result.emplace_back(word);
        ++lineIndex;
    }

    return result;
}

std::vector<u16> parse_packet_text(const QString &packetText)
{
    QTextStream stream(const_cast<QString *>(&packetText), QIODevice::ReadOnly);
    return parse_packet_text(stream);
#if 0
    std::vector<u16> result;

    for (auto line: packetText.split('\n', QString::SkipEmptyParts))
    {
        line = line.trimmed();

        if (line.isEmpty() || line.startsWith("#"))
            continue;

        bool ok = true;
        u16 word = line.toUShort(&ok, 0);

        if (!ok)
            throw std::runtime_error(fmt::format("Error parsing 16 bit number from '{}'", line.toStdString()));

        result.emplace_back(word);
    }

    return result;
#endif
}


bool gui_write_string_to_file(const QString &text, const QString &savePath)
{
    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(
            nullptr, "File save error",
            QSL("Error opening \"%1\" for writing").arg(savePath));
        return false;
    }

    QTextStream stream(&file);
    stream << text;

    if (stream.status() != QTextStream::Ok)
    {
        QMessageBox::critical(
            nullptr, "File save error",
            QSL("Error writing to \"%1\"").arg(savePath));
        return false;
    }

    return true;
}

void editor_save_packet_as(PacketEditor *editor)
{
    auto savePath = QFileDialog::getSaveFileName(
        nullptr, QSL("Save packet to file"), editor->getFilepath(),
        QSL("MCPD packets (*.mcpdpacket);; All Files (*)"));

    if (savePath.isEmpty())
        return;

    QFileInfo fi(savePath);

    if (fi.completeSuffix().isEmpty())
        savePath += ".mcpdpacket";

    if (gui_write_string_to_file(editor->getText(), savePath))
    {
        editor->setFilepath(savePath);
        editor->setModified(false);
    }
}

void editor_save_packet(PacketEditor *editor)
{
    auto filepath = editor->getFilepath();

    QFileInfo fi(filepath);

    if (fi.isDir() || filepath.isEmpty())
    {
        editor_save_packet_as(editor);
        return;
    }

    if (gui_write_string_to_file(editor->getText(), filepath))
    {
        editor->setModified(false);
    }
}

int main(int argc, char *argv[])
{
    qRegisterMetaType<std::vector<u16>>("std::vector<u16>");
    qRegisterMetaType<std::error_code>("std::error_code");

    Q_INIT_RESOURCE(resources);

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationDomain("www.mesytec.com");
    QCoreApplication::setOrganizationName("mesytec");
    QCoreApplication::setApplicationName("mcpd-udp-dev-gui");
    QCoreApplication::setApplicationVersion(mesytec::mcpd::GIT_VERSION);
    QLocale::setDefault(QLocale::c());

    Context context{};
    context.socketsBusy = false;

    QThread socketThread;
    McpdSocketHandler socketHandler;
    socketHandler.moveToThread(&socketThread);

    auto contextP = &context;
    auto socketHandlerP = &socketHandler;

    QSettings settings;

    // scripts directory
    context.scriptsDir = settings.value(
        "ScriptsDirectory",
        QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0)
        + "/mcpd-udp-dev-gui").toString();

    if (!QDir().mkpath(context.scriptsDir))
    {
        QMessageBox::critical(nullptr, "Error", QSL("Error creating scripts directory '%1'.").arg(context.scriptsDir));
        return 1;
    }

    MainWindow mainWin;
    mainWin.setWindowTitle("mesytec usb gui");
    auto mainUi = std::make_unique<Ui::MainWindow>();
    mainUi->setupUi(&mainWin);
    context.mainUi = mainUi.get();

    // gui: connection info
    mainUi->le_cmdHost->setText(settings.value("CommandHost", DefaultCommandHost).toString());
    mainUi->spin_cmdPort->setValue(settings.value("CommandPort", DefaultCommandPort).toInt());
    mainUi->spin_dataPort->setValue(settings.value("DataPort", DefaultDataPort).toInt());

    // gui: log views and poll checkboxes for cmd and data ports
    {
        context.lvcCmd.logview = make_logview().release();
        context.lvcCmd.cb_poll = new QCheckBox("Poll cmd socket");
        context.lvcCmd.cb_logRawPackets = new QCheckBox("Log raw packet data");
        context.lvcCmd.cb_decodePackets = new QCheckBox("Decode packets");

        context.lvcCmd.cb_logRawPackets->setChecked(true);
        context.lvcCmd.cb_decodePackets->setChecked(true);

        auto l_cmd = make_vbox();
        l_cmd->addWidget(context.lvcCmd.cb_poll);
        l_cmd->addWidget(context.lvcCmd.cb_logRawPackets);
        l_cmd->addWidget(context.lvcCmd.cb_decodePackets);
        l_cmd->addWidget(context.lvcCmd.logview);
        l_cmd->setStretch(0, 1);
        mainUi->gb_cmd->setLayout(l_cmd);
    }

    {
        context.lvcData.logview = make_logview().release();
        context.lvcData.cb_poll = new QCheckBox("Poll data socket");
        auto l_data = make_vbox();
        l_data->addWidget(context.lvcData.cb_poll);
        l_data->addWidget(context.lvcData.logview);
        l_data->setStretch(0, 1);
        mainUi->gb_data->setLayout(l_data);
    }

    // quit
    QObject::connect(mainUi->action_Quit, &QAction::triggered, [&] ()
                     {
                        app.quit();
                     });

    // logging setup
    spdlog::set_level(spdlog::level::info);

    auto cmdLogViewWrapper = std::make_unique<LogViewWrapper>(context.lvcCmd.logview);
    auto dataLogViewWrapper = std::make_unique<LogViewWrapper>(context.lvcData.logview);

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto cmdQtSink = std::make_shared<mesytec::spdlog_util::QtSink_mt>(cmdLogViewWrapper.get(), "logMessage");
    auto dataQtSink = std::make_shared<mesytec::spdlog_util::QtSink_mt>(dataLogViewWrapper.get(), "logMessage");

#if 0
    auto dupfilter = std::make_shared<spdlog::sinks::dup_filter_sink_mt>(std::chrono::seconds(2));
    dupfilter->add_sink(consolesink);
    dupfilter->add_sink(qtsink);
#endif

    // Create and set the default logger. Output goes to console and the cmd log view
    {
        auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list({ consoleSink, cmdQtSink }));
        spdlog::set_pattern("%H:%M:%S.%e %^%l%$: %v");
        spdlog::set_default_logger(logger);
    }

    // Setup a logger for the cmd logview
    {
        auto logger = std::make_shared<spdlog::logger>("cmd", spdlog::sinks_init_list({ cmdQtSink }));
        logger->set_pattern("%H:%M:%S.%e %^%l%$: %v");
        spdlog::register_logger(logger);
    }

    // Setup a logger for the data logview
    {
        auto logger = std::make_shared<spdlog::logger>("data", spdlog::sinks_init_list({ dataQtSink }));
        logger->set_pattern("%H:%M:%S.%e %^%l%$: %v");
        spdlog::register_logger(logger);
    }

    mainWin.show();
    mainWin.restoreGeometry(settings.value("MainWindowGeometry").toByteArray());
    mainWin.restoreState(settings.value("MainWindowState").toByteArray());

    // set equal splitter part sizes
    auto splitterWidth = mainUi->splitter->width();
    int partSize = splitterWidth / 3.0;
    mainUi->splitter->setSizes({ partSize, partSize, partSize });

    // restore saved splitter state
    mainUi->splitter->restoreState(settings.value("MainWindowSplitter").toByteArray());

    WidgetGeometrySaver geoSaver;

    //
    // scripts table view / filesystem model / file handling
    //
    static const QStringList PacketFilesNameFilters = { QSL("*.mcpdpacket") };
    auto filesModel = std::make_unique<QFileSystemModel>();
    filesModel->setFilter(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);
    filesModel->setNameFilters(PacketFilesNameFilters);
    filesModel->setNameFilterDisables(false);
    mainUi->filesView->setModel(filesModel.get());
    mainUi->filesView->verticalHeader()->hide();
    mainUi->filesView->hideColumn(2); // Hides the file type column
    mainUi->filesView->setSortingEnabled(true);

    QObject::connect(filesModel.get(), &QFileSystemModel::directoryLoaded,
                     mainUi->filesView, [&] (const QString &) {
        mainUi->filesView->resizeColumnsToContents();
        mainUi->filesView->resizeRowsToContents();
    });

    auto on_files_dir_changed = [&] (const QString &newPath)
    {
        mainUi->le_packetsDir->setText(newPath);
        // Set an empty path before the real path to force a refresh of the model.
        filesModel->setRootPath(QString());
        filesModel->setRootPath(newPath);
        mainUi->filesView->setRootIndex(filesModel->index(newPath));
    };

    auto on_remove_file_clicked = [&] ()
    {
        // TODO: close open editors.
        auto selectedIndex = mainUi->filesView->currentIndex();
        auto path = filesModel->filePath(selectedIndex);
        if (!filesModel->remove(selectedIndex)) // this deletes the file
            QMessageBox::critical(nullptr, "Error", QSL("Error removing file '%1'").arg(path));
    };

    auto on_browse_scriptsdir_clicked = [&] ()
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(context.scriptsDir));
    };

    //
    // Packet text editor creation and interactions
    //
    auto create_new_editor = [&geoSaver, &mainWin, &context, contextP, socketHandlerP] ()
    {
        auto editor = new PacketEditor;
        editor->setAttribute(Qt::WA_DeleteOnClose, true);
        editor->setFilepath(context.scriptsDir);
        editor->setText(DefaultEditorContents);
        editor->show();
        add_widget_close_action(editor);
        geoSaver.addAndRestore(editor, QSL("WindowGeometries/PacketEditor"));

        auto timer = new QTimer(editor);

        auto manual_cmd_transaction = [editor, contextP, socketHandlerP] ()
        {
            try
            {
                auto text = editor->getText();
                auto requestData = parse_packet_text(text);

                if (auto ec = update_connection(*contextP))
                {
                    spdlog::error("Error connecting mcpd sockets: {}", ec.message());
                    return;
                }

                QMetaObject::invokeMethod(
                    socketHandlerP, [contextP, socketHandlerP, requestData] ()
                    {
                        socketHandlerP->setSockets(
                            contextP->sockets.cmdSock,
                            contextP->sockets.dataSock);

                        bool expected = false;

                        if (!contextP->socketsBusy.compare_exchange_strong(expected, true))
                            return;

                        socketHandlerP->cmdTransaction(requestData);

                        contextP->socketsBusy = false;
                    });
            }
            catch (const std::runtime_error &e)
            {
                spdlog::error("{}", e.what());
            }
        };

        auto periodic_cmd_transaction = [=] ()
        {
            if (!contextP->socketsBusy)
            {
                manual_cmd_transaction();
            }
        };


        QObject::connect(editor, &PacketEditor::sendPacket, &mainWin, manual_cmd_transaction);
        QObject::connect(timer, &QTimer::timeout, &mainWin, periodic_cmd_transaction);

        QObject::connect(
            editor, &PacketEditor::sendRepeatChanged,
            &mainWin, [=] (bool doRepeat, int interval_ms)
            {
                if (doRepeat)
                    timer->start(interval_ms);
                else
                    timer->stop();
            });

        QObject::connect(
            editor, &PacketEditor::save,
            &mainWin, [editor] () { editor_save_packet(editor); });

        QObject::connect(
            editor, &PacketEditor::saveAs,
            &mainWin, [editor] () { editor_save_packet_as(editor); });

        return editor;
    };

    auto on_file_double_clicked = [&] (const QModelIndex &idx)
    {
        // TODO (maybe): check if the script is open in a editor. if so raise
        // the editor.  otherwise create a new editor for the script.
        auto filePath = filesModel->filePath(idx);

        if (filePath.isEmpty())
            return;
        QFile file(filePath);

        if (!file.open(QIODevice::ReadOnly))
            return;

        auto editor = create_new_editor();

        QTextStream stream(&file);
        editor->setText(stream.readAll());
        editor->setFilepath(filePath);
    };

    QObject::connect(mainUi->pb_browsePacketsDir, &QPushButton::clicked, &mainWin, on_browse_scriptsdir_clicked);
    QObject::connect(mainUi->pb_newPacket, &QPushButton::clicked, &mainWin, create_new_editor);
    QObject::connect(mainUi->pb_removePacket, &QPushButton::clicked, &mainWin, on_remove_file_clicked);
    QObject::connect(mainUi->filesView, &QTableView::doubleClicked, &mainWin, on_file_double_clicked);

    // cmd and data pipe socket polling
    QTimer socketPollTimer;
    socketPollTimer.setInterval(500);
    socketPollTimer.start();

    QObject::connect(&socketPollTimer, &QTimer::timeout,
                     &mainWin, [&] () {

                         if (context.lvcCmd.cb_poll->isChecked()
                             || context.lvcData.cb_poll->isChecked())
                         {
                             if (auto ec = update_connection(context))
                             {
                                 spdlog::error("Error connecting mcpd sockets: {}", ec.message());
                                 return;
                             }

                             bool pollCmd = context.lvcCmd.cb_poll->isChecked();
                             bool pollData = context.lvcData.cb_poll->isChecked();

                             if (pollCmd && !contextP->socketsBusy)
                             {
                                 QMetaObject::invokeMethod(
                                     &socketHandler, [contextP, socketHandlerP, pollCmd] ()
                                     {
                                         socketHandlerP->setSockets(
                                             contextP->sockets.cmdSock,
                                             contextP->sockets.dataSock);

                                         bool expected = false;

                                         if (pollCmd && contextP->socketsBusy.compare_exchange_strong(expected, true))
                                         {
                                             socketHandlerP->pollCmd();
                                             contextP->socketsBusy = false;
                                         }
                                     }, Qt::QueuedConnection);
                             }

                             if (pollData && !contextP->socketsBusy)
                             {
                                 QMetaObject::invokeMethod(
                                     &socketHandler, [contextP, socketHandlerP, pollData] ()
                                     {
                                         socketHandlerP->setSockets(
                                             contextP->sockets.cmdSock,
                                             contextP->sockets.dataSock);

                                         bool expected = false;

                                         if (pollData && contextP->socketsBusy.compare_exchange_strong(expected, true))
                                         {
                                             socketHandlerP->pollData();
                                             contextP->socketsBusy = false;
                                         }
                                     }, Qt::QueuedConnection);
                             }
                         }
                     });

    QObject::connect(&socketHandler, &McpdSocketHandler::cmdTransactionComplete,
                     &mainWin, [&] (const std::vector<u16> &request,
                                    const std::vector<u16> &response)
                     {
                         auto logger = spdlog::get("cmd");
                         logger->info(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> begin cmd transaction");

                         // request
                         if (context.lvcCmd.cb_logRawPackets->isChecked())
                         {
                             log_mcpd_buffer(logger, spdlog::level::info, request,
                                             fmt::format("request (size={})", request.size()));
                         }

                         if (context.lvcCmd.cb_decodePackets->isChecked())
                         {
                             auto requestPacket = command_packet_from_data(request);
                             std::stringstream ss;
                             format(ss, requestPacket, true);
                             logger->info("decoded request packet:\n{}\n", ss.str());
                         }

                         // response

                         if (context.lvcCmd.cb_logRawPackets->isChecked()
                             || context.lvcCmd.cb_decodePackets->isChecked())
                             logger->info("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< cmd response");


                         if (context.lvcCmd.cb_logRawPackets->isChecked())
                         {
                             log_mcpd_buffer(logger, spdlog::level::info, response,
                                             fmt::format("response (size={})", response.size()));
                         }

                         if (context.lvcCmd.cb_decodePackets->isChecked())
                         {
                             auto responsePacket = command_packet_from_data(response);
                             std::stringstream ss;
                             format(ss, responsePacket, true);
                             logger->info("decoded response packet:\n{}\n", ss.str());

                             size_t responseBytes = response.size() * sizeof(response[0]);
                             size_t actualDataWords = responseBytes / sizeof(u16) - CommandPacketHeaderWords;
                             size_t headerDataLen = get_data_length(responsePacket);

                             if (headerDataLen != actualDataWords)
                             {
                                 logger->warn("calculated data words from MCPD packet header = {}, actual data words from UDP packet = {}",
                                              headerDataLen, actualDataWords);
                             }

                         }

                         logger->info("======================================== end cmd transaction");
                     });

    QObject::connect(&socketHandler, &McpdSocketHandler::cmdPacketSent,
                     &mainWin, [&] (const std::vector<u16> &request)
                     {
                     });

    QObject::connect(&socketHandler, &McpdSocketHandler::cmdPacketReceived,
                     &mainWin, [&] (const std::vector<u16> &data)
                     {
                         auto logger = spdlog::get("cmd");
                         logger->info("cmd packet received, size={}", data.size());
                         log_mcpd_buffer(logger, spdlog::level::info, data, "cmd packet");
                     });

    QObject::connect(&socketHandler, &McpdSocketHandler::cmdError,
                     &mainWin, [&] (const std::error_code &ec)
                     {
                         // Ignore socket read timeouts under linux
                         //if (ec == std::errc::resource_unavailable_try_again)
                         //    return;

                         auto logger = spdlog::get("cmd");
                         logger->error(ec.message());
                     });

    QObject::connect(&socketHandler, &McpdSocketHandler::dataPacketReceived,
                     &mainWin, [&] (const std::vector<u16> &data)
                     {
                         auto logger = spdlog::get("data");
                         logger->info("data packet received, size={}", data.size());
                         log_mcpd_buffer(logger, spdlog::level::info, data, "data packet");
                     });

    QObject::connect(&socketHandler, &McpdSocketHandler::dataError,
                     &mainWin, [&] (const std::error_code &ec)
                     {
                         if (ec == std::errc::resource_unavailable_try_again)
                             return;

                         auto logger = spdlog::get("data");
                         logger->error(ec.message());
                     });

    socketThread.start();

    on_files_dir_changed(context.scriptsDir);

    QTimer filesDirUpdateTimer;
    QObject::connect(&filesDirUpdateTimer, &QTimer::timeout,
                     &mainWin, [&] () { on_files_dir_changed(context.scriptsDir); });
    filesDirUpdateTimer.setInterval(1000);
    filesDirUpdateTimer.start();

    auto ret = app.exec();

    socketThread.exit();
    socketThread.wait();

    settings.setValue("CommandHost", mainUi->le_cmdHost->text());
    settings.setValue("CommandPort", mainUi->spin_cmdPort->value());
    settings.setValue("DataPort", mainUi->spin_dataPort->value());
    settings.setValue("MainWindowGeometry", mainWin.saveGeometry());
    settings.setValue("MainWindowState", mainWin.saveState());
    settings.setValue("MainWindowSplitter", mainUi->splitter->saveState());

    return ret;
}
