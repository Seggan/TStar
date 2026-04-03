#include "AberrationInspectorDialog.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QTimer>

#include <algorithm>
#include <cmath>

// Fixed screen size (pixels) for each of the nine crop panels.
static constexpr int kDisplaySize = 140;

// =============================================================================
// Constructor
// =============================================================================

AberrationInspectorDialog::AberrationInspectorDialog(const ImageBuffer& img,
                                                     QWidget* parent)
    : DialogBase(parent,
                 tr("Aberration Inspector"),
                 kDisplaySize * 3 + 40,
                 kDisplaySize * 3 + 52)
    , m_source(img)
{
    m_panels.fill(nullptr);
    setupUi();

    // Defer panel rendering until the dialog is fully visible to avoid
    // painting onto widgets that have not yet been shown.
    QTimer::singleShot(300, this, &AberrationInspectorDialog::updatePanels);
}

// =============================================================================
// Public interface
// =============================================================================

void AberrationInspectorDialog::setSource(const ImageBuffer& img)
{
    m_source = img;
    updatePanels();
}

// =============================================================================
// Private helpers
// =============================================================================

void AberrationInspectorDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // 3x3 grid holding the nine crop panels.
    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(3);

    // Positional labels for each panel cell.
    const QString kNames[9] = {
        tr("Top-Left"),    tr("Top"),    tr("Top-Right"),
        tr("Left"),        tr("Center"), tr("Right"),
        tr("Bottom-Left"), tr("Bottom"), tr("Bottom-Right")
    };

    for (int i = 0; i < 9; ++i)
    {
        // Outer container with dark background.
        QWidget* cell = new QWidget(this);
        cell->setFixedSize(kDisplaySize, kDisplaySize + 14);
        cell->setStyleSheet("background: #111;");

        QVBoxLayout* cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(0);

        // Small caption label showing the panel position name.
        QLabel* posLabel = new QLabel(kNames[i], cell);
        posLabel->setAlignment(Qt::AlignCenter);
        posLabel->setStyleSheet("color: #888; font-size: 9px; padding: 1px;");
        posLabel->setFixedHeight(14);
        cellLayout->addWidget(posLabel);

        // Image display label - scaling is performed manually so that we
        // maintain full control over interpolation quality.
        m_panels[i] = new QLabel(cell);
        m_panels[i]->setFixedSize(kDisplaySize, kDisplaySize);
        m_panels[i]->setAlignment(Qt::AlignCenter);
        m_panels[i]->setScaledContents(false);
        m_panels[i]->setStyleSheet("background: black;");
        cellLayout->addWidget(m_panels[i]);

        grid->addWidget(cell, i / 3, i % 3);
    }

    mainLayout->addLayout(grid, 1);
}

QImage AberrationInspectorDialog::cropToQImage(int cx, int cy, int size)
{
    if (!m_source.isValid())
        return QImage();

    const int imgW = m_source.width();
    const int imgH = m_source.height();
    const int half = size / 2;

    // Clamp the top-left corner so the entire crop remains within bounds.
    const int x0    = std::clamp(cx - half, 0, std::max(0, imgW - size));
    const int y0    = std::clamp(cy - half, 0, std::max(0, imgH - size));
    const int cropW = std::min(size, imgW - x0);
    const int cropH = std::min(size, imgH - y0);

    const int    ch  = m_source.channels();
    const float* src = m_source.data().data();

    std::vector<float> buf(cropW * cropH * ch);

    for (int r = 0; r < cropH; ++r)
    {
        for (int c = 0; c < cropW; ++c)
        {
            for (int k = 0; k < ch; ++k)
            {
                buf[(r * cropW + c) * ch + k] =
                    src[((y0 + r) * imgW + (x0 + c)) * ch + k];
            }
        }
    }

    ImageBuffer tmp;
    tmp.setData(cropW, cropH, ch, buf);

    return tmp.getDisplayImage(ImageBuffer::Display_AutoStretch);
}

void AberrationInspectorDialog::updatePanels()
{
    if (!m_source.isValid())
        return;

    const int W = m_source.width();
    const int H = m_source.height();

    // Crop size is 20% of the shorter image dimension, with a minimum of 10px.
    const int cropSize = std::max(10, static_cast<int>(std::min(W, H) * 0.20f));
    const int half     = cropSize / 2;

    // X and Y centres for the three columns and three rows respectively.
    const int cx[3] = { half,     W / 2, W - half };
    const int cy[3] = { half,     H / 2, H - half };

    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            const int idx = row * 3 + col;
            QImage img = cropToQImage(cx[col], cy[row], cropSize);

            if (img.isNull())
            {
                m_panels[idx]->clear();
                continue;
            }

            // Scale to exactly kDisplaySize x kDisplaySize preserving aspect
            // ratio, with smooth interpolation and a black letterbox background.
            QPixmap pm = QPixmap::fromImage(
                img.scaled(kDisplaySize, kDisplaySize,
                           Qt::KeepAspectRatio,
                           Qt::SmoothTransformation));

            m_panels[idx]->setPixmap(pm);
        }
    }
}