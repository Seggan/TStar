#ifndef ERRORHANDLING_H
#define ERRORHANDLING_H

// ============================================================================
// ErrorHandling.h
// Standardized error handling utilities for the TStar application.
//
// Conventions used throughout the codebase:
//   1. bool return + optional QString* error output parameter
//   2. Q_ASSERT guards for precondition enforcement
//   3. RAII wrappers (ScopeGuard) for deterministic resource cleanup
//   4. Signal/slot for asynchronous error reporting
// ============================================================================

#include <QString>
#include <functional>
#include <optional>
#include <stdexcept>

// ============================================================================
// Error Message Formatting
// ============================================================================

/**
 * @brief Format a structured error message with operation context and error code.
 * @return e.g. "Failed to load file: /path/to/file.fits (error 123)"
 */
inline QString formatError(const QString& operation,
                           const QString& context,
                           int errorCode)
{
    return QString("%1: %2 (error %3)")
        .arg(operation, context, QString::number(errorCode));
}

/** Overload accepting a human-readable reason string instead of error code. */
inline QString formatError(const QString& operation,
                           const QString& context,
                           const QString& reason)
{
    return QString("%1: %2 - %3").arg(operation, context, reason);
}

/** Application-specific assert macro that includes the calling function name. */
#define TSTAR_ASSERT(cond, msg) Q_ASSERT_X(cond, __FUNCTION__, msg)

// ============================================================================
// Result<T> -- Type-safe error-or-value wrapper
// ============================================================================

/**
 * @brief Lightweight Result type that holds either a success value or an error.
 *
 * Usage:
 * @code
 *   Result<ImageBuffer> r = loadImage(path);
 *   if (r) { use(r.value()); }
 *   else   { log(r.error()); }
 * @endcode
 */
template<typename T>
class Result {
public:
    /** Construct a successful result. */
    explicit Result(const T& value)
        : m_value(value), m_hasError(false), m_error("") {}
    explicit Result(T&& value)
        : m_value(std::move(value)), m_hasError(false), m_error("") {}

    /** Construct an error result. */
    explicit Result(const QString& error)
        : m_hasError(true), m_error(error) {}

    bool isSuccess() const { return !m_hasError; }
    bool isError()   const { return m_hasError; }

    const T& value() const        { Q_ASSERT(!m_hasError); return m_value; }
    T&       mutable_value()      { Q_ASSERT(!m_hasError); return m_value; }
    const QString& error() const  { Q_ASSERT(m_hasError);  return m_error; }

    explicit operator bool() const { return isSuccess(); }
    bool operator!()         const { return isError(); }

private:
    T       m_value;
    bool    m_hasError;
    QString m_error;
};

// ============================================================================
// ScopeGuard -- RAII cleanup wrapper
// ============================================================================

/**
 * @brief Executes a cleanup function on scope exit unless dismissed.
 *
 * Usage:
 * @code
 *   void* resource = acquire();
 *   ScopeGuard cleanup([resource] { release(resource); });
 *   // resource is automatically released when scope exits
 *   cleanup.dismiss();  // prevent cleanup if ownership transferred
 * @endcode
 */
template<typename CleanupFunc>
class ScopeGuard {
public:
    explicit ScopeGuard(CleanupFunc func)
        : m_cleanup(func), m_enabled(true) {}

    ~ScopeGuard()
    {
        if (m_enabled) {
            m_cleanup();
        }
    }

    /** Disable the cleanup (e.g. after successful ownership transfer). */
    void dismiss() { m_enabled = false; }

private:
    CleanupFunc m_cleanup;
    bool        m_enabled;
};

// ============================================================================
// Error Reporting Functions
// ============================================================================

/** Log an error and optionally present it to the user. */
void reportUserError(const QString& title, const QString& message);

/** Log a warning. */
void reportWarning(const QString& title, const QString& message);

/** Log an informational message. */
void reportInfo(const QString& title, const QString& message);

// ============================================================================
// Validation Helpers
// ============================================================================

/** Validate that a file exists and is readable. */
bool validateFileExists(const QString& path, QString* error = nullptr);

/** Validate that an ImageBuffer has valid dimensions and data. */
bool validateBuffer(const class ImageBuffer& buffer, QString* error = nullptr);

/** Validate that a file's magic bytes match the expected format. */
bool validateFileFormat(const QString& path,
                        const QString& expectedMagic,
                        QString* error = nullptr);

#endif // ERRORHANDLING_H