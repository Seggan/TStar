#include "CurvesDialog.h"
#include "MainWindowCallbacks.h"
#include "../ImageBuffer.h"
#include "widgets/HistogramWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif

// ============================================================================
// CurvesGraph
// ============================================================================

CurvesGraph::CurvesGraph(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(340, 180);
    setMouseTracking(true);
    reset();
}

void CurvesGraph::reset()
{
    m_points.clear();
    m_points.push_back({ 0.0, 0.0 });
    m_points.push_back({ 1.0, 1.0 });
    m_dragIdx = -1;
    update();
    emit curvesChanged();
}

void CurvesGraph::setPoints(const std::vector<SplinePoint>& pts)
{
    m_points = pts;
    sortPoints();
    update();
    emit curvesChanged();
}

void CurvesGraph::sortPoints()
{
    std::sort(m_points.begin(), m_points.end());
}

SplineData CurvesGraph::getSpline() const
{
    return CubicSpline::fit(m_points);
}

void CurvesGraph::setHistogram(const std::vector<std::vector<int>>& hist)
{
    m_hist = hist;
    updatePaths();
    update();
}

void CurvesGraph::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePaths();
}

void CurvesGraph::updatePaths()
{
    const int w = width();
    const int h = height();

    if (w <= 0 || h <= 0 || m_hist.empty()) return;

    m_lastW = w;
    m_resampledBins.clear();
    m_maxVal = 0;

    HistogramWidget::computeDisplayHistogram(
        m_hist, m_hist.size(), w, m_resampledBins, m_maxVal, m_logScale);
}

void CurvesGraph::setChannelMode(int mode)
{
    m_channelMode = mode;
    update();
}

void CurvesGraph::setLogScale(bool enabled)
{
    if (m_logScale == enabled) return;
    m_logScale = enabled;
    updatePaths();
    update();
}

void CurvesGraph::setGridVisible(bool visible)
{
    m_showGrid = visible;
    update();
}

// ----------------------------------------------------------------------------
// Painting
// ----------------------------------------------------------------------------

void CurvesGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();

    p.fillRect(rect(), Qt::black);

    // --- Grid ---
    if (m_showGrid)
    {
        p.setPen(QPen(QColor(100, 100, 100), 1, Qt::SolidLine));
        for (int i = 1; i < 4; ++i)
        {
            p.drawLine(i * w / 4, 0, i * w / 4, h);
            p.drawLine(0, i * h / 4, w, i * h / 4);
        }

        p.setPen(QPen(QColor(60, 60, 60), 1, Qt::DashLine));
        for (int i = 1; i < 8; ++i)
        {
            if (i % 2 == 0) continue; // Quarters already drawn above.
            p.drawLine(i * w / 8, 0, i * w / 8, h);
            p.drawLine(0, i * h / 8, w, i * h / 8);
        }
    }

    // --- Identity diagonal (x == y reference line) ---
    p.setPen(QPen(QColor(80, 80, 80), 1, Qt::SolidLine));
    p.drawLine(0, h, w, 0);

    // --- Histogram ---
    if (!m_resampledBins.empty() && m_maxVal > 0)
    {
        const QColor colors[3] = {
            QColor(255, 80, 80),
            QColor(80, 255, 80),
            QColor(80, 80, 255)
        };

        p.setCompositionMode(QPainter::CompositionMode_Screen);

        for (int c = 0; c < (int)m_resampledBins.size(); ++c)
        {
            QPainterPath fillPath;
            QPainterPath linePath;
            fillPath.moveTo(0, h);

            bool first = true;
            for (int px = 0; px < w; ++px)
            {
                const double normH = m_resampledBins[c][px] / m_maxVal;
                const float  py    = h - static_cast<float>(normH * h);
                fillPath.lineTo(px, py);
                if (first) { linePath.moveTo(px, py); first = false; }
                else        { linePath.lineTo(px, py); }
            }
            fillPath.lineTo(w, h);
            fillPath.closeSubpath();

            QColor fillColor = colors[c % 3];
            fillColor.setAlpha(60);
            p.setPen(Qt::NoPen);
            p.setBrush(fillColor);
            p.drawPath(fillPath);

            QColor lineColor = colors[c % 3];
            lineColor.setAlpha(200);
            p.setPen(QPen(lineColor, 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawPath(linePath);
        }

        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    // --- Spline curve ---
    const SplineData spline = getSpline();
    if (spline.n >= 2)
    {
        p.setPen(QPen(QColor(250, 128, 114), 2));
        QPolygonF curvePoly;
        for (int i = 0; i <= w; ++i)
        {
            const float x   = static_cast<float>(i) / w;
            const float val = CubicSpline::interpolate(x, spline);
            curvePoly << QPointF(i, h - val * h);
        }
        p.drawPolyline(curvePoly);
    }

    // --- Control points ---
    for (size_t i = 0; i < m_points.size(); ++i)
    {
        const float px = static_cast<float>(m_points[i].x * w);
        const float py = static_cast<float>(h - m_points[i].y * h);

        p.setPen(QPen(Qt::green, 1));
        if (i == (size_t)m_dragIdx || i == (size_t)m_hoverIdx)
            p.setBrush(Qt::green);
        else
            p.setBrush(Qt::NoBrush);

        p.drawRect(QRectF(px - 3, py - 3, 6, 6));
    }
}

// ----------------------------------------------------------------------------
// Mouse interaction
// ----------------------------------------------------------------------------

void CurvesGraph::mousePressEvent(QMouseEvent* event)
{
    const int   w  = width();
    const int   h  = height();
    const float mx = static_cast<float>(event->position().x()) / w;
    const float my = 1.0f - static_cast<float>(event->position().y()) / h;

    if (event->button() == Qt::LeftButton)
    {
        // Find the nearest existing point within the pick radius.
        int   idx  = -1;
        float dist = 0.05f;
        for (size_t i = 0; i < m_points.size(); ++i)
        {
            const float dx = static_cast<float>(m_points[i].x) - mx;
            const float dy = static_cast<float>(m_points[i].y) - my;
            const float d  = std::sqrt(dx * dx + dy * dy);
            if (d < dist) { dist = d; idx = (int)i; }
        }

        if (idx != -1)
        {
            m_dragIdx = idx;
        }
        else
        {
            m_points.push_back({ mx, my });
            sortPoints();
            for (size_t i = 0; i < m_points.size(); ++i)
            {
                if (std::abs(static_cast<float>(m_points[i].x) - mx) < 1e-5f)
                {
                    m_dragIdx = (int)i;
                    break;
                }
            }
        }

        update();
        emit curvesChanged();
    }
    else if (event->button() == Qt::RightButton)
    {
        // Remove any non-endpoint point within the pick radius.
        for (size_t i = 0; i < m_points.size(); ++i)
        {
            if (i == 0 || i == m_points.size() - 1) continue;

            const float dx = static_cast<float>(m_points[i].x) - mx;
            const float dy = static_cast<float>(m_points[i].y) - my;
            if (std::sqrt(dx * dx + dy * dy) < 0.05f)
            {
                m_points.erase(m_points.begin() + i);
                update();
                emit curvesChanged();
                break;
            }
        }
    }
}

void CurvesGraph::mouseMoveEvent(QMouseEvent* event)
{
    const int   w  = width();
    const int   h  = height();
    float       mx = static_cast<float>(event->position().x()) / w;
    float       my = 1.0f - static_cast<float>(event->position().y()) / h;

    if (m_dragIdx != -1)
    {
        mx = std::max(0.0f, std::min(1.0f, mx));
        my = std::max(0.0f, std::min(1.0f, my));

        // Constrain x within the interval defined by neighbouring points.
        if (m_dragIdx > 0)
        {
            const float minX = static_cast<float>(m_points[m_dragIdx - 1].x) + 0.0001f;
            if (mx < minX) mx = minX;
        }
        if (m_dragIdx < (int)m_points.size() - 1)
        {
            const float maxX = static_cast<float>(m_points[m_dragIdx + 1].x) - 0.0001f;
            if (mx > maxX) mx = maxX;
        }

        m_points[m_dragIdx].x = mx;
        m_points[m_dragIdx].y = my;
        update();
        emit curvesChanged();
    }
    else
    {
        // Update hover highlight.
        m_hoverIdx = -1;
        for (size_t i = 0; i < m_points.size(); ++i)
        {
            const float dx = static_cast<float>(m_points[i].x) - mx;
            const float dy = static_cast<float>(m_points[i].y) - my;
            if (std::sqrt(dx * dx + dy * dy) < 0.05f)
            {
                m_hoverIdx = (int)i;
                break;
            }
        }
        update();
    }

    emit mouseHover(mx, my);
}

void CurvesGraph::mouseReleaseEvent([[maybe_unused]] QMouseEvent* event)
{
    m_dragIdx = -1;
    update();
}

// ============================================================================
// CurvesDialog
// ============================================================================

CurvesDialog::CurvesDialog(ImageViewer* viewer, QWidget* parent)
    : DialogBase(parent, tr("Curves Transformation"), 700, 450)
    , m_viewer(viewer)
{
    setMinimumWidth(500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Toolbar ---
    QHBoxLayout* topLayout = new QHBoxLayout();

    m_logBtn = new QToolButton(this);
    m_logBtn->setText(tr("Log"));
    m_logBtn->setCheckable(true);
    m_logBtn->setChecked(false);
    connect(m_logBtn, &QToolButton::toggled, this, &CurvesDialog::onLogToggled);
    topLayout->addWidget(m_logBtn);

    m_gridBtn = new QToolButton(this);
    m_gridBtn->setText(tr("Grid"));
    m_gridBtn->setCheckable(true);
    m_gridBtn->setChecked(true);
    connect(m_gridBtn, &QToolButton::toggled, this, &CurvesDialog::onGridToggled);
    topLayout->addWidget(m_gridBtn);

    topLayout->addSpacing(20);

    // Helper lambda to create coloured channel toggle buttons.
    auto createToggle = [&](const QString& text, const QString& col) -> QToolButton*
    {
        QToolButton* btn = new QToolButton(this);
        btn->setText(text);
        btn->setCheckable(true);
        btn->setChecked(true);
        btn->setFixedSize(30, 30);
        btn->setStyleSheet(
            QString("QToolButton:checked { background-color: %1; "
                    "color: black; font-weight: bold; }").arg(col));
        return btn;
    };

    m_redBtn   = createToggle("R", "#ff0000");
    m_greenBtn = createToggle("G", "#00ff00");
    m_blueBtn  = createToggle("B", "#0000ff");

    connect(m_redBtn,   &QToolButton::toggled, this, &CurvesDialog::onChannelToggled);
    connect(m_greenBtn, &QToolButton::toggled, this, &CurvesDialog::onChannelToggled);
    connect(m_blueBtn,  &QToolButton::toggled, this, &CurvesDialog::onChannelToggled);

    topLayout->addWidget(m_redBtn);
    topLayout->addWidget(m_greenBtn);
    topLayout->addWidget(m_blueBtn);
    topLayout->addStretch();
    mainLayout->addLayout(topLayout);

    // --- Curve graph ---
    m_graph = new CurvesGraph(this);

    if (m_viewer && m_viewer->getBuffer().isValid())
    {
        m_origHist = m_viewer->getBuffer().computeHistogram(65536);
        m_graph->setHistogram(m_origHist);
    }

    m_graph->installEventFilter(this);

    connect(m_graph, &CurvesGraph::curvesChanged, this, [this]() {
        onCurvesChanged(false);
    });
    connect(m_graph, &CurvesGraph::mouseHover, this, [this](double x, double y) {
        m_statsLabel->setText(
            tr("Point: x=%1, y=%2").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3));
    });

    mainLayout->addWidget(m_graph, 1);

    // --- Coordinate readout ---
    QHBoxLayout* statsLayout = new QHBoxLayout();
    m_statsLabel = new QLabel(tr("Point: x=0.000, y=0.000"), this);
    statsLayout->addWidget(m_statsLabel);
    mainLayout->addLayout(statsLayout);

    // --- Bottom controls ---
    QHBoxLayout* botLayout = new QHBoxLayout();

    QPushButton* resetBtn = new QPushButton(tr("Reset"), this);
    connect(resetBtn, &QPushButton::clicked, this, &CurvesDialog::onReset);
    botLayout->addWidget(resetBtn);

    m_previewCheck = new QCheckBox(tr("Preview"), this);
    m_previewCheck->setChecked(true);
    connect(m_previewCheck, &QCheckBox::toggled, this, &CurvesDialog::onPreviewToggled);
    botLayout->addWidget(m_previewCheck);

    botLayout->addStretch();

    QPushButton* applyBtn  = new QPushButton(tr("Apply"),  this);
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    applyBtn->setDefault(true);

    connect(applyBtn,  &QPushButton::clicked, this, &CurvesDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &CurvesDialog::reject);

    botLayout->addWidget(cancelBtn);
    botLayout->addWidget(applyBtn);
    mainLayout->addLayout(botLayout);

    if (parentWidget())
        move(parentWidget()->window()->geometry().center() - rect().center());
}

CurvesDialog::~CurvesDialog() {}

// ============================================================================
// Public interface
// ============================================================================

void CurvesDialog::setInputHistogram(const std::vector<std::vector<int>>& hist)
{
    m_origHist = hist;
    m_uiHist   = m_origHist;
    onCurvesChanged(false);
}

void CurvesDialog::reject()
{
    if (m_viewer)
        m_viewer->clearPreviewLUT();
    QDialog::reject();
}

void CurvesDialog::setViewer(ImageViewer* viewer)
{
    if (m_viewer == viewer) return;

    if (m_viewer)
        m_viewer->clearPreviewLUT();

    m_viewer = viewer;

    if (m_viewer && m_viewer->getBuffer().isValid())
    {
        m_origHist = m_viewer->getBuffer().computeHistogram(65536);
        m_uiHist   = m_origHist;
        m_graph->setHistogram(m_uiHist);
    }
    else
    {
        m_origHist.clear();
        m_uiHist.clear();
        m_graph->setHistogram({});
    }

    onCurvesChanged(true);
}

bool CurvesDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_graph && event->type() == QEvent::MouseButtonRelease)
        onCurvesChanged(true);

    return QDialog::eventFilter(obj, event);
}

