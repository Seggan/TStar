/**
 * @file ScriptDialog.h
 * @brief Dialog for authoring and executing TStar scripts.
 *
 * Copyright (C) 2024-2026 TStar Team
 */
#ifndef SCRIPT_DIALOG_H
#define SCRIPT_DIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QTableWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QComboBox>
#include <memory>

#include "../scripting/ScriptRunner.h"
#include "../scripting/StackingCommands.h"

class MainWindow;

/**
 * @brief Full-featured script editor and execution dialog.
 *
 * Provides:
 *   - A syntax-highlighted script editor with tooltip documentation.
 *   - A predefined script browser populated from the application's scripts folder.
 *   - A user-editable variable table for parameterizing scripts.
 *   - A real-time output log with color-coded messages.
 *   - Start and stop controls for background script execution.
 */
class ScriptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptDialog(MainWindow* parent = nullptr);
    ~ScriptDialog() override;

    /**
     * @brief Loads a script file into the editor.
     * @param path Absolute path to the script file.
     * @return True if the file was opened and read successfully.
     */
    bool loadScript(const QString& path);

private slots:
    void onLoadScript();
    void onSaveScript();
    void onLoadPredefined();

    void onAddVariable();
    void onRemoveVariable();

    void onRunScript();
    void onStopScript();

    void onCommandStarted(const QString& name, int line);
    void onCommandFinished(const QString& name, bool success);
    void onLogMessage(const QString& message, const QString& color);
    void onProgressChanged(const QString& message, double progress);
    void onFinished(bool success);
    void onImageLoaded(const QString& title);

private:
    void setupUI();
    void populatePredefinedScripts();

    /**
     * @brief Highlights a specific line in the editor to indicate the
     *        currently executing or failing command.
     * @param lineNumber One-based line number to highlight.
     * @param error      If true, uses an error color; otherwise uses a progress color.
     */
    void highlightLine(int lineNumber, bool error);

    /**
     * @brief Builds a tooltip string for the script editor listing all registered
     *        commands with their descriptions and argument signatures.
     */
    void buildCommandTooltip();

    // UI widgets
    QComboBox*    m_predefinedCombo;
    QTextEdit*    m_scriptEditor;
    QTableWidget* m_variablesTable;
    QProgressBar* m_progressBar;
    QTextEdit*    m_outputLog;
    QPushButton*  m_runBtn;
    QPushButton*  m_stopBtn;

    // Data members
    Scripting::ScriptRunner m_runner;
    QString                 m_currentFile;
    MainWindow*             m_mainWindow;
    bool                    m_isRunning = false;
};

#endif // SCRIPT_DIALOG_H