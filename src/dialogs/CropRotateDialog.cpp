#include "CropRotateDialog.h"
#include "MainWindowCallbacks.h"
#include "../ImageViewer.h"
#include "../widgets/CustomMdiSubWindow.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

// ============================================================================
// Construction
// ============================================================================

CropRotateDialog::CropRotateDialog(QWidget* parent)
    : DialogBase(parent, tr("Rotate & Crop Tool"), 350, 150)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(11, 11, 11, 5);

    // --- Rotation angle ---
    QHBoxLayout* spinLayout = new QHBoxLayout();
    spinLayout->addWidget(new QLabel(tr("Rotation (deg):"), this));

    m_angleSpin = new QDoubleSpinBox(this);
    m_angleSpin->setRange(-180.0, 180.0);
    m_angleSpin->setSingleStep(0.1);
    spinLayout->addWidget(m_angleSpin);
    mainLayout->addLayout(spinLayout);

    // Slider uses 10x integer range to provide 0.1-degree precision.
    m_angleSlider = new QSlider(Qt::Horizontal, this);
    m_angleSlider->setRange(-1800, 1800);
    mainLayout->addWidget(m_angleSlider);

    // --- Aspect ratio constraint ---
    QHBoxLayout* arLayout = new QHBoxLayout();
    arLayout->addWidget(new QLabel(tr("Aspect Ratio:"), this));

    m_aspectCombo = new QComboBox(this);
    m_aspectCombo->addItem(tr("Free"),         -1.0f);
    m_aspectCombo->addItem(tr("1:1 (Square)"),  1.0f);
    m_aspectCombo->addItem("3:2",               1.5f);
    m_aspectCombo->addItem("2:3",               0.6666f);
    m_aspectCombo->addItem("4:3",               1.3333f);
    m_aspectCombo->addItem("3:4",               0.75f);
    m_aspectCombo->addItem("16:9",              1.7777f);
    m_aspectCombo->addItem("9:16",              0.5625f);
    arLayout->addWidget(m_aspectCombo);
    mainLayout->addLayout(arLayout);

    // --- Action buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn  = new QPushButton(tr("Apply"),      this);
    QPushButton* batchBtn  = new QPushButton(tr("Batch Crop"), this);
    QPushButton* closeBtn  = new QPushButton(tr("Close"),      this);

    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(batchBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);

    // --- Signal/slot connections ---

    // Keep spin box and slider synchronized without re-entrancy.
    connect(m_angleSlider, &QSlider::valueChanged, this, [this](int val) {
        m_angleSpin->blockSignals(true);
        m_angleSpin->setValue(val / 10.0);
        m_angleSpin->blockSignals(false);
        onRotationChanged();
    });

    connect(m_angleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double val) {
        m_angleSlider->blockSignals(true);
        m_angleSlider->setValue(static_cast<int>(val * 10));
        m_angleSlider->blockSignals(false);
        onRotationChanged();
    });

    connect(applyBtn,  &QPushButton::clicked,
            this, &CropRotateDialog::onApply);
    connect(batchBtn,  &QPushButton::clicked,
            this, &CropRotateDialog::onBatchApply);
    connect(closeBtn,  &QPushButton::clicked,
            this, &CropRotateDialog::reject);
    connect(m_aspectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CropRotateDialog::onRatioChanged);

    // Ensure the dialog appears on screen (guards against off-screen placement on macOS).
    if (parentWidget())
        move(parentWidget()->window()->geometry().center() - rect().center());
}

CropRotateDialog::~CropRotateDialog()
{
    if (m_viewer)
        m_viewer->setCropMode(false);
}

// ============================================================================
// Public interface
// ============================================================================

void CropRotateDialog::setViewer(ImageViewer* v)
{
    if (m_viewer == v) return;

    // Deactivate crop mode on the previous viewer without altering its parameters,
    // preserving any crop rectangle that was in progress.
    if (m_viewer)
        m_viewer->setCropMode(false);

    m_viewer = v;

    if (m_viewer)
    {
        m_viewer->setCropMode(true);
        m_viewer->setAspectRatio(m_aspectCombo->currentData().toFloat());
        m_viewer->setCropAngle(static_cast<float>(m_angleSpin->value()));
    }
}

// ============================================================================
// Slots
// ============================================================================

void CropRotateDialog::onRotationChanged()
{
    if (m_viewer)
        m_viewer->setCropAngle(static_cast<float>(m_angleSpin->value()));
}

void CropRotateDialog::onRatioChanged()
{
    if (m_viewer)
        m_viewer->setAspectRatio(m_aspectCombo->currentData().toFloat());
}

void CropRotateDialog::onApply()
{
    if (!m_viewer) return;

    float cx, cy, w, h, angle;
    m_viewer->getCropState(cx, cy, w, h, angle);

    if (w <= 0 || h <= 0)
    {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Please draw a crop rectangle first."));
        return;
    }

    m_viewer->pushUndo(tr("Crop"));
    m_viewer->getBuffer().cropRotated(cx, cy, w, h, angle);

    // Catalog star coordinates are stored in sky (RA/Dec) space and remain
    // valid after a crop; no position update is required.

    m_viewer->refreshDisplay(false);
    m_viewer->fitToWindow();

    if (MainWindowCallbacks* cb = getCallbacks())
        cb->logMessage(tr("Crop applied."), 1, true);

    // Reset the rotation control and re-arm crop mode for the next operation.
    m_angleSpin->setValue(0);
    m_viewer->setCropMode(false);
    m_viewer->setCropMode(true);
}

void CropRotateDialog::onBatchApply()
{
    if (!m_viewer) return;

    float cx, cy, w, h, angle;
    m_viewer->getCropState(cx, cy, w, h, angle);

    if (w <= 0 || h <= 0)
    {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Please draw a crop rectangle first."));
        return;
    }

    // Locate the MDI area by traversing the parent hierarchy.
    QMdiArea* mdiArea = nullptr;
    QWidget*  p       = m_viewer->parentWidget();
    while (p)
    {
        if (QMdiSubWindow* sw = qobject_cast<QMdiSubWindow*>(p))
        {
            mdiArea = sw->mdiArea();
            break;
        }
        p = p->parentWidget();
    }

    if (!mdiArea) return;

    // Collect all valid, non-tool image windows.
    QList<CustomMdiSubWindow*> targetWindows;
    for (auto* sub : mdiArea->subWindowList())
    {
        auto* csw = qobject_cast<CustomMdiSubWindow*>(sub);
        if (csw && !csw->isToolWindow() && csw->viewer())
            targetWindows.append(csw);
    }

    if (targetWindows.size() > 1)
    {
        if (QMessageBox::question(
                this, tr("Batch Crop"),
                tr("Apply this crop to %1 images?").arg(targetWindows.size()),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
    }

    for (auto* csw : targetWindows)
    {
        if (ImageViewer* v = csw->viewer())
        {
            v->pushUndo(tr("Crop"));
            v->getBuffer().cropRotated(cx, cy, w, h, angle);
            v->refreshDisplay(false);
            v->fitToWindow();
            v->setCropMode(false);
        }
    }

    if (MainWindowCallbacks* cb = getCallbacks())
    {
        cb->logMessage(
            tr("Batch Crop applied to %1 images.").arg(targetWindows.size()), 1, true);
        cb->refreshHeaderPanel();
    }

    m_angleSpin->setValue(0);
    m_viewer->setCropMode(true);
}