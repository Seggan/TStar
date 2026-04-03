/**
 * @file ScriptTypes.h
 * @brief Core type definitions for the scripting subsystem.
 *
 * Declares the fundamental data structures shared across the parser,
 * runner, and command implementations:
 *   - ScriptCommand  : a single parsed command with arguments and options.
 *   - ScriptResult   : enumeration of possible execution outcomes.
 *   - CommandDef      : metadata and handler for a registered command.
 *   - ScriptVariable  : named variable with a QVariant value.
 *   - Callback typedefs for progress and logging.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef SCRIPT_TYPES_H
#define SCRIPT_TYPES_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QMap>
#include <functional>

namespace Scripting {

// ============================================================================
// ScriptCommand
// ============================================================================

/**
 * @brief Represents a single parsed script command.
 *
 * After tokenization the parser fills in the command name, positional
 * arguments, and any named options (e.g. --key=value). Helper accessors
 * provide type-safe conversion for common option types.
 */
struct ScriptCommand {
    QString                  name;         ///< Command name (lower-cased).
    QStringList              args;         ///< Positional arguments.
    QMap<QString, QString>   options;      ///< Named options (--key=value).
    int                      lineNumber = 0; ///< Source line number for diagnostics.

    /** @brief Check whether a named option is present. */
    bool hasOption(const QString& key) const {
        return options.contains(key);
    }

    /** @brief Retrieve an option value, returning @p defaultValue if absent. */
    QString option(const QString& key,
                   const QString& defaultValue = QString()) const {
        return options.value(key, defaultValue);
    }

    /** @brief Retrieve an option value converted to int. */
    int optionInt(const QString& key, int defaultValue = 0) const {
        bool ok;
        int val = options.value(key).toInt(&ok);
        return ok ? val : defaultValue;
    }

    /** @brief Retrieve an option value converted to double. */
    double optionDouble(const QString& key, double defaultValue = 0.0) const {
        bool ok;
        double val = options.value(key).toDouble(&ok);
        return ok ? val : defaultValue;
    }

    /**
     * @brief Retrieve an option as a boolean.
     *
     * Returns true when the key is present and its value is empty, "true",
     * "1", or "yes" (case-insensitive). Returns false otherwise.
     */
    bool optionBool(const QString& key) const {
        if (!options.contains(key))
            return false;
        QString val = options[key].toLower();
        return val.isEmpty() || val == "true" || val == "1" || val == "yes";
    }
};

// ============================================================================
// ScriptResult
// ============================================================================

/**
 * @brief Possible outcomes of script execution.
 */
enum class ScriptResult {
    OK = 0,        ///< All commands executed successfully.
    SyntaxError,   ///< The script could not be parsed.
    CommandError,  ///< A command handler returned failure.
    FileError,     ///< The script file could not be opened.
    Cancelled,     ///< Execution was cancelled by the user.
    UnknownCommand ///< An unregistered command was encountered.
};

// ============================================================================
// CommandHandler / CommandDef
// ============================================================================

/**
 * @brief Signature for command handler functions.
 *
 * A handler receives the fully parsed (and variable-substituted)
 * ScriptCommand and returns true on success, false on failure.
 */
using CommandHandler = std::function<bool(const ScriptCommand&)>;

/**
 * @brief Describes a registered script command.
 *
 * Holds the command name, argument constraints, human-readable usage
 * and description strings, and the handler callback.
 */
struct CommandDef {
    QString        name;
    int            minArgs    = 0;
    int            maxArgs    = -1;   ///< -1 means unlimited.
    QString        usage;
    QString        description;
    CommandHandler handler;
    bool           scriptable = true; ///< Whether the command is available in scripts.

    CommandDef() = default;

    CommandDef(const QString& n, int min, int max,
               const QString& u, const QString& d,
               CommandHandler h, bool s = true)
        : name(n), minArgs(min), maxArgs(max)
        , usage(u), description(d), handler(h), scriptable(s)
    {}
};

// ============================================================================
// Auxiliary types
// ============================================================================

/**
 * @brief A named variable used during variable substitution.
 */
struct ScriptVariable {
    QString  name;
    QVariant value;
};

/** @brief Progress callback: message string and fractional progress [0..1]. */
using ScriptProgressCallback = std::function<void(const QString&, double)>;

/** @brief Logging callback: message string and color hint. */
using ScriptLogCallback = std::function<void(const QString&, const QString&)>;

} // namespace Scripting

#endif // SCRIPT_TYPES_H