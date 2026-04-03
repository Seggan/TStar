#include "AlignChannelsDialog.h"
#include "DialogBase.h"

#include "../MainWindowCallbacks.h"
#include "../ImageViewer.h"
#include "../ImageBuffer.h"
#include "../widgets/CustomMdiSubWindow.h"
#include "../stacking/Registration.h"
#include "../stacking/StackingTypes.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QApplication>
#include <QMdiArea>
#include <QMdiSubWindow>

#include <opencv2/imgproc.hpp>

#include <cmath>

using namespace Stacking;

// =============================================================================
// Constructor
// =============================================================================

AlignChannelsDialog::AlignChannelsDialog(QWidget* parent)
    : DialogBase(parent, tr("Align Channels"))
{
    m_mainWindow = getCallbacks();
    setMinimumWidth(430);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // -------------------------------------------------------------------------
    // Reference image selection
    // -------------------------------------------------------------------------
    QGroupBox*   refGroup  = new QGroupBox(tr("Reference Image"), this);
    QFormLayout* refLayout = new QFormLayout(refGroup);

    m_refCombo = new QComboBox(this);
    refLayout->addRow(tr("Reference:"), m_refCombo);

    mainLayout->addWidget(refGroup);

    // -------------------------------------------------------------------------
    // Target image rows
    // -------------------------------------------------------------------------
    QGroupBox*   tgtGroup  = new QGroupBox(tr("Images to Align"), this);
    QVBoxLayout* tgtLayout = new QVBoxLayout(tgtGroup);

    const QString rowLabels[kMaxTargets] = {
        tr("Image 1:"), tr("Image 2:"), tr("Image 3:")
    };

    for (int i = 0; i < kMaxTargets; ++i)
    {
        QHBoxLayout* row = new QHBoxLayout();

        m_targets[i].check = new QCheckBox(rowLabels[i], this);
        m_targets[i].check->setChecked(i == 0);

        m_targets[i].combo = new QComboBox(this);
        m_targets[i].combo->setEnabled(i == 0);

        // The combo is enabled only when its corresponding checkbox is checked.
        connect(m_targets[i].check, &QCheckBox::toggled,
                m_targets[i].combo,  &QComboBox::setEnabled);

        row->addWidget(m_targets[i].check);
        row->addWidget(m_targets[i].combo, 1);
        tgtLayout->addLayout(row);
    }

    mainLayout->addWidget(tgtGroup);

    // -------------------------------------------------------------------------
    // Registration parameters
    // -------------------------------------------------------------------------
    QGroupBox*   paramGroup  = new QGroupBox(tr("Registration Parameters"), this);
    QVBoxLayout* paramLayout = new QVBoxLayout(paramGroup);

    m_allowRotationCheck = new QCheckBox(tr("Allow Rotation"), this);
    m_allowRotationCheck->setChecked(true);

    m_allowScaleCheck = new QCheckBox(tr("Allow Scale"), this);
    m_allowScaleCheck->setChecked(false);

    QHBoxLayout* threshRow = new QHBoxLayout();
    threshRow->addWidget(new QLabel(tr("Detection Threshold:"), this));

    m_thresholdSpin = new QDoubleSpinBox(this);
    m_thresholdSpin->setRange(2.0, 20.0);
    m_thresholdSpin->setSingleStep(0.5);
    m_thresholdSpin->setValue(4.0);
    m_thresholdSpin->setDecimals(1);
    threshRow->addWidget(m_thresholdSpin);
    threshRow->addStretch(1);

    paramLayout->addWidget(m_allowRotationCheck);
    paramLayout->addWidget(m_allowScaleCheck);
    paramLayout->addLayout(threshRow);

    mainLayout->addWidget(paramGroup);

    // -------------------------------------------------------------------------
    // Status label and action buttons
    // -------------------------------------------------------------------------
    m_statusLabel = new QLabel(QString(), this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    QHBoxLayout* btnLayout = new QHBoxLayout();

    QPushButton* refreshBtn = new QPushButton(tr("Refresh List"), this);
    m_applyBtn              = new QPushButton(tr("Align"), this);
    QPushButton* closeBtn   = new QPushButton(tr("Close"), this);

    connect(refreshBtn, &QPushButton::clicked, this, &AlignChannelsDialog::refreshImageList);
    connect(m_applyBtn, &QPushButton::clicked, this, &AlignChannelsDialog::onApply);
    connect(closeBtn,   &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addWidget(refreshBtn);
    btnLayout->addStretch(1);
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(m_applyBtn);

    mainLayout->addLayout(btnLayout);

    refreshImageList();
}

// =============================================================================
// Public interface
// =============================================================================

void AlignChannelsDialog::refreshImageList()
{
    populateCombo(m_refCombo);

    for (int i = 0; i < kMaxTargets; ++i)
    {
        populateCombo(m_targets[i].combo);

        // Pre-select a different entry from the reference to reduce manual work.
        if (m_targets[i].combo->count() > i + 1)
            m_targets[i].combo->setCurrentIndex(i + 1);
    }
}

// =============================================================================
// Private helpers
// =============================================================================

void AlignChannelsDialog::populateCombo(QComboBox* combo)
{
    combo->clear();

    if (!m_mainWindow)
        return;

    ImageViewer* currentViewer = m_mainWindow->getCurrentViewer();
    if (!currentViewer)
        return;

    // Walk all CustomMdiSubWindow siblings of the current viewer's window.
    const auto subWindows = currentViewer->window()->findChildren<CustomMdiSubWindow*>();

    for (CustomMdiSubWindow* csw : subWindows)
    {
        ImageViewer* v = csw->viewer();
        if (!v || !v->getBuffer().isValid())
            continue;

        combo->addItem(csw->windowTitle(),
                       QVariant::fromValue(reinterpret_cast<quintptr>(v)));
    }
}

ImageViewer* AlignChannelsDialog::viewerFromCombo(QComboBox* combo) const
{
    const QVariant data = combo->currentData();
    if (!data.isValid())
        return nullptr;

    return reinterpret_cast<ImageViewer*>(data.value<quintptr>());
}

// =============================================================================
// Private slots
// =============================================================================

void AlignChannelsDialog::onApply()
{
    // -------------------------------------------------------------------------
    // Validate the reference selection.
    // -------------------------------------------------------------------------
    ImageViewer* refViewer = viewerFromCombo(m_refCombo);

    if (!refViewer)
    {
        QMessageBox::warning(this, tr("Align Channels"),
                             tr("Select a valid reference image."));
        return;
    }

    const ImageBuffer& refBuf = refViewer->getBuffer();

    if (!refBuf.isValid())
    {
        QMessageBox::warning(this, tr("Align Channels"),
                             tr("The reference image is empty."));
        return;
    }

    // -------------------------------------------------------------------------
    // Collect enabled target viewers (excluding the reference itself).
    // -------------------------------------------------------------------------
    struct Target { ImageViewer* viewer; };
    QVector<Target> targets;

    for (int i = 0; i < kMaxTargets; ++i)
    {
        if (!m_targets[i].check->isChecked())
            continue;

        ImageViewer* v = viewerFromCombo(m_targets[i].combo);

        if (!v || v == refViewer || !v->getBuffer().isValid())
            continue;

        targets.append({ v });
    }

    if (targets.isEmpty())
    {
        QMessageBox::warning(this, tr("Align Channels"),
                             tr("Select at least one image to align, different from the reference."));
        return;
    }

    // -------------------------------------------------------------------------
    // Configure the registration engine parameters.
    // -------------------------------------------------------------------------
    RegistrationParams params;
    params.allowRotation       = m_allowRotationCheck->isChecked();
    params.allowScale          = m_allowScaleCheck->isChecked();
    params.detectionThreshold  = static_cast<float>(m_thresholdSpin->value());
    params.highPrecision       = true;

    // -------------------------------------------------------------------------
    // Process each target image sequentially.
    // -------------------------------------------------------------------------
    m_applyBtn->setEnabled(false);

    if (m_mainWindow)
        m_mainWindow->startLongProcess();

    int successCount = 0;
    const int totalCount = targets.size();

    for (int idx = 0; idx < totalCount; ++idx)
    {
        ImageViewer* tgtViewer = targets[idx].viewer;

        m_statusLabel->setText(tr("Aligning image %1 / %2...")
                                   .arg(idx + 1).arg(totalCount));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        const ImageBuffer& tgtBuf = tgtViewer->getBuffer();

        // Create a fresh engine instance for each image (stateless by design).
        RegistrationEngine engine;
        engine.setParams(params);

        // Route engine log output to the status label and the main window log.
        connect(&engine, &RegistrationEngine::logMessage,
                this, [this](const QString& msg, const QString& /*color*/)
                {
                    m_statusLabel->setText(msg);
                    if (m_mainWindow)
                        m_mainWindow->logMessage(msg, 0, false);
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                });

        RegistrationResult result = engine.registerImage(tgtBuf, refBuf);

        if (!result.success)
        {
            const QString errMsg = tr("Image %1: registration failed - %2")
                                       .arg(idx + 1).arg(result.error);
            m_statusLabel->setText(errMsg);
            if (m_mainWindow)
                m_mainWindow->logMessage(errMsg, 1, false);
            continue;
        }

        // ---------------------------------------------------------------------
        // Build the homography matrix from the registration result.
        // ---------------------------------------------------------------------
        cv::Mat H(3, 3, CV_64F);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                H.at<double>(r, c) = result.transform.H[r][c];

        const int w  = tgtBuf.width();
        const int h  = tgtBuf.height();
        const int ch = tgtBuf.channels();

        ImageBuffer warped(w, h, ch);
        warped.setMetadata(tgtBuf.metadata());

        {
            cv::Mat src(h, w, CV_32FC(ch),
                        const_cast<void*>(static_cast<const void*>(tgtBuf.data().data())));
            cv::Mat dst(h, w, CV_32FC(ch), warped.data().data());

            cv::warpPerspective(src, dst, H, dst.size(),
                                cv::INTER_LINEAR,
                                cv::BORDER_CONSTANT,
                                cv::Scalar(0));

            // Zero out border pixels where the warp mapped from outside the
            // source frame, identified by a warped unity mask falling below 1.
            cv::Mat mask(h, w, CV_32FC1, cv::Scalar(1.0f));
            cv::Mat warpedMask(h, w, CV_32FC1);
            cv::warpPerspective(mask, warpedMask, H, dst.size(),
                                cv::INTER_LINEAR,
                                cv::BORDER_CONSTANT,
                                cv::Scalar(0.0f));

            float*       dstData  = reinterpret_cast<float*>(dst.data);
            const float* maskData = reinterpret_cast<const float*>(warpedMask.data);
            const int    totalPx  = w * h;

            for (int p = 0; p < totalPx; ++p)
            {
                if (maskData[p] < 0.999f)
                {
                    for (int c = 0; c < ch; ++c)
                        dstData[p * ch + c] = 0.0f;
                }
            }
        }

        tgtViewer->pushUndo(tr("Align Channels"));
        tgtViewer->setBuffer(warped, tgtViewer->windowTitle(), true);

        const QString okMsg = tr("Image %1: aligned "
                                 "(shift=%.1f,%.1f  rot=%.2f deg  %3 stars)")
                                  .arg(idx + 1)
                                  .arg(result.transform.shiftX,  0, 'f', 1)
                                  .arg(result.transform.shiftY,  0, 'f', 1)
                                  .arg(result.transform.rotation * 180.0 / M_PI, 0, 'f', 2)
                                  .arg(result.starsMatched);

        m_statusLabel->setText(okMsg);
        if (m_mainWindow)
            m_mainWindow->logMessage(okMsg, 0, false);

        ++successCount;
    }

    // -------------------------------------------------------------------------
    // Finalise and report overall result.
    // -------------------------------------------------------------------------
    if (m_mainWindow)
        m_mainWindow->endLongProcess();

    m_applyBtn->setEnabled(true);

    const QString summary = (successCount == totalCount)
        ? tr("Completed: %1/%2 images aligned successfully.")
              .arg(successCount).arg(totalCount)
        : tr("Completed with errors: %1/%2 images aligned.")
              .arg(successCount).arg(totalCount);

    m_statusLabel->setText(summary);

    if (m_mainWindow)
        m_mainWindow->logMessage(summary, successCount == totalCount ? 0 : 1);
}