/**
 * @file ScriptDialog.cpp
 * @brief Implementation of the TStar script editor and execution dialog.
 *
 * Copyright (C) 2024-2026 TStar Team
 */
#include "ScriptDialog.h"

#include "../MainWindow.h"
#include "../scripting/StackingCommands.h"
#include "../ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QCoreApplication>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QThreadPool>
#include <QtConcurrent>

// ----------------------------------------------------------------------------
// Constructor / Destructor
// ----------------------------------------------------------------------------

ScriptDialog::ScriptDialog(MainWindow* parent)
    : QDialog(parent)
    , m_mainWindow(parent)
{
    setWindowTitle(tr("Script Runner"));
    setMinimumSize(800, 600);
    resize(900, 700);

    setupUI();
    populatePredefinedScripts();

    // Register stacking-specific commands into the script runner
    Scripting::StackingCommands::registerCommands(m_runner);
    buildCommandTooltip();

    // Connect runner signals to UI update slots
    connect(&m_runner, &Scripting::ScriptRunner::commandStarted,
            this,      &ScriptDialog::onCommandStarted);
    connect(&m_runner, &Scripting::ScriptRunner::commandFinished,
            this,      &ScriptDialog::onCommandFinished);
    connect(&m_runner, &Scripting::ScriptRunner::logMessage,
            this,      &ScriptDialog::onLogMessage);
    connect(&m_runner, &Scripting::ScriptRunner::progressChanged,
            this,      &ScriptDialog::onProgressChanged);
    connect(&m_runner, &Scripting::ScriptRunner::finished,
            this,      &ScriptDialog::onFinished);
    connect(&m_runner, &Scripting::ScriptRunner::imageLoaded,
            this,      &ScriptDialog::onImageLoaded);

    if (parent)
        move(parent->geometry().center() - rect().center());
}

ScriptDialog::~ScriptDialog()
{
    if (m_isRunning)
    {
        m_runner.requestCancel();
        // Allow up to 5 seconds for any running background tasks to finish
        QThreadPool::globalInstance()->waitForDone(5000);
    }
}

// ----------------------------------------------------------------------------
// Private Methods - UI Setup
// ----------------------------------------------------------------------------

void ScriptDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Toolbar: predefined script selector and file load/save buttons
    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->addWidget(new QLabel(tr("Predefined:"), this));

    m_predefinedCombo = new QComboBox(this);
    connect(m_predefinedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ScriptDialog::onLoadPredefined);
    toolbar->addWidget(m_predefinedCombo);
    toolbar->addStretch();

    QPushButton* loadBtn = new QPushButton(tr("Load..."), this);
    connect(loadBtn, &QPushButton::clicked, this, &ScriptDialog::onLoadScript);
    toolbar->addWidget(loadBtn);

    QPushButton* saveBtn = new QPushButton(tr("Save..."), this);
    connect(saveBtn, &QPushButton::clicked, this, &ScriptDialog::onSaveScript);
    toolbar->addWidget(saveBtn);

    mainLayout->addLayout(toolbar);

    // Main vertical splitter separating the editor area from the output area
    QSplitter* splitter = new QSplitter(Qt::Vertical, this);

    // Top: horizontal split between script editor and variables table
    QSplitter* topSplitter = new QSplitter(Qt::Horizontal, this);

    // Script editor pane
    QGroupBox*   editorGroup  = new QGroupBox(tr("Script"), this);
    QVBoxLayout* editorLayout = new QVBoxLayout(editorGroup);
    m_scriptEditor = new QTextEdit(this);
    m_scriptEditor->setFontFamily("Consolas");
    m_scriptEditor->setStyleSheet("background-color: #1e1e1e; color: #d4d4d4;");
    editorLayout->addWidget(m_scriptEditor);
    topSplitter->addWidget(editorGroup);

    // Variables table pane
    QGroupBox*   varsGroup  = new QGroupBox(tr("Variables"), this);
    QVBoxLayout* varsLayout = new QVBoxLayout(varsGroup);
    m_variablesTable = new QTableWidget(this);
    m_variablesTable->setColumnCount(2);
    m_variablesTable->setHorizontalHeaderLabels({ tr("Name"), tr("Value") });
    m_variablesTable->horizontalHeader()->setStretchLastSection(true);
    varsLayout->addWidget(m_variablesTable);

    QHBoxLayout* varButtons = new QHBoxLayout();
    QPushButton* addVarBtn = new QPushButton(tr("Add"), this);
    connect(addVarBtn, &QPushButton::clicked, this, &ScriptDialog::onAddVariable);
    varButtons->addWidget(addVarBtn);

    QPushButton* removeVarBtn = new QPushButton(tr("Remove"), this);
    connect(removeVarBtn, &QPushButton::clicked, this, &ScriptDialog::onRemoveVariable);
    varButtons->addWidget(removeVarBtn);
    varButtons->addStretch();

    varsLayout->addLayout(varButtons);
    topSplitter->addWidget(varsGroup);
    topSplitter->setSizes({ 500, 500 });
    splitter->addWidget(topSplitter);

    // Bottom: output log and run controls
    QWidget*     bottomWidget = new QWidget(this);
    QVBoxLayout* bottomLayout = new QVBoxLayout(bottomWidget);

    m_progressBar = new QProgressBar(this);
    bottomLayout->addWidget(m_progressBar);

    m_outputLog = new QTextEdit(this);
    m_outputLog->setReadOnly(true);
    m_outputLog->setFontFamily("Consolas");
    m_outputLog->setStyleSheet("QTextEdit { background-color: #1e1e1e; color: #ffffff; }");
    bottomLayout->addWidget(m_outputLog);

    QHBoxLayout* runButtons = new QHBoxLayout();
    runButtons->addStretch();

    m_stopBtn = new QPushButton(tr("Stop"), this);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &ScriptDialog::onStopScript);
    runButtons->addWidget(m_stopBtn);

    m_runBtn = new QPushButton(tr("Run Script"), this);
    m_runBtn->setMinimumWidth(120);
    connect(m_runBtn, &QPushButton::clicked, this, &ScriptDialog::onRunScript);
    runButtons->addWidget(m_runBtn);

    bottomLayout->addLayout(runButtons);
    splitter->addWidget(bottomWidget);
    splitter->setSizes({ 400, 200 });

    mainLayout->addWidget(splitter);

    // Insert a default WORKING_DIR variable as a convenience for script authors
    onAddVariable();
    m_variablesTable->item(0, 0)->setText("WORKING_DIR");
    m_variablesTable->item(0, 1)->setText(QDir::currentPath());
}

void ScriptDialog::populatePredefinedScripts()
{
    m_predefinedCombo->addItem(tr("(Select predefined script)"), QString());

    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList   scriptDirs;

#ifdef Q_OS_MAC
    scriptDirs << appDir + "/../Resources/scripts";
#endif

    scriptDirs << appDir + "/scripts";
    scriptDirs << QDir::homePath() + "/TStar/scripts";

    // Development fallbacks: look one level above the build output directory
    QDir parentDir(appDir);
    parentDir.cdUp();
    scriptDirs << parentDir.absolutePath() + "/scripts";
    scriptDirs << parentDir.absolutePath() + "/src/scripts";
    scriptDirs << "scripts";

    // Deduplicate and validate the candidate paths
    QStringList validDirs;
    for (const QString& d : scriptDirs)
    {
        QDir qd(d);
        if (qd.exists())
        {
            const QString abs = qd.absolutePath();
            if (!validDirs.contains(abs))
                validDirs << abs;
        }
    }

    // Populate the combo box, avoiding duplicate filenames across search paths
    QSet<QString> addedFiles;
    for (const QString& dir : validDirs)
    {
        QDir        scriptDir(dir);
        QStringList scripts = scriptDir.entryList({ "*.tss" }, QDir::Files);

        for (const QString& script : scripts)
        {
            if (addedFiles.contains(script))
                continue;

            const QString name = QFileInfo(script).baseName().replace('_', ' ');
            m_predefinedCombo->addItem(name, scriptDir.absoluteFilePath(script));
            addedFiles.insert(script);
        }
    }
}