// ============================================================================
// State management
// ============================================================================

CurvesDialog::State CurvesDialog::getState() const
{
    State s;
    s.points   = m_graph->getPoints();
    s.logScale = m_logBtn->isChecked();
    s.showGrid = m_gridBtn->isChecked();
    s.ch[0]    = m_redBtn->isChecked();
    s.ch[1]    = m_greenBtn->isChecked();
    s.ch[2]    = m_blueBtn->isChecked();
    s.preview  = m_previewCheck ? m_previewCheck->isChecked() : true;
    return s;
}

void CurvesDialog::setState(const State& s)
{
    const bool wasBlocked = signalsBlocked();
    blockSignals(true);

    m_graph->setPoints(s.points);

    m_logBtn->blockSignals(true);
    m_logBtn->setChecked(s.logScale);
    m_logBtn->blockSignals(false);

    m_gridBtn->blockSignals(true);
    m_gridBtn->setChecked(s.showGrid);
    m_gridBtn->blockSignals(false);

    m_redBtn->blockSignals(true);
    m_redBtn->setChecked(s.ch[0]);
    m_redBtn->blockSignals(false);

    m_greenBtn->blockSignals(true);
    m_greenBtn->setChecked(s.ch[1]);
    m_greenBtn->blockSignals(false);

    m_blueBtn->blockSignals(true);
    m_blueBtn->setChecked(s.ch[2]);
    m_blueBtn->blockSignals(false);

    if (m_previewCheck)
    {
        m_previewCheck->blockSignals(true);
        m_previewCheck->setChecked(s.preview);
        m_previewCheck->blockSignals(false);
    }

    m_graph->setLogScale(s.logScale);
    m_graph->setGridVisible(s.showGrid);

    blockSignals(wasBlocked);
}

