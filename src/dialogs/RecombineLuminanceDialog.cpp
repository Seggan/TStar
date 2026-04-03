#include "RecombineLuminanceDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QMessageBox>
#include <QMdiSubWindow>

#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../widgets/CustomMdiSubWindow.h"
#include "../algos/ChannelOps.h"
#include "../ImageViewer.h"

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------

RecombineLuminanceDialog::RecombineLuminanceDialog(QWidget* parent)
    : DialogBase(parent, tr("Recombine Luminance"))
{
    m_mainWindow = getCallbacks();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Source image selection
    QHBoxLayout* srcLayout = new QHBoxLayout();
    srcLayout->addWidget(new QLabel(tr("Luminance Source:")));
    m_sourceCombo = new QComboBox();
    srcLayout->addWidget(m_sourceCombo);
    mainLayout->addLayout(srcLayout);

    // Color space selection
    QHBoxLayout* csLayout = new QHBoxLayout();
    csLayout->addWidget(new QLabel(tr("Color Space:")));
    m_colorSpaceCombo = new QComboBox();
    m_colorSpaceCombo->addItem(tr("HSL (Hue-Saturation-Lightness)"), static_cast<int>(ChannelOps::ColorSpaceMode::HSL));
    m_colorSpaceCombo->addItem(tr("HSV (Hue-Saturation-Value)"),     static_cast<int>(ChannelOps::ColorSpaceMode::HSV));
    m_colorSpaceCombo->addItem(tr("CIE L*a*b*"),                     static_cast<int>(ChannelOps::ColorSpaceMode::CIELAB));
    csLayout->addWidget(m_colorSpaceCombo);
    mainLayout->addLayout(csLayout);

    // Blend factor slider
    QHBoxLayout* blendLayout = new QHBoxLayout();
    blendLayout->addWidget(new QLabel(tr("Blend:")));
    m_blendSlider = new QSlider(Qt::Horizontal);
    m_blendSlider->setRange(0, 100);
    m_blendSlider->setValue(100);
    m_blendLabel = new QLabel("100%");
    blendLayout->addWidget(m_blendSlider);
    blendLayout->addWidget(m_blendLabel);
    mainLayout->addLayout(blendLayout);

    // Dialog action buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_blendSlider, &QSlider::valueChanged,    this, &RecombineLuminanceDialog::updateBlendLabel);
    connect(applyBtn,      &QPushButton::clicked,     this, &RecombineLuminanceDialog::onApply);
    connect(closeBtn,      &QPushButton::clicked,     this, &RecombineLuminanceDialog::reject);

    refreshSourceList();
}

// ----------------------------------------------------------------------------
// Public Methods
// ----------------------------------------------------------------------------

void RecombineLuminanceDialog::refreshSourceList()
{
    m_sourceCombo->clear();
    if (!m_mainWindow)
        return;

    ImageViewer* current = m_mainWindow->getCurrentViewer();
    if (!current)
        return;

    // Enumerate all open MDI sub-windows and populate the combo box,
    // excluding the current (target) viewer from the list.
    const auto list = current->window()->findChildren<CustomMdiSubWindow*>();
    for (auto* win : list)
    {
        ImageViewer* v = win->viewer();
        if (v && v != current)
            m_sourceCombo->addItem(win->windowTitle(), QVariant::fromValue(static_cast<void*>(v)));
    }
}

// ----------------------------------------------------------------------------
// Private Slots
// ----------------------------------------------------------------------------

void RecombineLuminanceDialog::updateBlendLabel(int val)
{
    m_blendLabel->setText(QString("%1%").arg(val));
}

void RecombineLuminanceDialog::onApply()
{
    ImageViewer* target = m_mainWindow ? m_mainWindow->getCurrentViewer() : nullptr;
    if (!target)
        return;

    // Validate source selection
    const int idx = m_sourceCombo->currentIndex();
    if (idx < 0)
    {
        QMessageBox::warning(this, tr("No Source"), tr("Please select a source luminance image."));
        return;
    }

    ImageViewer* srcViewer = static_cast<ImageViewer*>(m_sourceCombo->itemData(idx).value<void*>());
    if (!srcViewer)
        return;

    // Validate that the source image is single-channel (mono)
    if (srcViewer->getBuffer().channels() != 1)
    {
        QMessageBox::warning(this, tr("Invalid Source"),
            tr("The source image must be a single-channel (mono) luminance image."));
        return;
    }

    // Validate that the target image is RGB
    if (target->getBuffer().channels() < 3)
    {
        QMessageBox::warning(this, tr("Invalid Target"),
            tr("The target image must be a color (RGB) image."));
        return;
    }

    // Save the original buffer before any modification to support mask blending later
    const ImageBuffer origBuf = target->getBuffer();

    if (m_mainWindow)
    {
        m_mainWindow->logMessage(tr("Recombining luminance..."), 1);
        m_mainWindow->startLongProcess();
    }

    // Push undo state before modifying the buffer
    target->pushUndo(tr("Recombine Luminance"));

    const ChannelOps::ColorSpaceMode csMode =
        static_cast<ChannelOps::ColorSpaceMode>(m_colorSpaceCombo->currentData().toInt());
    const float blend = m_blendSlider->value() / 100.0f;

    const bool ok = ChannelOps::recombineLuminance(target->getBuffer(), srcViewer->getBuffer(), csMode, blend);

    // If the target had a mask active, blend the processed result back with the original
    if (ok && origBuf.hasMask())
        target->getBuffer().blendResult(origBuf);

    if (m_mainWindow)
        m_mainWindow->endLongProcess();

    if (ok)
    {
        target->refresh();
        if (m_mainWindow)
            m_mainWindow->logMessage(tr("Luminance recombination completed."), 1);
        accept();
    }
    else
    {
        // Revert to the state before the failed operation
        target->undo();
        QMessageBox::critical(this, tr("Error"),
            tr("Recombination failed. Check that image dimensions match and the source is a valid luminance image."));
    }
}