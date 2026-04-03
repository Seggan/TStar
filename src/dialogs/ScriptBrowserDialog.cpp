#include "ScriptBrowserDialog.h"

#include "../scripting/ScriptRunner.h"
#include "../scripting/StackingCommands.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFont>

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------

ScriptBrowserDialog::ScriptBrowserDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("TStar Scripts"));
    setMinimumSize(700, 500);
    setupUI();
    loadScripts();
}

// ----------------------------------------------------------------------------
// Private Methods - UI Setup
// ----------------------------------------------------------------------------

void ScriptBrowserDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    // Left panel: script list
    QGroupBox*   listGroup  = new QGroupBox(tr("Available Scripts"));
    QVBoxLayout* listLayout = new QVBoxLayout(listGroup);
    m_scriptList = new QListWidget();
    m_scriptList->setMinimumWidth(200);
    listLayout->addWidget(m_scriptList);

    m_refreshBtn = new QPushButton(tr("Refresh"));
    listLayout->addWidget(m_refreshBtn);
    splitter->addWidget(listGroup);

    // Right panel: script content preview
    QGroupBox*   previewGroup  = new QGroupBox(tr("Script Preview"));
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new QTextEdit();
    m_preview->setReadOnly(true);
    m_preview->setFont(QFont("Consolas", 9));
    m_preview->setStyleSheet("background: #1e1e1e; color: #d4d4d4;");
    previewLayout->addWidget(m_preview);
    splitter->addWidget(previewGroup);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter);

    // Bottom action buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_closeBtn = new QPushButton(tr("Close"));
    buttonLayout->addWidget(m_closeBtn);

    m_editBtn = new QPushButton(tr("Edit Script"));
    m_editBtn->setEnabled(false);
    buttonLayout->addWidget(m_editBtn);

    m_runBtn = new QPushButton(tr("Run Script"));
    m_runBtn->setEnabled(false);
    m_runBtn->setDefault(true);
    buttonLayout->addWidget(m_runBtn);

    mainLayout->addLayout(buttonLayout);

    // Signal connections
    connect(m_scriptList, &QListWidget::itemClicked,
            this, &ScriptBrowserDialog::onScriptSelected);
    connect(m_scriptList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { onRunScript(); });
    connect(m_runBtn,     &QPushButton::clicked, this, &ScriptBrowserDialog::onRunScript);
    connect(m_editBtn,    &QPushButton::clicked, this, &ScriptBrowserDialog::onEditScript);
    connect(m_refreshBtn, &QPushButton::clicked, this, &ScriptBrowserDialog::refreshScriptList);
    connect(m_closeBtn,   &QPushButton::clicked, this, &QDialog::reject);
}

// ----------------------------------------------------------------------------
// Private Methods - Script Discovery
// ----------------------------------------------------------------------------

QString ScriptBrowserDialog::scriptsDir() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QString scriptsPath;

#ifdef Q_OS_MAC
    // On macOS, the application is a bundle; resources live in Contents/Resources
    scriptsPath = appDir + "/../Resources/scripts";
    if (QDir(scriptsPath).exists())
        return QDir(scriptsPath).canonicalPath();
#endif

    // Standard location alongside the executable (Windows, Linux, and development builds)
    scriptsPath = appDir + "/scripts";
    if (QDir(scriptsPath).exists())
        return scriptsPath;

    // Fallback: one level up from the build output directory (development environment)
    QDir parentDir(appDir);
    parentDir.cdUp();

    scriptsPath = parentDir.absolutePath() + "/scripts";
    if (QDir(scriptsPath).exists())
        return QDir(scriptsPath).absolutePath();

    scriptsPath = parentDir.absolutePath() + "/src/scripts";
    if (QDir(scriptsPath).exists())
        return QDir(scriptsPath).absolutePath();

    return QString();
}

void ScriptBrowserDialog::loadScripts()
{
    m_scriptList->clear();

    const QString path = scriptsDir();
    if (path.isEmpty())
    {
        m_scriptList->addItem(tr("(Scripts folder not found)"));
        return;
    }

    QDir dir(path);
    const QStringList filters = { "*.tss", "*.txt" };
    const QStringList scripts = dir.entryList(filters, QDir::Files, QDir::Name);

    for (const QString& script : scripts)
    {
        QListWidgetItem* item = new QListWidgetItem(script);
        item->setData(Qt::UserRole, dir.absoluteFilePath(script));
        m_scriptList->addItem(item);
    }

    if (scripts.isEmpty())
        m_scriptList->addItem(tr("(No scripts found)"));
}

// ----------------------------------------------------------------------------
// Private Slots
// ----------------------------------------------------------------------------

void ScriptBrowserDialog::refreshScriptList()
{
    loadScripts();
    m_preview->clear();
    m_runBtn->setEnabled(false);
    m_editBtn->setEnabled(false);
}

void ScriptBrowserDialog::onScriptSelected(QListWidgetItem* item)
{
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty())
    {
        m_preview->clear();
        m_runBtn->setEnabled(false);
        m_editBtn->setEnabled(false);
        return;
    }

    m_selectedPath = path;
    m_runBtn->setEnabled(true);
    m_editBtn->setEnabled(true);

    // Load and display the script content in the preview pane
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream in(&file);
        m_preview->setPlainText(in.readAll());
    }
}

void ScriptBrowserDialog::onRunScript()
{
    if (m_selectedPath.isEmpty())
        return;

    accept();
}

void ScriptBrowserDialog::onEditScript()
{
    if (m_selectedPath.isEmpty())
        return;

    QDesktopServices::openUrl(QUrl::fromLocalFile(m_selectedPath));
}

// ----------------------------------------------------------------------------
// Public Methods
// ----------------------------------------------------------------------------

QString ScriptBrowserDialog::selectedScript() const
{
    return m_selectedPath;
}