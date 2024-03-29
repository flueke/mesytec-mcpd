#include "mcpd_udp_gui.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QSpinBox>

#include "util/qt_util.h"

PacketEditor::PacketEditor(QWidget *parent)
    : QWidget(parent)
{
    m_toolBar = make_toolbar();
    m_editor = new CodeEditor;
    m_statusBar = make_statusbar();

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(m_toolBar);
    layout->addWidget(m_editor);
    layout->addWidget(m_statusBar);

    //new SyntaxHighlighter(m_editor->document());

    auto on_editor_modification_changed = [this] (bool wasModified)
    {
        updateWindowTitle();
    };

    connect(m_editor, &QPlainTextEdit::modificationChanged,
            this, on_editor_modification_changed);

    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    m_toolBar->addAction(QIcon(":/icons/mesytec/media-playback-start.png"), QSL("Send"),
                         this,  &PacketEditor::sendPacket);

    auto cb_repeat = new QCheckBox("Repeat");
    auto spin_interval = new QSpinBox;
    spin_interval->setSuffix(" ms");
    spin_interval->setMinimum(10);
    spin_interval->setMaximum(1000 * 1000);
    spin_interval->setValue(500);
    auto f = new QFrame;
    auto fl = new QHBoxLayout(f);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->addWidget(cb_repeat);
    fl->addWidget(spin_interval);

    m_toolBar->addWidget(f);

    m_toolBar->addAction(QIcon(":/icons/mesytec/document-save.png"), QSL("Save"),
                         this, &PacketEditor::save);
    m_toolBar->addAction(QIcon(":/icons/mesytec/document-save-as.png"), QSL("Save As"),
                         this, &PacketEditor::saveAs);

    connect(cb_repeat, &QAbstractButton::toggled,
            this, [=] () {
                emit sendRepeatChanged(cb_repeat->isChecked(), spin_interval->value());
            });

    connect(spin_interval, qOverload<int>(&QSpinBox::valueChanged),
            this, [=] () {
                emit sendRepeatChanged(cb_repeat->isChecked(), spin_interval->value());
            });

    new SyntaxHighlighter(m_editor->document());

    resize(800, 600);
}

PacketEditor::~PacketEditor()
{
}

void PacketEditor::setText(const QString &contents)
{
    m_editor->setPlainText(contents);
    updateWindowTitle();
}

void PacketEditor::setFilepath(const QString &path)
{
    m_filepath = path;
    updateWindowTitle();
}

void PacketEditor::updateWindowTitle()
{
    QString title;

    if (QFileInfo(m_filepath).isFile())
        title = m_filepath;
    else
        title = "<unsaved>";

    if (isModified())
        title += " *";

    setWindowTitle(title);
}

/* Adapted from the QSyntaxHighlighter documentation. */
void SyntaxHighlighter::highlightBlock(const QString &text)
{
    static const QRegularExpression reComment("#.*$");
    static const QRegExp reMultiStart("/\\*");
    static const QRegExp reMultiEnd("\\*/");

    QTextCharFormat commentFormat;
    commentFormat.setForeground(Qt::blue);

    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1)
    {
        startIndex = text.indexOf(reMultiStart);
    }

    while (startIndex >= 0)
    {
        int endIndex = text.indexOf(reMultiEnd, startIndex);
        int commentLength;
        if (endIndex == -1)
        {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        }
        else
        {
            commentLength = endIndex - startIndex + reMultiEnd.matchedLength() + 3;
        }
        setFormat(startIndex, commentLength, commentFormat);
        startIndex = text.indexOf(reMultiStart, startIndex + commentLength);
    }

    QRegularExpressionMatch match;
    int index = text.indexOf(reComment, 0, &match);
    if (index >= 0)
    {
        int length = match.capturedLength();
        setFormat(index, length, commentFormat);
    }
}
