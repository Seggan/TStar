// =============================================================================
// BackgroundNeutralizationDialog.cpp
//
// Implementation of the background neutralization dialog and algorithm.
// Uses robust statistics to compute per-channel offsets from a user-selected
// reference region and applies subtractive correction across the entire image.
// =============================================================================

#include "BackgroundNeutralizationDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include "../core/RobustStatistics.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDebug>
#include <QApplication>

#include <algorithm>
#include <numeric>


// =============================================================================
// Construction and Destruction
// =============================================================================

BackgroundNeutralizationDialog::BackgroundNeutralizationDialog(QWidget* parent)
    : DialogBase(parent, tr("Background Neutralization"), 350, 100)
{
    m_interactionEnabled = false;
    setupUI();
}

BackgroundNeutralizationDialog::~BackgroundNeutralizationDialog()
{
    setSelectionMode(false);
}


// =============================================================================
// Interaction Control
// =============================================================================

void BackgroundNeutralizationDialog::setInteractionEnabled(bool enabled)
{
    if (m_interactionEnabled == enabled)
        return;

    m_interactionEnabled = enabled;

    if (m_activeViewer)
        setSelectionMode(m_interactionEnabled);
}

void BackgroundNeutralizationDialog::setSelectionMode(bool active)
{
    if (!m_activeViewer)
        return;

    if (active) {
        m_activeViewer->setInteractionMode(ImageViewer::Mode_Selection);
        m_activeViewer->setRegionSelectedCallback(
            [this](QRectF r) { this->onRectSelected(r); });
    } else {
        qDebug() << "[BackgroundNeutralization::setSelectionMode] Disabling."
                 << "Viewer:" << m_activeViewer
                 << "Visible?" << (m_activeViewer ? m_activeViewer->isVisible() : false);

        if (m_activeViewer) {
            m_activeViewer->setInteractionMode(ImageViewer::Mode_PanZoom);
            m_activeViewer->clearSelection();
            m_activeViewer->setRegionSelectedCallback(nullptr);
        }
    }
}

void BackgroundNeutralizationDialog::setViewer(ImageViewer* viewer)
{
    if (m_activeViewer == viewer)
        return;

    // Deactivate selection mode on the previous viewer
    if (m_activeViewer)
        setSelectionMode(false);

    m_activeViewer = viewer;

    // Activate selection mode on the new viewer if interaction is enabled
    if (m_activeViewer && m_interactionEnabled)
        setSelectionMode(true);
}


// =============================================================================
// UI Construction
// =============================================================================

void BackgroundNeutralizationDialog::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* info = new QLabel(
        tr("Select a background reference on the image by drawing a rectangle."),
        this);
    info->setWordWrap(true);
    layout->addWidget(info);

    m_statusLabel = new QLabel(
        tr("Ready - Please select a background region in the viewer."), this);
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    // Action buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();

    m_btnCancel = new QPushButton(tr("Cancel"), this);
    m_btnApply  = new QPushButton(tr("Apply"), this);
    m_btnApply->setEnabled(false);

    connect(m_btnApply,  &QPushButton::clicked,
            this, &BackgroundNeutralizationDialog::onApply);
    connect(m_btnCancel, &QPushButton::clicked,
            this, &QDialog::reject);

    btnLayout->addWidget(m_btnCancel);
    btnLayout->addWidget(m_btnApply);
    layout->addLayout(btnLayout);
}


// =============================================================================
// Selection Handling
// =============================================================================

void BackgroundNeutralizationDialog::onRectSelected(const QRectF& r)
{
    qDebug() << "[BackgroundNeutralizationDialog::onRectSelected]"
             << "Received rect:" << r;

    try {
        if (!m_statusLabel || !m_btnApply)
            return;

        m_selection = r.toRect();

        if (m_selection.width() >= 2 && m_selection.height() >= 2) {
            m_hasSelection = true;
            m_statusLabel->setText(
                tr("Selection: %1x%2 at %3,%4")
                    .arg(m_selection.width()).arg(m_selection.height())
                    .arg(m_selection.x()).arg(m_selection.y()));
            m_btnApply->setEnabled(true);
        } else {
            m_hasSelection = false;
            m_statusLabel->setText(tr("Selection too small."));
            m_btnApply->setEnabled(false);
        }
    } catch (const std::exception& e) {
        qWarning() << "BackgroundNeutralization selection error:" << e.what();
    } catch (...) {
        qWarning() << "BackgroundNeutralization selection error: unknown";
    }
}

void BackgroundNeutralizationDialog::onApply()
{
    if (!m_hasSelection)
        return;

    emit apply(m_selection);
    // Dialog remains open for repeated operations
}

void BackgroundNeutralizationDialog::closeEvent(QCloseEvent* event)
{
    setSelectionMode(false);
    QDialog::closeEvent(event);
}


// =============================================================================
// Background Neutralization Algorithm
//
// 1. Sample pixel values from the selected reference rectangle.
// 2. Compute per-channel medians and the average median as a target level.
// 3. Calculate per-channel offsets (mean - average_median).
// 4. Subtract offsets from every pixel, clamping to [0, 1].
// =============================================================================

void BackgroundNeutralizationDialog::neutralizeBackground(ImageBuffer& img,
                                                           const QRect& rect)
{
    if (img.channels() != 3)
        return;

    const int w  = img.width();
    const int h  = img.height();
    const int ch = img.channels();

    ImageBuffer::WriteLock lock(&img);
    std::vector<float>& data = img.data();
    if (data.empty())
        return;

    // Step 1: Extract sample data from the reference rectangle
    std::vector<float> sample[3];

    const int x0 = std::clamp(rect.x(),                  0, w - 1);
    const int y0 = std::clamp(rect.y(),                  0, h - 1);
    const int x1 = std::clamp(rect.x() + rect.width(),   0, w);
    const int y1 = std::clamp(rect.y() + rect.height(),  0, h);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            size_t idx = (static_cast<size_t>(y) * w + x) * ch;
            if (std::isfinite(data[idx]))
                sample[0].push_back(data[idx]);
            if (std::isfinite(data[idx + 1]))
                sample[1].push_back(data[idx + 1]);
            if (std::isfinite(data[idx + 2]))
                sample[2].push_back(data[idx + 2]);
        }
    }

    if (sample[0].empty() || sample[1].empty() || sample[2].empty())
        return;

    // Step 2: Compute per-channel medians and average median
    double med[3];
    double avgMed = 0.0;
    for (int c = 0; c < 3; ++c) {
        med[c]  = RobustStatistics::getMedian(sample[c]);
        avgMed += med[c];
    }
    avgMed /= 3.0;

    // Step 3: Compute per-channel offsets relative to the average median
    double offset[3];
    for (int c = 0; c < 3; ++c) {
        double mean = std::accumulate(sample[c].begin(), sample[c].end(), 0.0)
                      / sample[c].size();
        offset[c] = mean - avgMed;
        if (!std::isfinite(offset[c]))
            offset[c] = 0.0;
    }

    // Step 4: Apply subtractive correction to all pixels
    const size_t totalPixels = static_cast<size_t>(w) * h;

    #pragma omp parallel for
    for (size_t i = 0; i < totalPixels; ++i) {
        size_t idx = i * ch;
        for (int c = 0; c < 3; ++c) {
            float v = data[idx + c] - static_cast<float>(offset[c]);
            data[idx + c] = std::clamp(v, 0.0f, 1.0f);
        }
    }
}