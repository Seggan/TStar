#ifndef PLATESOLVINGDIALOG_H
#define PLATESOLVINGDIALOG_H

/**
 * @file PlateSolvingDialog.h
 * @brief Astrometric plate solving dialog.
 *
 * Provides a UI for configuring and executing plate solving on astronomical
 * images using either the ASTAP external solver or the built-in native solver.
 * Supports SIMBAD object lookup, automatic optical parameter calculation,
 * WCS metadata application, and automatic solver fallback on failure.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QCloseEvent>
#include <QPointer>

#include "DialogBase.h"
#include "astrometry/SimbadSearcher.h"
#include "astrometry/NativePlateSolver.h"
#include "astrometry/AstapSolver.h"
#include "../ImageBuffer.h"
#include "../ImageViewer.h"

class QComboBox;

/**
 * @class PlateSolvingDialog
 * @brief Dialog for astrometric plate solving of astronomical images.
 *
 * Workflow:
 *  1. Optionally search SIMBAD for target coordinates.
 *  2. Configure optical parameters (focal length, pixel size).
 *  3. Run plate solving with selected engine (ASTAP or native).
 *  4. On failure, automatically fall back to the alternate solver.
 *  5. Apply WCS solution to the image metadata on success.
 */
class PlateSolvingDialog : public DialogBase {
    Q_OBJECT

public:
    explicit PlateSolvingDialog(QWidget* parent = nullptr);

    /** @brief Set the image buffer for solving (used by native solver). */
    void setImageBuffer(const ImageBuffer& img);

    /** @brief Set the target viewer and auto-populate from its metadata. */
    void setViewer(ImageViewer* v);

    /** @brief Whether a valid solution has been found. */
    bool isSolved() const { return m_solved; }

    /** @brief Retrieve the plate solving result. */
    NativeSolveResult result() const { return m_result; }

private slots:
    void onSearchSimbad();
    void onSolve();
    void onCancel();
    void onSolverFinished(const NativeSolveResult& res);
    void onSolverLog(const QString& text);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    /** @brief Auto-populate UI fields from image FITS/XISF metadata. */
    void updateScaleFromMetadata();

    /** @brief Calculate pixel scale from focal length and pixel size. */
    void calculatePixelScale();

    // -- Target coordinate inputs --
    QLineEdit*  m_objectName  = nullptr;
    QLineEdit*  m_raHint      = nullptr;
    QLineEdit*  m_decHint     = nullptr;
    QLineEdit*  m_fov         = nullptr;

    // -- Optical parameter inputs --
    QLineEdit*  m_focalLength = nullptr;  ///< Focal length in mm
    QLineEdit*  m_pixelSizeUm = nullptr;  ///< Pixel size in micrometers
    QLineEdit*  m_pixelScale  = nullptr;  ///< Calculated pixel scale (arcsec/px)

    // -- Solver controls --
    QTextEdit*  m_log          = nullptr;
    QComboBox*  m_engineCombo  = nullptr;
    QPushButton* m_solveBtn    = nullptr;
    QPushButton* m_cancelBtn   = nullptr;

    // -- Image and viewer references --
    ImageBuffer                  m_image;
    QPointer<ImageViewer>        m_viewer;
    QPointer<ImageViewer>        m_jobTarget;  ///< Viewer that initiated the solve

    // -- Solver backends --
    SimbadSearcher*     m_simbad     = nullptr;
    NativePlateSolver*  m_solver     = nullptr;
    AstapSolver*        m_astapSolver = nullptr;

    // -- Result state --
    bool              m_solved = false;
    NativeSolveResult m_result;

    // -- Fallback logic state --
    bool   m_isFallbackLoop = false;
    double m_lastRA     = 0;
    double m_lastDec    = 0;
    double m_lastRadius = 0;
    double m_lastScale  = 0;
};

#endif // PLATESOLVINGDIALOG_H