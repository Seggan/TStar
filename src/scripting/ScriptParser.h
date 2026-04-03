/**
 * @file ScriptParser.h
 * @brief Tokenizer and parser for TStar script files.
 *
 * ScriptParser reads a script (from a file or a string), splits it into
 * logical lines (handling continuation with backslash), performs variable
 * substitution, tokenizes each line respecting quoted strings, and
 * produces a list of ScriptCommand objects ready for execution.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef SCRIPT_PARSER_H
#define SCRIPT_PARSER_H

#include "ScriptTypes.h"
#include <QVector>

namespace Scripting {

/**
 * @brief Parses script text into a sequence of ScriptCommand objects.
 */
class ScriptParser {
public:
    ScriptParser() = default;

    // ========================================================================
    // Public interface
    // ========================================================================

    /**
     * @brief Parse a script from a file on disk.
     * @param path  Filesystem path to the script file.
     * @return true if parsing completed without errors.
     */
    bool parseFile(const QString& path);

    /**
     * @brief Parse a script from an in-memory string.
     * @param content     The full script text.
     * @param sourceName  Label used in error messages (defaults to "script").
     * @return true if parsing completed without errors.
     */
    bool parseString(const QString& content,
                     const QString& sourceName = "script");

    /** @brief Retrieve the list of successfully parsed commands. */
    const QVector<ScriptCommand>& commands() const { return m_commands; }

    /** @brief Retrieve any error messages generated during parsing. */
    const QStringList& errors() const { return m_errors; }

    /** @brief Discard all parsed commands, errors, and pending state. */
    void clear();

    /**
     * @brief Define or overwrite a variable for later substitution.
     * @param name   Variable name (without $ prefix).
     * @param value  Variable value.
     */
    void setVariable(const QString& name, const QString& value);

    /** @brief Access the current variable map. */
    const QMap<QString, QString>& variables() const { return m_variables; }

private:
    // ========================================================================
    // Internal helpers
    // ========================================================================

    /** @brief Parse a single logical line and append the result to m_commands. */
    bool parseLine(const QString& line, int lineNumber);

    /**
     * @brief Perform variable substitution in @p text.
     * @note  Currently deferred to ScriptRunner; returns text unchanged.
     */
    QString substituteVariables(const QString& text);

    /** @brief Split a line into tokens, respecting quotes and escape sequences. */
    QStringList tokenize(const QString& line);

    /** @brief Interpret a token list as a ScriptCommand (name + args + options). */
    bool parseTokens(const QStringList& tokens, ScriptCommand& cmd);

    // ========================================================================
    // Member data
    // ========================================================================

    QVector<ScriptCommand>   m_commands;
    QMap<QString, QString>   m_variables;
    QStringList              m_errors;
    QString                  m_pendingLine;  ///< Accumulator for continuation lines.
};

} // namespace Scripting

#endif // SCRIPT_PARSER_H