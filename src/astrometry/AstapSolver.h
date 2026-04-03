#ifndef ASTAPSOLVER_H
#define ASTAPSOLVER_H

// ============================================================================
// AstapSolver
//
// Wraps the external ASTAP plate-solving executable. Provides automatic
// discovery of the ASTAP binary and its star-catalog database on all
// supported platforms (Windows, macOS, Linux). The solve() method exports
// a temporary TIFF image, invokes ASTAP with progressively relaxed
// parameters, and parses the resulting WCS solution.
// ============================================================================

#include <QObject>
#include <QString>
#include <QProcess>
#include <QMutex>
#include <atomic>

#include "../ImageBuffer.h"
#include "NativePlateSolver.h"   // NativeSolveResult

class AstapSolver : public QObject {
    Q_OBJECT

public:
    explicit AstapSolver(QObject* parent = nullptr);
    ~AstapSolver() override;

    // ------------------------------------------------------------------------
    // Public interface (mirrors NativePlateSolver API)
    // ------------------------------------------------------------------------

    /**
     * Initiates an asynchronous plate solve using ASTAP.
     * Emits finished() with the result when done.
     *
     * @param image      The image to solve.
     * @param raHint     Approximate RA of field center (degrees).
     * @param decHint    Approximate Dec of field center (degrees).
     * @param radiusDeg  Search radius (degrees).
     * @param pixelScale Pixel scale (arcsec/pixel).
     */
    void solve(const ImageBuffer& image,
               double raHint, double decHint,
               double radiusDeg, double pixelScale);

    /** Requests cancellation of the current solve. Thread-safe. */
    void cancelSolve();

    // ------------------------------------------------------------------------
    // Static path-resolution utilities
    // ------------------------------------------------------------------------

    /** Locates the ASTAP executable on the current platform. */
    static QString getAstapExecutable();

    /**
     * Locates the ASTAP star-catalog database directory.
     * @param overridePath  Optional user-provided path to check first.
     */
    static QString getAstapDatabasePath(const QString& overridePath = QString());

    /** Returns true if the given directory contains ASTAP catalog files. */
    static bool containsAstapCatalogs(const QString& path);

signals:
    void logMessage(const QString& msg);
    void finished(const NativeSolveResult& result);

private:
    // ------------------------------------------------------------------------
    // Internal helpers
    // ------------------------------------------------------------------------

    /**
     * Attempts to gracefully terminate an ASTAP process, escalating to
     * kill and platform-specific forced termination if necessary.
     */
    bool terminateAstapProcess(QProcess& process, int terminateWaitMs);

    /** Parses ASTAP .wcs output into a NativeSolveResult. */
    bool parseAstapWCS(const QString& wcsFile, int imageHeight,
                       NativeSolveResult& res);

    /** Parses ASTAP .ini output into a NativeSolveResult. */
    bool parseAstapIni(const QString& iniFile, int imageHeight,
                       NativeSolveResult& res);

    // ------------------------------------------------------------------------
    // Member state
    // ------------------------------------------------------------------------

    QMutex              m_processMutex;
    QProcess*           m_runningProcess = nullptr;
    std::atomic<bool>   m_cancelRequested{false};
};

#endif // ASTAPSOLVER_H