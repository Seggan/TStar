/**
 * @file PCCDistributionDialog.cpp
 * @brief Scatter plot visualization for PCC star distribution analysis.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "PCCDistributionDialog.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

// ============================================================================
// Construction
// ============================================================================

PCCDistributionDialog::PCCDistributionDialog(const PCCResult& result, QWidget* parent)
    : DialogBase(parent, tr("Star Distribution (PCC Analysis)"), 800, 400)
    , m_result(result)
{
    // Preferred dimensions are set by DialogBase constructor.
}

// ============================================================================
// Paint event -- renders two side-by-side scatter plots
// ============================================================================

void PCCDistributionDialog::paintEvent([[maybe_unused]] QPaintEvent* event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);

    // Divide the dialog into two equal halves for R/G and B/G plots
    const int halfWidth = width() / 2;
    const int fullHeight = height();

    QRect rectRG(0,         0, halfWidth, fullHeight);
    QRect rectBG(halfWidth, 0, halfWidth, fullHeight);

    // Draw R/G distribution on the left
    drawScatterPlot(p, rectRG,
                    m_result.CatRG, m_result.ImgRG,
                    m_result.polyRG, m_result.isQuadratic,
                    tr("R/G Distribution"), Qt::red);

    // Draw B/G distribution on the right
    drawScatterPlot(p, rectBG,
                    m_result.CatBG, m_result.ImgBG,
                    m_result.polyBG, m_result.isQuadratic,
                    tr("B/G Distribution"), Qt::blue);

    // Vertical separator between the two plots
    p.setPen(Qt::gray);
    p.drawLine(halfWidth, 0, halfWidth, fullHeight);
}

// ============================================================================
// Scatter plot rendering
// ============================================================================

void PCCDistributionDialog::drawScatterPlot(
    QPainter& p, const QRect& rect,
    const std::vector<double>& xData,
    const std::vector<double>& yData,
    const double coeffs[3], bool isQuadratic,
    const QString& title, const QColor& color)
{
    if (xData.empty() || xData.size() != yData.size())
        return;

    // -- Define plot margins and effective drawing area --
    constexpr int margin = 40;
    QRect plotRect = rect.adjusted(margin, margin, -margin, -margin);

    // -- Determine data extents --
    double minX = 1e9, maxX = -1e9;
    double minY = 1e9, maxY = -1e9;

    for (double v : xData) {
        if (v < minX) minX = v;
        if (v > maxX) maxX = v;
    }
    for (double v : yData) {
        if (v < minY) minY = v;
        if (v > maxY) maxY = v;
    }

    // Add 10% padding to each axis for visual clarity
    double padX = (maxX - minX) * 0.1;
    double padY = (maxY - minY) * 0.1;
    if (padX == 0.0) padX = 0.1;
    if (padY == 0.0) padY = 0.1;
    minX -= padX; maxX += padX;
    minY -= padY; maxY += padY;

    // -- Coordinate mapping lambdas --
    auto mapX = [&](double val) -> int {
        return plotRect.left() +
               static_cast<int>((val - minX) / (maxX - minX) * plotRect.width());
    };
    auto mapY = [&](double val) -> int {
        return plotRect.bottom() -
               static_cast<int>((val - minY) / (maxY - minY) * plotRect.height());
    };

    // -- Draw axes --
    p.setPen(Qt::black);
    p.drawLine(plotRect.topLeft(),    plotRect.bottomLeft());
    p.drawLine(plotRect.bottomLeft(), plotRect.bottomRight());

    // -- Draw title and axis labels --
    p.drawText(rect.adjusted(0, 10, 0, 0), Qt::AlignTop | Qt::AlignHCenter, title);
    p.drawText(rect.adjusted(0, 0, 0, -10), Qt::AlignBottom | Qt::AlignHCenter,
               tr("Expected (Catalog)"));

    p.save();
    p.translate(rect.left() + 15, rect.center().y());
    p.rotate(-90);
    p.drawText(0, 0, tr("Measured (Image)"));
    p.restore();

    // -- Draw grid lines --
    p.setPen(QColor(220, 220, 220));
    for (int i = 1; i < 5; ++i) {
        double xCoord = minX + (maxX - minX) * i / 5.0;
        double yCoord = minY + (maxY - minY) * i / 5.0;
        int px = mapX(xCoord);
        int py = mapY(yCoord);
        p.drawLine(px, plotRect.top(), px, plotRect.bottom());
        p.drawLine(plotRect.left(), py, plotRect.right(), py);
    }

    // -- Draw data points --
    p.setPen(Qt::NoPen);
    p.setBrush(color.lighter(150));
    for (size_t i = 0; i < xData.size(); ++i) {
        int px = mapX(xData[i]);
        int py = mapY(yData[i]);
        p.drawEllipse(QPoint(px, py), 3, 3);
    }

    // -- Draw regression fit line or curve --
    p.setPen(QPen(Qt::black, 2, Qt::DashLine));
    if (isQuadratic) {
        // Quadratic fit: y = coeffs[0]*x^2 + coeffs[1]*x + coeffs[2]
        QPainterPath path;
        constexpr int kSegments = 40;
        for (int i = 0; i <= kSegments; ++i) {
            double x = minX + static_cast<double>(i) / kSegments * (maxX - minX);
            double y = coeffs[0] * x * x + coeffs[1] * x + coeffs[2];
            QPointF pt(mapX(x), mapY(y));
            if (i == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        p.drawPath(path);
    } else {
        // Linear fit: y = coeffs[1]*x + coeffs[2]
        double y1 = coeffs[1] * minX + coeffs[2];
        double y2 = coeffs[1] * maxX + coeffs[2];
        p.drawLine(QPointF(mapX(minX), mapY(y1)),
                   QPointF(mapX(maxX), mapY(y2)));
    }
    // Note: A perfect calibration produces slope=1 and intercept=0.
}