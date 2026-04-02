#include "PCCDistributionDialog.h"
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

PCCDistributionDialog::PCCDistributionDialog(const PCCResult& result, QWidget* parent)
    : DialogBase(parent, tr("Star Distribution (PCC Analysis)"), 800, 400), m_result(result) 
{
    // Preferred size already handled by DialogBase
}

void PCCDistributionDialog::paintEvent([[maybe_unused]] QPaintEvent* event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);
    
    // Split into 2 plots
    int w = width() / 2;
    int h = height();
    
    QRect rectR(0, 0, w, h);
    QRect rectB(w, 0, w, h);
    
    drawScatterPlot(p, rectR, m_result.CatRG, m_result.ImgRG, m_result.polyRG, m_result.isQuadratic, tr("R/G Distribution"), Qt::red);
    drawScatterPlot(p, rectB, m_result.CatBG, m_result.ImgBG, m_result.polyBG, m_result.isQuadratic, tr("B/G Distribution"), Qt::blue);
    
    // Draw Separator
    p.setPen(Qt::gray);
    p.drawLine(w, 0, w, h);
}

void PCCDistributionDialog::drawScatterPlot(QPainter& p, const QRect& rect, 
                                            const std::vector<double>& xData, 
                                            const std::vector<double>& yData, 
                                            const double coeffs[3], bool isQuadratic,
                                            const QString& title, const QColor& color)
{
    if (xData.empty() || xData.size() != yData.size()) return;
    
    // Margins
    int m = 40;
    QRect plotRect = rect.adjusted(m, m, -m, -m);
    
    // Find Limits
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    
    for (double v : xData) {
        if (v < minX) minX = v;
        if (v > maxX) maxX = v;
    }
    for (double v : yData) {
        if (v < minY) minY = v;
        if (v > maxY) maxY = v;
    }
    
    // Padding
    double padX = (maxX - minX) * 0.1; if (padX == 0) padX = 0.1;
    double padY = (maxY - minY) * 0.1; if (padY == 0) padY = 0.1;
    minX -= padX; maxX += padX;
    minY -= padY; maxY += padY;
    
    // Helper to map coordinates
    auto mapX = [&](double val) {
        return plotRect.left() + (val - minX) / (maxX - minX) * plotRect.width();
    };
    auto mapY = [&](double val) {
        return plotRect.bottom() - (val - minY) / (maxY - minY) * plotRect.height();
    };
    
    // Draw Axes
    p.setPen(Qt::black);
    p.drawLine(plotRect.topLeft(), plotRect.bottomLeft());
    p.drawLine(plotRect.bottomLeft(), plotRect.bottomRight());
    
    // Draw Title
    p.drawText(rect.adjusted(0, 10, 0, 0), Qt::AlignTop | Qt::AlignHCenter, title);
    
    // Draw Labels
    p.drawText(rect.adjusted(0, 0, 0, -10), Qt::AlignBottom | Qt::AlignHCenter, tr("Expected (Catalog)"));
    p.save();
    p.translate(rect.left() + 15, rect.center().y());
    p.rotate(-90);
    p.drawText(0, 0, tr("Measured (Image)"));
    p.restore();
    
    // Draw Grid (Simple)
    p.setPen(QColor(220, 220, 220));
    for (int i=1; i<5; i++) {
        double xC = minX + (maxX - minX) * i / 5.0;
        double yC = minY + (maxY - minY) * i / 5.0;
        int px = mapX(xC);
        int py = mapY(yC);
        p.drawLine(px, plotRect.top(), px, plotRect.bottom());
        p.drawLine(plotRect.left(), py, plotRect.right(), py);
    }
    
    // Draw Points
    p.setPen(Qt::NoPen);
    p.setBrush(color.lighter(150));
    for (size_t i=0; i<xData.size(); i++) {
        int px = mapX(xData[i]);
        int py = mapY(yData[i]);
        p.drawEllipse(QPoint(px, py), 3, 3);
    }
    
    // Draw Fit Line / Curve
    p.setPen(QPen(Qt::black, 2, Qt::DashLine));
    if (isQuadratic) {
        QPainterPath path;
        for (int i = 0; i <= 40; ++i) {
            double x = minX + (double)i / 40.0 * (maxX - minX);
            double y = coeffs[0] * x * x + coeffs[1] * x + coeffs[2];
            QPointF pt(mapX(x), mapY(y));
            if (i == 0) path.moveTo(pt);
            else path.lineTo(pt);
        }
        p.drawPath(path);
    } else {
        double y1 = coeffs[1] * minX + coeffs[2];
        double y2 = coeffs[1] * maxX + coeffs[2];
        p.drawLine(QPointF(mapX(minX), mapY(y1)), QPointF(mapX(maxX), mapY(y2)));
    }
    
    // Fit line represents the current correction state.
    // When perfectly aligned, it has slope=1 and intercept=0.
}