void CurvesDialog::resetState()
{
    onReset();
}

// ============================================================================
// Slots
// ============================================================================

void CurvesDialog::onLogToggled(bool checked)
{
    m_graph->setLogScale(checked);
}

void CurvesDialog::onGridToggled(bool checked)
{
    m_graph->setGridVisible(checked);
}

void CurvesDialog::onChannelToggled()
{
    onCurvesChanged(true);
}

void CurvesDialog::onReset()
{
    m_graph->reset();
    onCurvesChanged(true);
}

void CurvesDialog::onApply()
{
    if (m_viewer) m_viewer->clearPreviewLUT();
    m_applied = true;

    const SplineData spline = m_graph->getSpline();
    const bool ch[3] = {
        m_redBtn->isChecked(),
        m_greenBtn->isChecked(),
        m_blueBtn->isChecked()
    };

    emit applyRequested(spline, ch);
    accept();
}

void CurvesDialog::onPreviewToggled([[maybe_unused]] bool checked)
{
    onCurvesChanged(true);
}

void CurvesDialog::onCurvesChanged(bool isFinal)
{
    const SplineData spline = m_graph->getSpline();
    const bool ch[3] = {
        m_redBtn->isChecked(),
        m_greenBtn->isChecked(),
        m_blueBtn->isChecked()
    };

    // --- Real-time histogram transformation ---
    // Builds a 65536-entry scatter LUT from the spline and redistributes the
    // original histogram counts into the transformed bin positions.
    if (!m_origHist.empty())
    {
        constexpr int HIST_SIZE = 65536;

        std::vector<int> transformLUT(HIST_SIZE);
        #pragma omp parallel for
        for (int i = 0; i < HIST_SIZE; ++i)
        {
            const float x      = static_cast<float>(i) / (HIST_SIZE - 1);
            const float val    = CubicSpline::interpolate(x, spline);
            transformLUT[i] = std::clamp(
                static_cast<int>(val * (HIST_SIZE - 1) + 0.5f), 0, HIST_SIZE - 1);
        }

        const int numChannels = std::min((int)m_origHist.size(), 3);
        std::vector<std::vector<int>> transformed(
            numChannels, std::vector<int>(HIST_SIZE, 0));

        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int c = 0; c < numChannels; ++c)
        {
            if (!ch[c])
            {
                // Unchecked channel passes through unchanged.
                transformed[c] = m_origHist[c];
                continue;
            }

            const int* src = m_origHist[c].data();
            int*       dst = transformed[c].data();

            for (int i = 0; i < HIST_SIZE; ++i)
            {
                if (src[i] > 0)
                    dst[transformLUT[i]] += src[i];
            }
        }

        m_graph->setHistogram(transformed);
    }

    // --- Image preview update (on mouse release only) ---
    if (isFinal)
    {
        if (!m_previewCheck->isChecked())
        {
            emit previewRequested({});
            return;
        }

        // Build per-channel LUTs (65536 entries) for real-time viewer preview.
        constexpr int   LUT_SIZE = 65536;
        std::vector<std::vector<float>> lut(3, std::vector<float>(LUT_SIZE));

        #pragma omp parallel for
        for (int i = 0; i < LUT_SIZE; ++i)
        {
            const float x = static_cast<float>(i) / 65535.0f;
            const float y = CubicSpline::interpolate(x, spline);

            for (int c = 0; c < 3; ++c)
                lut[c][i] = ch[c] ? y : x;
        }

        emit previewRequested(lut);
    }
}