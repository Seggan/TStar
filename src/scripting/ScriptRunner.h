/**
 * @file ScriptRunner.h
 * @brief Executes parsed script commands through registered handlers.
 *
 * ScriptRunner is the central execution engine for TStar scripts. It
 * maintains a registry of available commands, manages script-level
 * variables, handles variable substitution at execution time, and
 * provides progress / logging / cancellation infrastructure.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef SCRIPT_RUNNER_H
#define SCRIPT_RUNNER_H

#include "ScriptTypes.h"
#include "ScriptParser.h"

#include <QObject>
#include <QMap>
#include <atomic>

namespace Scripting {

class ScriptRunner : public QObject {
    Q_OBJECT

public:
    explicit ScriptRunner(QObject* parent = nullptr);
    ~ScriptRunner() override;

    // ========================================================================
    // Command registration
    // ========================================================================

    /** @brief Register a single command definition. */
    void registerCommand(const CommandDef& def);

    /** @brief Register a batch of command definitions. */
    void registerCommands(const QVector<CommandDef>& defs);

    /** @brief Check whether a command with the given name is registered. */
    bool hasCommand(const QString& name) const;

    /** @brief Look up a command definition by name; returns nullptr if not found. */
    const CommandDef* getCommand(const QString& name) const;

    /** @brief Return the names of all registered commands. */
    QStringList registeredCommands() const;

    // ========================================================================
    // Script execution
    // ========================================================================

    /**
     * @brief Parse and execute a script file.
     * @param path  Filesystem path to the script.
     * @return Overall execution result.
     */
    ScriptResult executeFile(const QString& path);

    /**
     * @brief Parse and execute a script provided as a string.
     * @param content  Full script text.
     * @return Overall execution result.
     */
    ScriptResult executeString(const QString& content);

    /**
     * @brief Execute a pre-parsed list of commands.
     * @param commands  Commands to execute sequentially.
     * @return Overall execution result.
     */
    ScriptResult executeCommands(const QVector<ScriptCommand>& commands);

    /**
     * @brief Execute a single command (variable substitution + validation + handler).
     * @param cmd  The command to execute.
     * @return true on success.
     */
    bool executeCommand(const ScriptCommand& cmd);

    // ========================================================================
    // Variables
    // ========================================================================

    /** @brief Set a script-level variable. */
    void setVariable(const QString& name, const QString& value);

    /** @brief Retrieve a script-level variable (empty string if undefined). */
    QString variable(const QString& name) const;

    /** @brief Access the full variable map. */
    const QMap<QString, QString>& variables() const { return m_variables; }

    // ========================================================================
    // Working directory
    // ========================================================================

    /** @brief Set the working directory for relative path resolution. */
    void setWorkingDirectory(const QString& path);

    /** @brief Get the current working directory. */
    QString workingDirectory() const { return m_workingDir; }

    // ========================================================================
    // Cancellation
    // ========================================================================

    /** @brief Request cancellation of the running script. */
    void requestCancel();

    /** @brief Check whether cancellation has been requested. */
    bool isCancelled() const { return m_cancelled.load(); }

    /** @brief Clear the cancellation flag so a new script can be executed. */
    void resetCancel();

    // ========================================================================
    // Error handling
    // ========================================================================

    /**
     * @brief Record an error and emit a log message.
     * @param message     Human-readable error description.
     * @param lineNumber  Source line that caused the error (0 if unknown).
     */
    void setError(const QString& message, int lineNumber);

    /** @brief Retrieve the most recent error message. */
    QString lastError() const { return m_lastError; }

    /** @brief Retrieve the source line of the most recent error. */
    int lastErrorLine() const { return m_lastErrorLine; }

    // ========================================================================
    // Logging
    // ========================================================================

    /**
     * @brief Emit a log message.
     *
     * Intended for use inside tight command loops where signal/slot
     * overhead needs to be minimized. Currently delegates to the
     * logMessage signal.
     */
    void logMessageDirect(const QString& message, const QString& color);

    /**
     * @brief Perform variable substitution on the given text.
     *
     * Supports both ${VAR} and $VAR syntax. Resolves against script
     * variables first, then against dynamic metadata from the current image.
     */
    QString substituteVariables(const QString& text) const;

signals:
    /** @brief Emitted immediately before a command is executed. */
    void commandStarted(const QString& name, int lineNumber);

    /** @brief Emitted immediately after a command finishes. */
    void commandFinished(const QString& name, bool success);

    /** @brief Progress update: descriptive message and fraction [0..1]. */
    void progressChanged(const QString& message, double progress);

    /** @brief General-purpose log message with a color hint. */
    void logMessage(const QString& message, const QString& color);

    /** @brief Emitted when the entire script has finished. */
    void finished(bool success);

    /** @brief Emitted when cancellation has been requested. */
    void cancelRequested();

    /**
     * @brief Emitted when a script command loads a new image.
     *
     * The image data is available through
     * StackingCommands::getCurrentImage().
     */
    void imageLoaded(const QString& title);

private:
    /** @brief Validate a command's argument count against its definition. */
    bool validateCommand(const ScriptCommand& cmd);

    // -- Member data ---------------------------------------------------------

    QMap<QString, CommandDef>  m_commands;            ///< Registered command handlers.
    QMap<QString, QString>    m_variables;            ///< Script-level variables.
    QString                   m_workingDir;           ///< Current working directory.
    QString                   m_lastError;            ///< Most recent error message.
    int                       m_lastErrorLine = 0;    ///< Line number of last error.
    std::atomic<bool>         m_cancelled{false};     ///< Cancellation flag.
};

} // namespace Scripting

#endif // SCRIPT_RUNNER_H