// ----------------------------------------------------------------------------
// Private Slots - Script File I/O
// ----------------------------------------------------------------------------

void ScriptDialog::onLoadScript()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Script"), QString(),
        tr("TStar Scripts (*.tss);;All Files (*)"));

    if (!path.isEmpty())
        loadScript(path);
}

void ScriptDialog::onSaveScript()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Script"),
        m_currentFile.isEmpty() ? "script.tss" : m_currentFile,
        tr("TStar Scripts (*.tss)"));

    if (path.isEmpty())
        return;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream out(&file);
        out << m_scriptEditor->toPlainText();
        file.close();
        m_currentFile = path;
        setWindowTitle(tr("Script Runner - %1").arg(QFileInfo(path).fileName()));
    }
}

void ScriptDialog::onLoadPredefined()
{
    const QString path = m_predefinedCombo->currentData().toString();
    if (!path.isEmpty())
        loadScript(path);
}

bool ScriptDialog::loadScript(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Error"),
                             tr("Cannot open script file: %1").arg(path));
        return false;
    }

    QTextStream in(&file);
    m_scriptEditor->setPlainText(in.readAll());
    file.close();

    m_currentFile = path;
    setWindowTitle(tr("Script Runner - %1").arg(QFileInfo(path).fileName()));
    return true;
}

// ----------------------------------------------------------------------------
// Private Slots - Variable Management
// ----------------------------------------------------------------------------

void ScriptDialog::onAddVariable()
{
    const int row = m_variablesTable->rowCount();
    m_variablesTable->insertRow(row);
    m_variablesTable->setItem(row, 0, new QTableWidgetItem("VAR_NAME"));
    m_variablesTable->setItem(row, 1, new QTableWidgetItem(""));
}

void ScriptDialog::onRemoveVariable()
{
    const int row = m_variablesTable->currentRow();
    if (row >= 0)
        m_variablesTable->removeRow(row);
}

// ----------------------------------------------------------------------------
// Private Slots - Script Execution
// ----------------------------------------------------------------------------

void ScriptDialog::onRunScript()
{
    const QString script = m_scriptEditor->toPlainText();
    if (script.trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("Empty Script"),
                             tr("Please enter or load a script to run."));
        return;
    }

    m_isRunning = true;
    m_runBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_outputLog->clear();
    m_progressBar->setValue(0);

    // Inject user-defined variables into the script runner
    for (int row = 0; row < m_variablesTable->rowCount(); ++row)
    {
        const QString name  = m_variablesTable->item(row, 0)->text();
        const QString value = m_variablesTable->item(row, 1)->text();
        m_runner.setVariable(name, value);
    }

    // Initialize the scripting engine's current image from the active viewer
    // so that commands such as "save" and "starnet" have a valid image to work with.
    if (m_mainWindow)
    {
        ImageViewer* viewer = m_mainWindow->currentViewer();
        if (viewer && viewer->getBuffer().isValid())
        {
            Scripting::StackingCommands::initCurrentImage(viewer->getBuffer());
            m_outputLog->append(tr("Initialized script image from current viewer."));
        }
        else
        {
            m_outputLog->append(tr("<span style='color:orange'>Warning: no image open in the viewer. "
                                   "Commands that require a current image may fail.</span>"));
        }
    }

    m_outputLog->append(tr("Running script..."));

    // Execute the script on a background thread to keep the UI responsive
    QThreadPool::globalInstance()->start([this, script]() {
        m_runner.executeString(script);
    });
}

