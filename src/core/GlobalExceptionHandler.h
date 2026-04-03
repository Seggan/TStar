#ifndef GLOBALEXCEPTIONHANDLER_H
#define GLOBALEXCEPTIONHANDLER_H

// ============================================================================
// GlobalExceptionHandler.h
// Platform-specific crash handling and unhandled exception reporting.
// Installs SEH (Windows), POSIX signal handlers, and std::terminate hooks.
// ============================================================================

#include <QString>
#include <exception>
#if defined(Q_OS_LINUX)
#include <bits/types/siginfo_t.h>
#endif

/**
 * @brief Installs global crash handlers and provides a last-resort error dialog.
 *
 * Call GlobalExceptionHandler::init() early in main() before entering the
 * event loop. Handles:
 *   - Windows SEH exceptions (access violations, etc.)
 *   - POSIX signals (SIGSEGV, SIGABRT, SIGFPE, etc.)
 *   - std::terminate (unhandled C++ exceptions)
 */
class GlobalExceptionHandler
{
public:
    /** Install all platform-specific crash handlers. */
    static void init();

    /** Handle a caught std::exception. */
    static void handle(const std::exception& e);

    /** Handle an arbitrary error message string. */
    static void handle(const QString& errorMessage);

#ifdef Q_OS_WIN
    /** Windows Structured Exception Handling filter. */
    static long __stdcall handleSEH(struct _EXCEPTION_POINTERS* exceptionInfo);
#else
    /** POSIX signal handler with extended signal info. */
    static void handlePosixSignal(int sig, siginfo_t* info, void* context);
#endif

    /** C++ std::terminate handler. */
    static void handleTerminate();

    /** Fallback signal handler for simple signal() registrations. */
    static void handleSignal(int sig);

private:
    /**
     * @brief Display a last-resort error dialog with log context.
     * @param message  Error description.
     * @param isFatal  If true, the application will exit after the dialog closes.
     */
    static void showDialog(const QString& message, bool isFatal = false);
};

#endif // GLOBALEXCEPTIONHANDLER_H