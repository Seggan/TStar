#ifndef PCCDISTRIBUTIONDIALOG_H
#define PCCDISTRIBUTIONDIALOG_H

/**
 * @file PCCDistributionDialog.h
 * @brief Scatter plot visualization of PCC star distribution analysis.
 *
 * Displays side-by-side scatter plots of catalog vs. measured color ratios
 * (R/G and B/G) with regression fit overlays, allowing visual verification
 * of the photometric calibration quality.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "DialogBase.h"
#include "../photometry/PCCCalibrator.h"

#include <vector>

class QPainter;

class PCCDistributionDialog : public DialogBase {
    Q_OBJECT

public:
    explicit PCCDistributionDialog(const PCCResult& result, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /**
     * @brief Render a single scatter plot with regression fit line/curve.
     *
     * @param p           Active painter.
     * @param rect        Drawing region within the dialog.
     * @param xData       Expected (catalog) color ratios.
     * @param yData       Measured (image) color ratios.
     * @param coeffs      Polynomial coefficients [a, b, c] for fit.
     * @param isQuadratic Whether the fit is quadratic or linear.
     * @param title       Plot title.
     * @param color       Point color.
     */
    void drawScatterPlot(QPainter& p, const QRect& rect,
                         const std::vector<double>& xData,
                         const std::vector<double>& yData,
                         const double coeffs[3], bool isQuadratic,
                         const QString& title, const QColor& color);

    PCCResult m_result;
};

#endif // PCCDISTRIBUTIONDIALOG_H