void ScriptDialog::onStopScript()
{
    m_runner.requestCancel();
    m_outputLog->append(tr("<span style='color:salmon'>Script cancelled</span>"));
}

// ----------------------------------------------------------------------------
// Private Slots - Runner Signal Handlers
// ----------------------------------------------------------------------------

void ScriptDialog::onCommandStarted(const QString& name, int line)
{
    m_outputLog->append(tr("-> %1").arg(name));
    highlightLine(line, false);
}

void ScriptDialog::onCommandFinished(const QString& name, bool success)
{
    Q_UNUSED(name);
    if (!success)
        m_outputLog->append(tr("<span style='color:red'>Failed</span>"));
}

void ScriptDialog::onLogMessage(const QString& message, const QString& color)
{
    QString finalColor = color;
    if (finalColor.isEmpty() || finalColor.toLower() == "neutral")
        finalColor = "white";

    m_outputLog->append(QString("<span style='color:%1'>%2</span>").arg(finalColor, message));
}

void ScriptDialog::onProgressChanged(const QString& message, double progress)
{
    Q_UNUSED(message);
    if (progress >= 0)
        m_progressBar->setValue(static_cast<int>(progress * 100));
}

void ScriptDialog::onFinished(bool success)
{
    m_isRunning = false;
    m_runBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);

    if (!success)
    {
        const QString err  = m_runner.lastError();
        const int     line = m_runner.lastErrorLine();

        if (line > 0)
            m_outputLog->append(tr("<span style='color:red'>Script failed: %1 (line %2)</span>").arg(err).arg(line));
        else
            m_outputLog->append(tr("<span style='color:red'>Script failed: %1</span>").arg(err));

        highlightLine(line, true);
    }
    else
    {
        m_outputLog->append(tr("<span style='color:lime'>Script finished successfully.</span>"));
    }
}

void ScriptDialog::onImageLoaded(const QString& title)
{
    const ImageBuffer* img = Scripting::StackingCommands::getCurrentImage();
    if (!img || !img->isValid())
        return;

    // Copy the image produced by the script into a new viewer window
    if (m_mainWindow)
    {
        ImageBuffer displayBuf = *img;
        m_mainWindow->createNewImageWindow(displayBuf, title);
    }
}

// ----------------------------------------------------------------------------
// Private Methods - Editor Utilities
// ----------------------------------------------------------------------------

void ScriptDialog::highlightLine(int lineNumber, bool error)
{
    if (lineNumber <= 0)
        return;

    QTextCursor cursor = m_scriptEditor->textCursor();
    cursor.movePosition(QTextCursor::Start);

    for (int i = 1; i < lineNumber; ++i)
        cursor.movePosition(QTextCursor::Down);

    cursor.select(QTextCursor::LineUnderCursor);

    QTextCharFormat fmt;
    fmt.setBackground(error ? QColor(255, 100, 100, 80) : QColor(100, 255, 100, 40));
    cursor.setCharFormat(fmt);

    m_scriptEditor->setTextCursor(cursor);
}

void ScriptDialog::buildCommandTooltip()
{
    QStringList cmdNames = m_runner.registeredCommands();
    cmdNames.sort(Qt::CaseInsensitive);

    QString tip = tr("Available commands:\n");

    for (const QString& name : cmdNames)
    {
        const Scripting::CommandDef* def = m_runner.getCommand(name);
        if (!def)
            continue;

        QString line = name;

        if (!def->description.isEmpty())
        {
            line += " --- " + def->description;
        }
        else
        {
            QString args;
            for (int i = 0; i < def->minArgs; ++i)
                args += QString(" <arg%1>").arg(i + 1);
            if (def->maxArgs < 0 || def->maxArgs > def->minArgs)
                args += " [...]";
            line += args;
        }

        tip += line + "\n";
    }

    tip += tr("\nVariables: ${VAR_NAME} or $VAR_NAME");
    m_scriptEditor->setToolTip(tip);
}