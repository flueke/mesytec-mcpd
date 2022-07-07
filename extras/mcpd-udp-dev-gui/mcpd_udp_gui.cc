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
    spin_interval->setValue(1000);
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
