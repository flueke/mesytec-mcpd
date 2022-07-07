#ifndef __MESYTEC_MCPD_UDP_GUI_H__
#define __MESYTEC_MCPD_UDP_GUI_H__

#include <QLabel>
#include <QStatusBar>
#include <QSyntaxHighlighter>
#include <QToolBar>
#include <QWidget>

#include "code_editor.h"

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

#endif /* __MESYTEC_MCPD_UDP_GUI_H__ */
