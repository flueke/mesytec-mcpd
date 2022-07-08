#ifndef __MESYTEC_MCPD_UDP_GUI_H__
#define __MESYTEC_MCPD_UDP_GUI_H__

#include <QLabel>
#include <QStatusBar>
#include <QSyntaxHighlighter>
#include <QToolBar>
#include <QWidget>
#include <QDebug>

#include <mesytec-mcpd/mesytec-mcpd.h>
#include "code_editor.h"
#include "util/int_types.h"

class PacketEditor: public QWidget
{
    Q_OBJECT
    signals:
        void sendPacket();
        void sendRepeatChanged(bool repeat, int interval_ms);
        void save();
        void saveAs();

    public:
        PacketEditor(QWidget *parent = nullptr);
        ~PacketEditor();

        QString getText() const
        {
            return m_editor->toPlainText();
        }

        QString getFilepath() const
        {
            return m_filepath;
        }

        bool isModified() const
        {
            return m_editor->document()->isModified();
        }

        void setModified(bool b)
        {
            m_editor->document()->setModified(b);
            updateWindowTitle();
        }

        CodeEditor *getCodeEditor() const { return m_editor; }

    public slots:
        void setText(const QString &contents);
        void setFilepath(const QString &path);

    private:
        CodeEditor *m_editor;
        QToolBar *m_toolBar;
        QStatusBar *m_statusBar;
        QLabel *m_labelPosition;
        QString m_filepath;

        void updateWindowTitle();
};

class McpdSocketHandler: public QObject
{
    Q_OBJECT
    signals:
        void cmdTransactionComplete(
            const std::vector<u16> &request,
            const std::vector<u16> &response);

        void cmdPacketReceived(const std::vector<u16> &data);
        void cmdError(const std::error_code &ec);

        void dataPacketReceived(const std::vector<u16> &data);
        void dataError(const std::error_code &ec);

    public:
        static const int PacketReceiveTimeout_ms = 100;

        explicit McpdSocketHandler(QObject *parent = nullptr)
            : QObject(parent)
        {}


    public slots:
        void setSockets(int cmdSock, int dataSock)
        {
            cmdSock_ = cmdSock;
            dataSock_ = dataSock;
        }

        void cmdTransaction(const std::vector<u16> &data)
        {
            size_t bytesTransferred = 0;

            auto ec = mesytec::mcpd::write_to_socket(
                cmdSock_,
                reinterpret_cast<const u8 *>(data.data()),
                data.size() / sizeof(data[0]),
                bytesTransferred);

            if (ec)
            {
                emit cmdError(ec);
            }
            else
            {
                std::vector<u16> dest;
                ec = receivePacket(cmdSock_, dest);

                if (!ec)
                {
                    //emit cmdPacketReceived(dest);
                    emit cmdTransactionComplete(data, dest);
                }
                else
                    emit cmdError(ec);
            }
        }

        void pollCmd()
        {
            std::vector<u16> dest;
            auto ec = receivePacket(cmdSock_, dest);

            if (!ec)
                emit cmdPacketReceived(dest);
            else
                emit cmdError(ec);
        }

        void pollData()
        {
            std::vector<u16> dest;
            auto ec = receivePacket(dataSock_, dest);

            if (!ec)
                emit dataPacketReceived(dest);
            else
                emit dataError(ec);
        }

    private:
        std::error_code receivePacket(int sock, std::vector<u16> &dest)
        {
            dest.resize(1500);
            size_t bytesTransferred = 0;

            auto ec = mesytec::mcpd::receive_one_packet(
                sock,
                reinterpret_cast<u8 *>(dest.data()), dest.size() * sizeof(u16),
                bytesTransferred,
                PacketReceiveTimeout_ms);

            dest.resize(bytesTransferred);
            return ec;
        }

        int cmdSock_ = -1;
        int dataSock_ = -1;
};

#endif /* __MESYTEC_MCPD_UDP_GUI_H__ */
