/**
 * @file ScriptParser.cpp
 * @brief Implementation of the script parser.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "ScriptParser.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

namespace Scripting {

// ============================================================================
// File and string parsing entry points
// ============================================================================

bool ScriptParser::parseFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_errors.append(QObject::tr("Cannot open file: %1").arg(path));
        return false;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    return parseString(content, path);
}

bool ScriptParser::parseString(const QString& content,
                               const QString& sourceName)
{
    qDebug() << "ScriptParser::parseString called. Content length:"
             << content.length();
    clear();

    QStringList lines = content.split('\n');
    int lineNumber = 0;

    for (const QString& rawLine : lines) {
        lineNumber++;
        QString line = rawLine.trimmed();

        // Append to any pending continuation line from the previous iteration.
        if (!m_pendingLine.isEmpty()) {
            line = m_pendingLine + " " + line;
            m_pendingLine.clear();
        }

        // A trailing backslash indicates the command continues on the next line.
        if (line.endsWith('\\')) {
            m_pendingLine = line.left(line.length() - 1);
            continue;
        }

        // Parse the completed line; continue even on errors so that
        // all diagnostics are collected in a single pass.
        parseLine(line, lineNumber);
    }

    // Detect an unterminated continuation at end-of-file.
    if (!m_pendingLine.isEmpty()) {
        m_errors.append(
            QObject::tr("%1: Unexpected end of file in continued line")
                .arg(sourceName));
    }

    return m_errors.isEmpty();
}

void ScriptParser::clear()
{
    m_commands.clear();
    m_errors.clear();
    m_pendingLine.clear();
}

// ============================================================================
// Single-line parsing
// ============================================================================

bool ScriptParser::parseLine(const QString& line, int lineNumber)
{
    // Skip blank lines.
    if (line.isEmpty())
        return true;

    // Skip comment lines (# or ; as first character).
    if (line.startsWith('#') || line.startsWith(';'))
        return true;

    // Handle "set <name> <value>" variable assignment directives.
    // The actual substitution is deferred to ScriptRunner at execution time.
    if (line.startsWith("set ", Qt::CaseInsensitive)) {
        QString rest     = line.mid(4).trimmed();
        int     spaceIdx = rest.indexOf(' ');
        if (spaceIdx > 0) {
            QString varName  = rest.left(spaceIdx).trimmed();
            QString varValue = rest.mid(spaceIdx + 1).trimmed();
            m_variables[varName] = varValue;
            return true;
        }
    }

    // Apply any immediate variable substitutions.
    QString processed = substituteVariables(line);

    // Tokenize the line.
    QStringList tokens = tokenize(processed);
    if (tokens.isEmpty())
        return true;

    // Build a ScriptCommand from the token list.
    ScriptCommand cmd;
    cmd.lineNumber = lineNumber;

    if (!parseTokens(tokens, cmd))
        return false;

    m_commands.append(cmd);
    return true;
}

// ============================================================================
// Variable substitution
// ============================================================================

void ScriptParser::setVariable(const QString& name, const QString& value)
{
    m_variables[name] = value;
}

QString ScriptParser::substituteVariables(const QString& text)
{
    // Variable substitution is deferred to ScriptRunner so that dynamic
    // variables (e.g. image metadata) are resolved at execution time.
    return text;
}

// ============================================================================
// Tokenization
// ============================================================================

QStringList ScriptParser::tokenize(const QString& line)
{
    QStringList tokens;
    QString     current;
    bool        inQuote = false;
    QChar       quoteChar;

    for (int i = 0; i < line.length(); ++i) {
        QChar c = line[i];

        if (inQuote) {
            if (c == quoteChar) {
                // Closing quote -- emit the accumulated token.
                inQuote = false;
                if (!current.isEmpty()) {
                    tokens.append(current);
                    current.clear();
                }
            } else if (c == '\\' && i + 1 < line.length()) {
                // Backslash escape sequence inside a quoted string.
                QChar next = line[++i];
                if      (next == 'n') current += '\n';
                else if (next == 't') current += '\t';
                else                  current += next;
            } else {
                current += c;
            }
        } else {
            if (c == '"' || c == '\'') {
                // Opening quote.
                inQuote   = true;
                quoteChar = c;
            } else if (c.isSpace()) {
                // Whitespace delimits tokens outside quotes.
                if (!current.isEmpty()) {
                    tokens.append(current);
                    current.clear();
                }
            } else if (c == '#' || c == ';') {
                // Inline comment -- stop parsing the rest of the line.
                break;
            } else {
                current += c;
            }
        }
    }

    // Emit any trailing token.
    if (!current.isEmpty())
        tokens.append(current);

    return tokens;
}

// ============================================================================
// Token-to-command conversion
// ============================================================================

bool ScriptParser::parseTokens(const QStringList& tokens, ScriptCommand& cmd)
{
    if (tokens.isEmpty())
        return false;

    // The first token is always the command name (case-insensitive).
    cmd.name = tokens[0].toLower();

    for (int i = 1; i < tokens.size(); ++i) {
        QString token = tokens[i];

        if (token.startsWith('-') && token.length() > 1) {
            // Named option: strip the leading dashes.
            int     prefixLen = token.startsWith("--") ? 2 : 1;
            QString opt       = token.mid(prefixLen);
            int     eqIdx     = opt.indexOf('=');

            if (eqIdx > 0) {
                // Inline value: --name=value or -n=value.
                cmd.options[opt.left(eqIdx)] = opt.mid(eqIdx + 1);
            } else if (i + 1 < tokens.size()) {
                QString next = tokens[i + 1];
                if (next == "=") {
                    // Separated equals sign: --name = value.
                    if (i + 2 < tokens.size()) {
                        cmd.options[opt] = tokens[i + 2];
                        i += 2;
                    } else {
                        cmd.options[opt] = "";
                        i += 1;
                    }
                } else if (!next.startsWith('-')) {
                    // Next token is the value: --name value.
                    cmd.options[opt] = next;
                    i++;
                } else {
                    // Boolean flag (no value).
                    cmd.options[opt] = "";
                }
            } else {
                // Boolean flag at end of line.
                cmd.options[opt] = "";
            }
        } else if (token == "=") {
            // Stray equals sign without a preceding option -- skip.
            continue;
        } else {
            // Positional argument.
            cmd.args.append(token);
        }
    }

    return true;
}

} // namespace Scripting