/**
 * @file SimplePlotWidget.h
 * @brief Lightweight line-graph widget for displaying sequence analysis metrics.
 *
 * Renders a simple XY line graph using QPainter. Intended for displaying
 * per-frame quality metrics such as FWHM, roundness, and eccentricity.
 * Supports point highlighting on mouse hover and selection by click.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef SIMPLE_PLOT_WIDGET_H
#define SIMPLE_PLOT_WIDGET_H

#include <QWidget>
#include <QVector>
#include <QString>
#include <QColor>
#include <QPointF>

/**
 * @brief Simple QPainter-based XY line plot with hover and selection support.
 */
class SimplePlotWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SimplePlotWidget(QWidget* parent = nullptr);

    /** Represents a single data point in the plot. */
    struct DataPoint {
        double x;
        double y;
        bool   selected;
    };

    /**
     * @brief Replaces the current dataset.
     * @param x  X-axis values (must have the same length as @p y).
     * @param y  Y-axis values.
     */
    void setData(const QVector<double>& x, const QVector<double>& y);

    /** Sets the plot title displayed at the top of the widget. */
    void setTitle(const QString& title);

    /** Sets the axis labels (currently reserved; not yet rendered). */
    void setAxisLabels(const QString& xLabel, const QString& yLabel);

    /**
     * @brief Marks the specified data points as selected (highlighted in red).
     * @param selectedIndices  Zero-based indices into the current dataset.
     */
    void setSelection(const QVector<int>& selectedIndices);

    /** Sets the line and fill colour. */
    void setColor(const QColor& color) { m_lineColor = color; update(); }

    /** Controls whether individual data points are drawn as circles. */
    void setShowPoints(bool show) { m_showPoints = show; update(); }

signals:
    /** Emitted when the user clicks near a data point. */
    void pointSelected(int index);

    /** Emitted when the selection set changes (reserved for multi-select). */
    void selectionChanged(const QVector<int>& indices);

protected:
    void paintEvent(QPaintEvent*  event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent*  event) override;

private:
    /** Maps a data-space point to widget pixel coordinates. */
    QPointF dataToScreen(const QPointF& point) const;

    /** Maps a widget pixel coordinate back to data space (reserved). */
    QPointF screenToData(const QPoint& point) const;

    /** Recomputes m_minX/Y and m_maxX/Y with padding from the current dataset. */
    void updateRange();

    QVector<DataPoint> m_data;
    QString            m_title;
    QString            m_xLabel;
    QString            m_yLabel;

    double m_minX = 0.0;
    double m_maxX = 0.0;
    double m_minY = 0.0;
    double m_maxY = 0.0;

    // Plot margins (pixels)
    int m_marginLeft   = 50;
    int m_marginRight  = 20;
    int m_marginTop    = 30;
    int m_marginBottom = 40;

    QColor m_lineColor   = QColor(0,   120, 215);
    QColor m_pointColor  = QColor(255, 255, 255);
    QColor m_selectColor = QColor(255, 0,   0  );

    bool m_showPoints     = true;
    int  m_highlightIndex = -1;   ///< Index of the point under the mouse cursor (-1 = none)
};

#endif // SIMPLE_PLOT_WIDGET_H