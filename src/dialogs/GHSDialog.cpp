#include "GHSDialog.h"
#include "widgets/HistogramWidget.h"
#include "algos/GHSAlgo.h"
#include "MainWindowCallbacks.h"
#include "../ImageViewer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// ===========================================================================
// Anonymous-namespace helpers
// ===========================================================================

namespace {

/** @brief Convert a discrete zoom level [1..100] to an exponential scale factor. */
double histogramZoomScaleFromLevel(int level)
{
    const int    clamped = std::clamp(level, 1, 100);
    const double t       = static_cast<double>(clamped - 1) / 99.0;
    return std::exp(std::log(100.0) * t);
}

/** @brief Format a zoom level as a human-readable label (e.g. "2.50x"). */
QString histogramZoomLabelFromLevel(int level)
{
    return QString::number(histogramZoomScaleFromLevel(level), 'f', 2) + "x";
}

/** @brief Shared dark-theme stylesheet for combo boxes. */
const char* kComboStyle =
    "QComboBox { color: white; background-color: #2a2a2a; "
    "  border: 1px solid #555; padding: 2px; border-radius: 3px; }"
    "QComboBox:focus { border: 2px solid #4a9eff; }"
    "QComboBox::drop-down { border: none; }"
    "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
    "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
    "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
    "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
    "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }";

} // anonymous namespace

// ===========================================================================
// Construction / Destruction
// ===========================================================================

GHSDialog::GHSDialog(QWidget* parent)
    : DialogBase(parent, tr("Generalized Hyperbolic Stretch (GHT)"), 440, 600)
    , m_previewPending(false)
    , m_activeViewer(nullptr)
    , m_applied(false)
{
    m_interactionEnabled = false;
    setModal(false);
    setupUI();
    connectSignals();
}

GHSDialog::~GHSDialog()
{
    if (m_activeViewer) {
        // Remove any active preview LUT.
        std::vector<std::vector<float>> emptyLUT;
        m_activeViewer->setPreviewLUT(emptyLUT);

        // Restore the buffer to its state at dialog open if nothing was applied.
        if (!m_applied) {
            m_activeViewer->setBuffer(m_bufferAtOpening,
                                      m_activeViewer->windowTitle(), true);
        }

        // Ensure the viewer interaction mode is clean.
        m_activeViewer->setInteractionMode(ImageViewer::Mode_PanZoom);
        m_activeViewer->setCursor(Qt::ArrowCursor);
    }
}

// ===========================================================================
// reject() -- restore original state on Cancel / Esc
// ===========================================================================

void GHSDialog::reject()
{
    if (m_activeViewer) {
        // Disconnect all viewer signals before restoring state.
        m_activeViewer->setRegionSelectedCallback(nullptr);
        disconnect(m_activeViewer, &ImageViewer::destroyed,    this, nullptr);
        disconnect(m_activeViewer, &ImageViewer::bufferChanged, this, nullptr);

        // Clear the preview LUT before restoring the buffer to avoid
        // baking the preview transform into the display.
        std::vector<std::vector<float>> emptyLUT;
        m_activeViewer->setPreviewLUT(emptyLUT);

        m_selfUpdating = true;
        if (!m_applied) {
            m_activeViewer->setBuffer(m_bufferAtOpening,
                                      m_activeViewer->windowTitle(), true);
        } else {
            m_activeViewer->setBuffer(m_originalBuffer,
                                      m_activeViewer->windowTitle(), true);
        }

        m_activeViewer->setInteractionMode(ImageViewer::Mode_PanZoom);
        m_activeViewer->setCursor(Qt::ArrowCursor);
        m_selfUpdating = false;

        m_activeViewer = nullptr;
    }

    QDialog::reject();
}

// ===========================================================================
// setupUI
// ===========================================================================

void GHSDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);

    // -- Histogram zoom toolbar ---------------------------------------------
    QHBoxLayout* histoToolbar = new QHBoxLayout();

    histoToolbar->addWidget(new QLabel(tr("Zoom:")));

    m_zoomOutBtn = new QToolButton();
    m_zoomOutBtn->setText("-");
    m_zoomOutBtn->setFixedWidth(24);
    m_zoomOutBtn->setToolTip(tr("Zoom Out"));
    m_zoomOutBtn->setAutoRepeat(true);
    m_zoomOutBtn->setAutoRepeatDelay(400);
    m_zoomOutBtn->setAutoRepeatInterval(50);
    histoToolbar->addWidget(m_zoomOutBtn);

    m_zoomLabel = new QLabel(histogramZoomLabelFromLevel(m_zoomLevel));
    m_zoomLabel->setMinimumWidth(35);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    histoToolbar->addWidget(m_zoomLabel);

    m_zoomInBtn = new QToolButton();
    m_zoomInBtn->setText("+");
    m_zoomInBtn->setFixedWidth(24);
    m_zoomInBtn->setToolTip(tr("Zoom In"));
    m_zoomInBtn->setAutoRepeat(true);
    m_zoomInBtn->setAutoRepeatDelay(400);
    m_zoomInBtn->setAutoRepeatInterval(50);
    histoToolbar->addWidget(m_zoomInBtn);

    m_zoomResetBtn = new QToolButton();
    m_zoomResetBtn->setText("1:1");
    m_zoomResetBtn->setToolTip(tr("Reset zoom to 1"));
    histoToolbar->addWidget(m_zoomResetBtn);

    histoToolbar->addStretch();

    m_logScaleCheck = new QCheckBox(tr("Logarithmic"));
    m_logScaleCheck->setChecked(false);
    m_logScaleCheck->setToolTip(tr("Logarithmic histogram scale"));
    histoToolbar->addWidget(m_logScaleCheck);

    mainLayout->addLayout(histoToolbar);

    // -- Histogram scroll area ----------------------------------------------
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(
        "QScrollArea { border: none; background: #1a1a1a; }"
        "QScrollBar:horizontal { border: none; background: #2b2b2b; "
        "  height: 8px; margin: 0px; border-radius: 4px; }"
        "QScrollBar::handle:horizontal { background: #555; min-width: 20px; border-radius: 4px; }"
        "QScrollBar::handle:horizontal:hover { background: #666; }"
        "QScrollBar::add-line:horizontal { width: 0px; }"
        "QScrollBar::sub-line:horizontal { width: 0px; }");

    m_histWidget = new HistogramWidget(this);
    m_histWidget->setMinimumHeight(100);
    m_histWidget->setLogScale(false);

    m_scrollArea->setWidget(m_histWidget);
    mainLayout->addWidget(m_scrollArea, 1);

    // Separate scrollbar beneath the histogram for fine-grained panning.
    m_histScrollBar = new QScrollBar(Qt::Horizontal);
    m_histScrollBar->setVisible(false);
    m_histScrollBar->setFixedHeight(10);
    m_histScrollBar->setStyleSheet(
        "QScrollBar:horizontal { border: none; background: #2b2b2b; "
        "  height: 10px; margin: 0px; border-radius: 5px; }"
        "QScrollBar::handle:horizontal { background: #555; min-width: 20px; border-radius: 5px; }"
        "QScrollBar::handle:horizontal:hover { background: #666; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }");
    mainLayout->addWidget(m_histScrollBar);

    // Synchronize the external scrollbar with the scroll area.
    connect(m_histScrollBar, &QScrollBar::valueChanged, [this](int v) {
        m_scrollArea->horizontalScrollBar()->setValue(v);
    });
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::rangeChanged,
            [this](int min, int max) { m_histScrollBar->setRange(min, max); });
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged,
            [this](int v) { m_histScrollBar->setValue(v); });

    // -- Channel / display toolbar ------------------------------------------
    QHBoxLayout* channelToolbar = new QHBoxLayout();

    auto createToggle = [&](const QString& text,
                            const QString& color,
                            const QString& tip) -> QToolButton* {
        QToolButton* btn = new QToolButton();
        btn->setText(text);
        btn->setCheckable(true);
        btn->setChecked(true);
        btn->setFixedSize(30, 30);
        btn->setStyleSheet(
            QString("QToolButton:checked { background-color: %1; "
                    "color: black; font-weight: bold; }").arg(color));
        btn->setToolTip(tip);
        return btn;
    };

    m_redBtn   = createToggle("R", "#ff0000", tr("Toggle Red channel"));
    m_greenBtn = createToggle("G", "#00ff00", tr("Toggle Green channel"));
    m_blueBtn  = createToggle("B", "#0000ff", tr("Toggle Blue channel"));

    channelToolbar->addWidget(m_redBtn);
    channelToolbar->addWidget(m_greenBtn);
    channelToolbar->addWidget(m_blueBtn);
    channelToolbar->addSpacing(10);

    m_gridBtn = new QToolButton();
    m_gridBtn->setText(tr("Grid"));
    m_gridBtn->setCheckable(true);
    m_gridBtn->setChecked(true);
    m_gridBtn->setToolTip(tr("Show grid overlay"));
    channelToolbar->addWidget(m_gridBtn);

    m_curveBtn = new QToolButton();
    m_curveBtn->setText(tr("Curve"));
    m_curveBtn->setCheckable(true);
    m_curveBtn->setChecked(true);
    m_curveBtn->setToolTip(tr("Show transfer curve"));
    channelToolbar->addWidget(m_curveBtn);

    channelToolbar->addStretch();
    mainLayout->addLayout(channelToolbar);

    // -- Mode selection group -----------------------------------------------
    QGroupBox*   modeGroup  = new QGroupBox(tr("Stretch Mode"));
    QVBoxLayout* modeLayout = new QVBoxLayout(modeGroup);

    m_modeCombo = new QComboBox();
    m_modeCombo->addItem(tr("Generalized Hyperbolic Stretch"), ImageBuffer::GHS_GeneralizedHyperbolic);
    m_modeCombo->addItem(tr("Inverse GHS"),                    ImageBuffer::GHS_InverseGeneralizedHyperbolic);
    m_modeCombo->addItem(tr("ArcSinh Stretch"),                ImageBuffer::GHS_ArcSinh);
    m_modeCombo->addItem(tr("Inverse ArcSinh"),                ImageBuffer::GHS_InverseArcSinh);
    m_modeCombo->setStyleSheet(kComboStyle);
    modeLayout->addWidget(m_modeCombo);

    m_colorCombo = new QComboBox();
    m_colorCombo->addItem(tr("RGB (Independent)"),         ImageBuffer::GHS_Independent);
    m_colorCombo->addItem(tr("Human Weighted Luminance"),  ImageBuffer::GHS_WeightedLuminance);
    m_colorCombo->addItem(tr("Even Weighted Luminance"),   ImageBuffer::GHS_EvenWeightedLuminance);
    m_colorCombo->addItem(tr("Saturation"),                ImageBuffer::GHS_Saturation);
    m_colorCombo->setStyleSheet(kComboStyle);
    modeLayout->addWidget(m_colorCombo);

    // -- Clipping statistics ------------------------------------------------
    QHBoxLayout* clipLayout = new QHBoxLayout();
    m_lowClipLabel  = new QLabel(tr("Low: 0.00%"));
    m_highClipLabel = new QLabel(tr("High: 0.00%"));
    m_lowClipLabel->setStyleSheet("color: #ff8888; margin-right: 10px;");
    m_highClipLabel->setStyleSheet("color: #8888ff;");
    clipLayout->addWidget(m_lowClipLabel);
    clipLayout->addWidget(m_highClipLabel);
    clipLayout->addStretch();
    mainLayout->addLayout(clipLayout);

    mainLayout->addWidget(modeGroup);

    // -- Parameters grid ----------------------------------------------------
    QGroupBox*   paramsGroup = new QGroupBox(tr("Parameters"));
    QGridLayout* paramsGrid  = new QGridLayout(paramsGroup);
    paramsGrid->setColumnStretch(1, 1);

    // Helper: create a row with a label, horizontal slider, and spin box.
    auto makeSliderRow = [&](int row, const QString& label,
                             QDoubleSpinBox*& spin, QSlider*& slider,
                             double min, double max, double val,
                             double step, int decimals,
                             int sliderMax = 10000)
    {
        paramsGrid->addWidget(new QLabel(label), row, 0);

        slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, sliderMax);
        const double t = (val - min) / (max - min);
        slider->setValue(static_cast<int>(t * sliderMax));
        paramsGrid->addWidget(slider, row, 1);

        spin = new QDoubleSpinBox();
        spin->setRange(min, max);
        spin->setValue(val);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setMinimumWidth(90);
        paramsGrid->addWidget(spin, row, 2);
    };

    makeSliderRow(0, tr("Stretch (D):"),     m_dSpin,  m_dSlider,  0.0, 10.0, 0.0, 0.01,   2);
    makeSliderRow(1, tr("Intensity (B):"),   m_bSpin,  m_bSlider, -5.0, 15.0, 0.0, 0.01,   2);

    // SP: Symmetry Point has a custom layout with picker button space.
    QHBoxLayout* spRow = new QHBoxLayout();
    paramsGrid->addWidget(new QLabel(tr("Symmetry (SP):")), 2, 0);

    m_spSlider = new QSlider(Qt::Horizontal);
    m_spSlider->setRange(0, 10000);
    m_spSlider->setValue(0);
    spRow->addWidget(m_spSlider);

    m_spSpin = new QDoubleSpinBox();
    m_spSpin->setRange(0.0, 1.0);
    m_spSpin->setValue(0.0);
    m_spSpin->setSingleStep(0.001);
    m_spSpin->setDecimals(4);
    m_spSpin->setMinimumWidth(90);
    spRow->addWidget(m_spSpin);

    QWidget* spWidget = new QWidget();
    spWidget->setLayout(spRow);
    paramsGrid->addWidget(spWidget, 2, 1, 1, 2);

    makeSliderRow(3, tr("Shadow (LP):"),       m_lpSpin, m_lpSlider, 0.0, 1.0, 0.0, 0.001,  4);
    makeSliderRow(4, tr("Highlight (HP):"),    m_hpSpin, m_hpSlider, 0.0, 1.0, 1.0, 0.001,  4);
    makeSliderRow(5, tr("Black Point (BP):"),  m_bpSpin, m_bpSlider, 0.0, 0.3, 0.0, 0.0001, 5);

    mainLayout->addWidget(paramsGroup);

    // -- Clip mode row ------------------------------------------------------
    QHBoxLayout* clipRow = new QHBoxLayout();
    clipRow->addWidget(new QLabel(tr("Clip Mode:")));

    m_clipModeCombo = new QComboBox();
    m_clipModeCombo->addItem(tr("Clip"),           ImageBuffer::GHS_Clip);
    m_clipModeCombo->addItem(tr("Rescale"),        ImageBuffer::GHS_Rescale);
    m_clipModeCombo->addItem(tr("RGB Blend"),      ImageBuffer::GHS_ClipRGBBlend);
    m_clipModeCombo->addItem(tr("Global rescale"), ImageBuffer::GHS_RescaleGlobal);
    m_clipModeCombo->setToolTip(tr("How to handle values outside 0-1 range"));
    m_clipModeCombo->setStyleSheet(kComboStyle);
    clipRow->addWidget(m_clipModeCombo);
    clipRow->addStretch();

    m_previewCheck = new QCheckBox(tr("Preview"));
    m_previewCheck->setChecked(true);
    m_previewCheck->setToolTip(tr("Enable live preview"));
    clipRow->addWidget(m_previewCheck);

    mainLayout->addLayout(clipRow);

    // -- Bottom button row --------------------------------------------------
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* resetBtn  = new QPushButton(tr("Reset"));
    QPushButton* applyBtn  = new QPushButton(tr("Apply"));
    applyBtn->setStyleSheet("QPushButton { background-color: #3a7d44; }");

    btnLayout->addWidget(resetBtn);

    QLabel* copyLabel = new QLabel(tr("(C) 2026 Mike Cranfield"));
    copyLabel->setStyleSheet("color: #888; font-size: 10px; margin-left: 10px;");
    btnLayout->addWidget(copyLabel);

    btnLayout->addStretch();
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);

    connect(resetBtn, &QPushButton::clicked, this, &GHSDialog::onReset);
    connect(applyBtn, &QPushButton::clicked, this, &GHSDialog::onApply);

    // -- Preview throttle timer ---------------------------------------------
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(100);

    connect(m_previewTimer, &QTimer::timeout, this, [this]() {
        if (m_previewPending) {
            m_previewPending = false;
            onPreviewTrigger();
        }
    });

    // Center on parent if available.
    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

// ===========================================================================
// connectSignals
// ===========================================================================

void GHSDialog::connectSignals()
{
    // Helper: bidirectionally synchronize a slider and a spin box.
    // The spin box triggers a full preview; the slider only updates the
    // curve in real time and defers the expensive preview to release.
    auto syncSliderSpin = [this](QSlider* slider, QDoubleSpinBox* spin,
                                double min, double max, int sliderMax)
    {
        // Spin box value changed -> update slider, trigger full preview.
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [=](double d) {
            const double ratio = (d - min) / (max - min);
            const int sVal = static_cast<int>(ratio * sliderMax);
            if (slider->value() != sVal) {
                slider->blockSignals(true);
                slider->setValue(sVal);
                slider->blockSignals(false);
            }
            onPreviewTrigger();
        });

        // Slider drag -> update spin box and curve only (no preview).
        connect(slider, &QSlider::valueChanged, [=](int i) {
            const double ratio = static_cast<double>(i) / sliderMax;
            const double d     = min + ratio * (max - min);
            if (std::abs(spin->value() - d) > 1e-6) {
                spin->blockSignals(true);
                spin->setValue(d);
                spin->blockSignals(false);
            }
            onValueChange();
            updateHistogram();
        });

        // Slider release -> trigger the expensive full preview.
        connect(slider, &QSlider::sliderReleased, this, &GHSDialog::onPreviewTrigger);
    };

    syncSliderSpin(m_dSlider,  m_dSpin,  0.0,  10.0, 10000);
    syncSliderSpin(m_bSlider,  m_bSpin, -5.0,  15.0, 10000);
    syncSliderSpin(m_lpSlider, m_lpSpin, 0.0,   1.0, 10000);
    syncSliderSpin(m_hpSlider, m_hpSpin, 0.0,   1.0, 10000);
    syncSliderSpin(m_bpSlider, m_bpSpin, 0.0,   0.3, 10000);

    // SP uses a fixed 0..1 range.
    connect(m_spSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double d) {
        const int sVal = static_cast<int>(d * 10000);
        if (m_spSlider->value() != sVal) {
            m_spSlider->blockSignals(true);
            m_spSlider->setValue(sVal);
            m_spSlider->blockSignals(false);
        }
        onPreviewTrigger();
    });

    connect(m_spSlider, &QSlider::valueChanged, [this](int i) {
        const double d = static_cast<double>(i) / 10000.0;
        if (std::abs(m_spSpin->value() - d) > 1e-6) {
            m_spSpin->blockSignals(true);
            m_spSpin->setValue(d);
            m_spSpin->blockSignals(false);
        }
        onValueChange();
    });
    connect(m_spSlider, &QSlider::sliderReleased, this, &GHSDialog::onPreviewTrigger);

    // Mode / color / clip changes trigger a full preview.
    connect(m_modeCombo,     QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int) { onPreviewTrigger(); });
    connect(m_colorCombo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int) { onPreviewTrigger(); });
    connect(m_clipModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int) { onPreviewTrigger(); });

    // Histogram zoom controls.
    connect(m_zoomInBtn, &QToolButton::clicked, [this]() {
        if (m_zoomLevel < 100) {
            m_zoomLevel++;
            m_zoomLabel->setText(histogramZoomLabelFromLevel(m_zoomLevel));
            onZoomChanged();
        }
    });
    connect(m_zoomOutBtn, &QToolButton::clicked, [this]() {
        if (m_zoomLevel > 1) {
            m_zoomLevel--;
            m_zoomLabel->setText(histogramZoomLabelFromLevel(m_zoomLevel));
            onZoomChanged();
        }
    });
    connect(m_zoomResetBtn, &QToolButton::clicked, [this]() {
        m_zoomLevel = 1;
        m_zoomLabel->setText(histogramZoomLabelFromLevel(m_zoomLevel));
        onZoomChanged();
    });

    connect(m_logScaleCheck, &QCheckBox::toggled, [this](bool checked) {
        m_histWidget->setLogScale(checked);
    });

    // Channel visibility toggles.
    connect(m_redBtn,   &QToolButton::toggled, this, &GHSDialog::onChannelToggled);
    connect(m_greenBtn, &QToolButton::toggled, this, &GHSDialog::onChannelToggled);
    connect(m_blueBtn,  &QToolButton::toggled, this, &GHSDialog::onChannelToggled);
    connect(m_gridBtn,  &QToolButton::toggled, [this](bool) { updateHistogram(); });
    connect(m_curveBtn, &QToolButton::toggled, [this](bool) { updateHistogram(); });

    // Preview enable/disable.
    connect(m_previewCheck, &QCheckBox::toggled, this, &GHSDialog::onPreviewTrigger);
}

// ===========================================================================
// Data setters
// ===========================================================================

void GHSDialog::setHistogramData(const std::vector<std::vector<int>>& bins,
                                 int channels)
{
    m_origBins = bins;
    m_channels = channels;
    m_histWidget->setGhostData(bins, channels);
    updateHistogram();
}

void GHSDialog::setSymmetryPoint(double sp)
{
    m_spSpin->blockSignals(true);
    m_spSlider->blockSignals(true);
    m_spSpin->setValue(sp);
    m_spSlider->setValue(static_cast<int>(sp * 10000));
    m_spSpin->blockSignals(false);
    m_spSlider->blockSignals(false);

    // Region pick should update the preview immediately.
    onPreviewTrigger();
}

void GHSDialog::setClippingStats(float lowPct, float highPct)
{
    if (m_lowClipLabel)  m_lowClipLabel->setText(tr("Low: %1%").arg(lowPct, 0, 'f', 4));
    if (m_highClipLabel) m_highClipLabel->setText(tr("High: %1%").arg(highPct, 0, 'f', 4));
}

// ===========================================================================
// Parameter collection
// ===========================================================================

ImageBuffer::GHSParams GHSDialog::getParams() const
{
    ImageBuffer::GHSParams params;
    params.D         = m_dSpin->value();
    params.B         = m_bSpin->value();
    params.SP        = m_spSpin->value();
    params.LP        = m_lpSpin->value();
    params.HP        = m_hpSpin->value();
    params.BP        = m_bpSpin->value();
    params.mode      = static_cast<ImageBuffer::GHSMode>(m_modeCombo->currentData().toInt());
    params.colorMode = static_cast<ImageBuffer::GHSColorMode>(m_colorCombo->currentData().toInt());
    params.clipMode  = static_cast<ImageBuffer::GHSClipMode>(m_clipModeCombo->currentData().toInt());
    params.inverse   = (params.mode == ImageBuffer::GHS_InverseGeneralizedHyperbolic ||
                        params.mode == ImageBuffer::GHS_InverseArcSinh);
    params.channels[0] = m_redBtn->isChecked();
    params.channels[1] = m_greenBtn->isChecked();
    params.channels[2] = m_blueBtn->isChecked();
    return params;
}

// ===========================================================================
// State serialization
// ===========================================================================

GHSDialog::State GHSDialog::getState() const
{
    State s;
    s.d  = m_dSpin->value();
    s.b  = m_bSpin->value();
    s.sp = m_spSpin->value();
    s.lp = m_lpSpin->value();
    s.hp = m_hpSpin->value();
    s.bp = m_bpSpin->value();

    s.mode      = m_modeCombo->currentIndex();
    s.colorMode = m_colorCombo->currentIndex();
    s.clipMode  = m_clipModeCombo->currentIndex();

    s.channels[0] = m_redBtn->isChecked();
    s.channels[1] = m_greenBtn->isChecked();
    s.channels[2] = m_blueBtn->isChecked();

    s.logScale = m_logScaleCheck->isChecked();

    s.sliderD  = m_dSlider->value();
    s.sliderB  = m_bSlider->value();
    s.sliderSP = m_spSlider->value();
    s.sliderLP = m_lpSlider->value();
    s.sliderHP = m_hpSlider->value();
    s.sliderBP = m_bpSlider->value();
    return s;
}

void GHSDialog::setState(const State& s)
{
    // Block every signal to prevent cascading intermediate updates.
    const bool wasBlocked = signalsBlocked();
    blockSignals(true);

    m_dSpin->blockSignals(true);   m_dSlider->blockSignals(true);
    m_bSpin->blockSignals(true);   m_bSlider->blockSignals(true);
    m_spSpin->blockSignals(true);  m_spSlider->blockSignals(true);
    m_lpSpin->blockSignals(true);  m_lpSlider->blockSignals(true);
    m_hpSpin->blockSignals(true);  m_hpSlider->blockSignals(true);
    m_bpSpin->blockSignals(true);  m_bpSlider->blockSignals(true);
    m_modeCombo->blockSignals(true);
    m_colorCombo->blockSignals(true);
    m_clipModeCombo->blockSignals(true);
    m_redBtn->blockSignals(true);
    m_greenBtn->blockSignals(true);
    m_blueBtn->blockSignals(true);

    // Restore spin box values.
    m_dSpin->setValue(s.d);
    m_bSpin->setValue(s.b);
    m_spSpin->setValue(s.sp);
    m_lpSpin->setValue(s.lp);
    m_hpSpin->setValue(s.hp);
    m_bpSpin->setValue(s.bp);

    // Restore slider positions.
    m_dSlider->setValue(s.sliderD);
    m_bSlider->setValue(s.sliderB);
    m_spSlider->setValue(s.sliderSP);
    m_lpSlider->setValue(s.sliderLP);
    m_hpSlider->setValue(s.sliderHP);
    m_bpSlider->setValue(s.sliderBP);

    // Restore combo selections.
    m_modeCombo->setCurrentIndex(s.mode);
    m_colorCombo->setCurrentIndex(s.colorMode);
    m_clipModeCombo->setCurrentIndex(s.clipMode);

    // Restore channel toggles.
    m_redBtn->setChecked(s.channels[0]);
    m_greenBtn->setChecked(s.channels[1]);
    m_blueBtn->setChecked(s.channels[2]);

    m_logScaleCheck->setChecked(s.logScale);

    // Unblock all.
    m_dSpin->blockSignals(false);   m_dSlider->blockSignals(false);
    m_bSpin->blockSignals(false);   m_bSlider->blockSignals(false);
    m_spSpin->blockSignals(false);  m_spSlider->blockSignals(false);
    m_lpSpin->blockSignals(false);  m_lpSlider->blockSignals(false);
    m_hpSpin->blockSignals(false);  m_hpSlider->blockSignals(false);
    m_bpSpin->blockSignals(false);  m_bpSlider->blockSignals(false);
    m_modeCombo->blockSignals(false);
    m_colorCombo->blockSignals(false);
    m_clipModeCombo->blockSignals(false);
    m_redBtn->blockSignals(false);
    m_greenBtn->blockSignals(false);
    m_blueBtn->blockSignals(false);

    // Synchronize derived state.
    m_showRed   = s.channels[0];
    m_showGreen = s.channels[1];
    m_showBlue  = s.channels[2];
    m_histWidget->setLogScale(s.logScale);

    blockSignals(wasBlocked);
}

void GHSDialog::resetState()
{
    onReset();
}

// ===========================================================================
// Apply / Reset
// ===========================================================================

void GHSDialog::onApply()
{
    if (!m_activeViewer) return;

    ImageBuffer::GHSParams params = getParams();

    m_selfUpdating = true;

    // 1. Restore the viewer to the clean (pre-preview) state.
    m_activeViewer->setBuffer(m_originalBuffer,
                              m_activeViewer->windowTitle(), true);

    // 2. Push the clean state onto the undo stack.
    m_activeViewer->pushUndo(tr("GHS"));

    // 3. Apply the stretch to the working buffer.
    m_originalBuffer.applyGHS(params);

    // 4. Commit the stretched result to the viewer.
    m_activeViewer->setBuffer(m_originalBuffer,
                              m_activeViewer->windowTitle(), true);
    m_selfUpdating = false;

    m_applied = true;

    // Reset intensity controls for iterative re-stretching.
    resetIntensity();

    // Recompute histogram from the new base.
    setHistogramData(m_originalBuffer.computeHistogram(65536),
                     m_originalBuffer.channels());

    if (auto mw = getCallbacks()) {
        mw->logMessage(tr("GHS applied."), 1);
    }

    emit applied(tr("GHS Transformation applied to %1")
                     .arg(m_activeViewer->windowTitle()));
}

void GHSDialog::onReset()
{
    // Block signals to avoid redundant previews during batch updates.
    m_dSpin->blockSignals(true);
    m_bSpin->blockSignals(true);
    m_spSpin->blockSignals(true);
    m_lpSpin->blockSignals(true);
    m_hpSpin->blockSignals(true);
    m_bpSpin->blockSignals(true);

    m_dSpin->setValue(0.0);
    m_bSpin->setValue(0.0);
    m_spSpin->setValue(0.0);
    m_lpSpin->setValue(0.0);
    m_hpSpin->setValue(1.0);
    m_bpSpin->setValue(0.0);

    m_modeCombo->setCurrentIndex(0);
    m_colorCombo->setCurrentIndex(0);
    m_clipModeCombo->setCurrentIndex(0);

    m_dSpin->blockSignals(false);
    m_bSpin->blockSignals(false);
    m_spSpin->blockSignals(false);
    m_lpSpin->blockSignals(false);
    m_hpSpin->blockSignals(false);
    m_bpSpin->blockSignals(false);

    // Synchronize sliders to match the reset spin values.
    m_dSlider->setValue(0);
    m_bSlider->setValue(2500);   // B = 0.0 mapped into -5..15 range
    m_spSlider->setValue(0);
    m_lpSlider->setValue(0);
    m_hpSlider->setValue(10000);
    m_bpSlider->setValue(0);

    onPreviewTrigger();
}

void GHSDialog::resetIntensity()
{
    // Partial reset: clear stretch/intensity/black-point but preserve SP,
    // mode, color mode, and channel selections for iterative workflows.
    m_dSpin->blockSignals(true);
    m_bSpin->blockSignals(true);
    m_bpSpin->blockSignals(true);

    m_dSpin->setValue(0.0);
    m_bSpin->setValue(0.0);
    m_bpSpin->setValue(0.0);

    m_dSpin->blockSignals(false);
    m_bSpin->blockSignals(false);
    m_bpSpin->blockSignals(false);

    m_dSlider->setValue(0);
    m_bSlider->setValue(2500);
    m_bpSlider->setValue(0);

    updateHistogram();
}

// ===========================================================================
// Real-time curve update (lightweight, no image processing)
// ===========================================================================

void GHSDialog::onValueChange()
{
    ImageBuffer::GHSParams params = getParams();

    // Compute a transfer curve LUT for the histogram widget overlay.
    ImageBuffer ib;
    std::vector<float> lut = ib.computeGHSLUT(params);

    if (m_histWidget) {
        m_histWidget->setTransformCurve(lut);
        updateHistogram();
    }
}

// ===========================================================================
// Full preview (expensive: applies transform or builds LUT for viewer)
// ===========================================================================

void GHSDialog::onPreviewTrigger()
{
    // Throttle: if the timer is running, defer to the next tick.
    if (m_previewTimer->isActive()) {
        m_previewPending = true;
        return;
    }

    // Keep the curve overlay synchronized.
    onValueChange();

    // Build parameters (identity when preview is unchecked).
    ImageBuffer::GHSParams params;
    if (m_previewCheck->isChecked()) {
        params = getParams();
    } else {
        params.D  = 0.0;  params.B  = 0.0;  params.SP = 0.0;
        params.LP = 0.0;  params.HP = 1.0;  params.BP = 0.0;
        params.mode      = ImageBuffer::GHS_GeneralizedHyperbolic;
        params.colorMode = ImageBuffer::GHS_Independent;
        params.inverse   = false;
        params.channels[0] = params.channels[1] = params.channels[2] = true;
    }

    if (m_activeViewer) {
        const bool isIdentity =
            (std::abs(params.D)  < 1e-6 &&
             std::abs(params.B)  < 1e-6 &&
             std::abs(params.BP) < 1e-6 &&
             params.mode == ImageBuffer::GHS_GeneralizedHyperbolic);

        if (isIdentity) {
            // Identity transform -- just restore the original buffer.
            m_selfUpdating = true;
            m_activeViewer->setBuffer(m_originalBuffer,
                                      m_activeViewer->windowTitle(), true);
            m_selfUpdating = false;
            setClippingStats(0, 0);

        } else if (params.colorMode == ImageBuffer::GHS_Independent ||
                   m_originalBuffer.channels() == 1) {
            // Fast path: per-channel LUT applied at render time.
            // Avoids a full buffer copy; the viewer composites the LUT
            // on the fly during display refresh.
            const int LUT_SZ = 65536;
            std::vector<float> ghsLut = m_originalBuffer.computeGHSLUT(params, LUT_SZ);
            const float identityScale = 1.0f / (LUT_SZ - 1);

            std::vector<std::vector<float>> luts(3, std::vector<float>(LUT_SZ));
            for (int i = 0; i < LUT_SZ; ++i) {
                const float identity = i * identityScale;
                luts[0][i] = params.channels[0] ? ghsLut[i] : identity;
                luts[1][i] = params.channels[1] ? ghsLut[i] : identity;
                luts[2][i] = params.channels[2] ? ghsLut[i] : identity;
            }

            // Compute clipping stats from a temporary stretched buffer.
            ImageBuffer temp = m_originalBuffer;
            temp.applyGHS(params);
            long lowClip = 0, highClip = 0;
            temp.computeClippingStats(lowClip, highClip);
            const long total = static_cast<long>(temp.width()) *
                               temp.height() * temp.channels();
            if (total > 0) {
                setClippingStats((100.0f * lowClip) / total,
                                 (100.0f * highClip) / total);
            }

            m_activeViewer->setPreviewLUT(luts);

        } else {
            // Slow path for coupled color modes (WeightedLuminance,
            // EvenWeightedLuminance, Saturation).  These require per-pixel
            // multi-channel math that cannot be expressed as a 1-D LUT.
            ImageBuffer temp = m_originalBuffer;
            temp.applyGHS(params);

            long lowClip = 0, highClip = 0;
            temp.computeClippingStats(lowClip, highClip);
            const long total = static_cast<long>(temp.width()) *
                               temp.height() * temp.channels();
            if (total > 0) {
                setClippingStats((100.0f * lowClip) / total,
                                 (100.0f * highClip) / total);
            }

            m_selfUpdating = true;
            m_activeViewer->setBuffer(temp,
                                      m_activeViewer->windowTitle(), true);
            m_selfUpdating = false;
        }
    } else {
        emit previewRequested(params);
    }

    // Re-arm the throttle timer.
    m_previewTimer->start();
}

// ===========================================================================
// Context switching
// ===========================================================================

void GHSDialog::setTarget(ImageViewer* viewer)
{
    qDebug() << "[GHSDialog::setTarget] New:" << viewer
             << "Old:" << m_activeViewer;
    if (m_activeViewer == viewer) return;

    // -- Teardown previous viewer -------------------------------------------
    if (m_activeViewer) {
        m_activeViewer->setRegionSelectedCallback(nullptr);
        disconnect(m_activeViewer, &ImageViewer::destroyed,     this, nullptr);
        disconnect(m_activeViewer, &ImageViewer::bufferChanged, this, nullptr);

        m_selfUpdating = true;

        // Clear the LUT before restoring to avoid baking the preview.
        std::vector<std::vector<float>> emptyLUT;
        m_activeViewer->setPreviewLUT(emptyLUT);
        m_activeViewer->setBuffer(m_originalBuffer,
                                  m_activeViewer->windowTitle(), true);

        m_activeViewer->setInteractionMode(ImageViewer::Mode_PanZoom);
        m_activeViewer->setCursor(Qt::ArrowCursor);
        m_selfUpdating = false;
    }

    // -- Bind new viewer ----------------------------------------------------
    m_activeViewer = viewer;
    m_applied      = false;

    if (m_activeViewer) {
        // Listen for external buffer changes (e.g. Undo/Redo from main menu).
        connect(m_activeViewer, &ImageViewer::bufferChanged, this, [this]() {
            if (m_selfUpdating) return;

            m_originalBuffer = m_activeViewer->getBuffer();
            setHistogramData(m_originalBuffer.computeHistogram(65536),
                             m_originalBuffer.channels());
            onPreviewTrigger();
        });

        // Handle viewer destruction gracefully.
        connect(m_activeViewer, &ImageViewer::destroyed, this, [this](QObject* obj) {
            if (obj == m_activeViewer) {
                m_activeViewer = nullptr;
                if (m_previewTimer) m_previewTimer->stop();
                m_previewPending = false;
            }
        });

        // Enable region selection for SP picking when interaction is on.
        if (m_interactionEnabled) {
            m_activeViewer->setInteractionMode(ImageViewer::Mode_Selection);
            m_activeViewer->setCursor(Qt::CrossCursor);
        }

        // Region-selected callback: compute mean value and set as SP.
        m_activeViewer->setRegionSelectedCallback([this](QRectF r) {
            try {
                if (!m_activeViewer) return;

                r = r.normalized();
                if (r.isEmpty()) return;
                QRect rect = r.toRect();

                if (rect.right()  < 0 || rect.bottom() < 0 ||
                    rect.left()   >= m_originalBuffer.width() ||
                    rect.top()    >= m_originalBuffer.height()) {
                    return;
                }

                float val = 0.0f;
                if (m_originalBuffer.channels() == 3) {
                    float sumMeans = 0.0f;
                    int   count    = 0;
                    if (m_redBtn->isChecked())   { sumMeans += m_originalBuffer.getAreaMean(rect.x(), rect.y(), rect.width(), rect.height(), 0); count++; }
                    if (m_greenBtn->isChecked()) { sumMeans += m_originalBuffer.getAreaMean(rect.x(), rect.y(), rect.width(), rect.height(), 1); count++; }
                    if (m_blueBtn->isChecked())  { sumMeans += m_originalBuffer.getAreaMean(rect.x(), rect.y(), rect.width(), rect.height(), 2); count++; }
                    val = (count > 0) ? (sumMeans / count) : 0.0f;
                } else {
                    val = m_originalBuffer.getAreaMean(rect.x(), rect.y(),
                                                       rect.width(), rect.height(), 0);
                }
                setSymmetryPoint(static_cast<double>(val));
            } catch (const std::exception& e) {
                qWarning() << "GHSDialog selection error:" << e.what();
            } catch (...) {
                qWarning() << "GHSDialog selection error: unknown exception";
            }
        });

        // Take a snapshot of the current buffer state.
        m_originalBuffer  = m_activeViewer->getBuffer();
        m_bufferAtOpening = m_originalBuffer;
        setHistogramData(m_originalBuffer.computeHistogram(65536),
                         m_originalBuffer.channels());

        // Defer the first preview slightly to avoid fade-in lag.
        QTimer::singleShot(300, this, &GHSDialog::onPreviewTrigger);

    } else {
        m_originalBuffer = ImageBuffer();
        if (m_histWidget) m_histWidget->clear();
    }
}

// ===========================================================================
// Histogram zoom
// ===========================================================================

void GHSDialog::onZoomChanged()
{
    int baseWidth = m_scrollArea ? m_scrollArea->viewport()->width() : 400;
    if (baseWidth < 100) baseWidth = 400;

    const double zoomScale = histogramZoomScaleFromLevel(m_zoomLevel);
    const int    newWidth  = static_cast<int>(baseWidth * zoomScale);

    if (m_zoomLevel == 1 || zoomScale <= 1.001) {
        // At 1x zoom, let the widget resize to fill the viewport.
        if (m_scrollArea) m_scrollArea->setWidgetResizable(true);
        m_histWidget->setMinimumWidth(0);
        m_histWidget->setMaximumWidth(16777215);
        m_histScrollBar->setVisible(false);
    } else {
        // At higher zoom, lock the widget width and show the scrollbar.
        if (m_scrollArea) m_scrollArea->setWidgetResizable(true);
        m_histWidget->setMinimumWidth(newWidth);
        m_histWidget->setMaximumWidth(newWidth);
        m_histScrollBar->setVisible(true);

        // Keep the shadow (left) side in view when zooming in.
        if (m_scrollArea) {
            m_scrollArea->horizontalScrollBar()->setValue(0);
        }
    }

    updateHistogram();
}

// ===========================================================================
// Channel toggles
// ===========================================================================

void GHSDialog::onChannelToggled()
{
    m_showRed   = m_redBtn->isChecked();
    m_showGreen = m_greenBtn->isChecked();
    m_showBlue  = m_blueBtn->isChecked();
    updateHistogram();
    onPreviewTrigger();
}

// ===========================================================================
// updateHistogram -- recompute transformed bins and redraw the widget
// ===========================================================================

void GHSDialog::updateHistogram()
{
    if (m_origBins.empty() || !m_histWidget) return;

    const ImageBuffer::GHSParams params = getParams();

    int histSize = 65536;
    if (!m_origBins.empty() && !m_origBins[0].empty()) {
        histSize = static_cast<int>(m_origBins[0].size());
    }

    // -- 1. Compute the transfer curve LUT for the overlay ------------------
    const int lutSize = 4096;
    std::vector<float> lut(lutSize);

    GHSAlgo::GHSParams algoParams;
    algoParams.D  = static_cast<float>(params.D);
    algoParams.B  = static_cast<float>(params.B);
    algoParams.SP = static_cast<float>(params.SP);
    algoParams.LP = static_cast<float>(params.LP);
    algoParams.HP = static_cast<float>(params.HP);
    algoParams.BP = static_cast<float>(params.BP);

    switch (params.mode) {
        case ImageBuffer::GHS_GeneralizedHyperbolic:        algoParams.type = GHSAlgo::STRETCH_PAYNE_NORMAL;  break;
        case ImageBuffer::GHS_InverseGeneralizedHyperbolic: algoParams.type = GHSAlgo::STRETCH_PAYNE_INVERSE; break;
        case ImageBuffer::GHS_Linear:                       algoParams.type = GHSAlgo::STRETCH_LINEAR;        break;
        case ImageBuffer::GHS_ArcSinh:                      algoParams.type = GHSAlgo::STRETCH_ASINH;         break;
        case ImageBuffer::GHS_InverseArcSinh:               algoParams.type = GHSAlgo::STRETCH_INVASINH;      break;
        default:                                            algoParams.type = GHSAlgo::STRETCH_PAYNE_NORMAL;  break;
    }

    GHSAlgo::GHSComputeParams cp;
    GHSAlgo::setup(cp, algoParams.B, algoParams.D,
                   algoParams.LP, algoParams.SP, algoParams.HP,
                   algoParams.type);

    for (int i = 0; i < lutSize; ++i) {
        lut[i] = GHSAlgo::compute(static_cast<float>(i) / (lutSize - 1),
                                  algoParams, cp);
    }
    m_histWidget->setTransformCurve(lut);

    // -- 2. Build a bin-mapping LUT and transform the histogram data --------
    std::vector<int> transformLUT(histSize);
    for (int i = 0; i < histSize; ++i) {
        const float normX  = static_cast<float>(i) / (histSize - 1);
        const float y      = GHSAlgo::compute(normX, algoParams, cp);
        const int   outBin = static_cast<int>(y * (histSize - 1) + 0.5f);
        transformLUT[i] = std::clamp(outBin, 0, histSize - 1);
    }

    std::vector<std::vector<int>> transformedBins(
        m_channels, std::vector<int>(histSize, 0));

    const int numCh = std::min(m_channels, static_cast<int>(m_origBins.size()));

#ifdef _OPENMP
    #pragma omp parallel for
#endif
    for (int c = 0; c < numCh; ++c) {
        const int* srcData = m_origBins[c].data();
        int*       dstData = transformedBins[c].data();
        for (int i = 0; i < histSize; ++i) {
            const int count = srcData[i];
            if (count > 0) {
                dstData[transformLUT[i]] += count;
            }
        }
    }

    // -- 3. Compose display bins --------------------------------------------
    // Active channels show their transformed histogram; inactive channels
    // show their original histogram at its native position so the user
    // can always see every channel.
    const bool showChannels[3] = { m_showRed, m_showGreen, m_showBlue };

    std::vector<std::vector<int>> displayBins(m_channels);
    for (int c = 0; c < m_channels && c < 3; ++c) {
        if (showChannels[c]) {
            displayBins[c] = transformedBins[c];
        } else if (c < static_cast<int>(m_origBins.size())) {
            displayBins[c] = m_origBins[c];
        }
    }

    // -- 4. Clipping statistics (active channels only) ----------------------
    long lowClip = 0, highClip = 0, totalPixels = 0;
    for (int c = 0; c < m_channels && c < static_cast<int>(transformedBins.size()); ++c) {
        if (!showChannels[c]) continue;
        lowClip  += transformedBins[c][0];
        highClip += transformedBins[c][histSize - 1];
        for (int count : transformedBins[c]) {
            totalPixels += count;
        }
    }
    if (totalPixels > 0) {
        setClippingStats((100.0f * lowClip)  / totalPixels,
                         (100.0f * highClip) / totalPixels);
    } else {
        setClippingStats(0, 0);
    }

    // -- 5. Push data to the widget -----------------------------------------
    m_histWidget->setData(displayBins, m_channels);
    m_histWidget->setShowGrid(m_gridBtn->isChecked());
    m_histWidget->setShowCurve(m_curveBtn->isChecked());
}

// ===========================================================================
// Interaction mode
// ===========================================================================

void GHSDialog::setInteractionEnabled(bool enabled)
{
    if (m_interactionEnabled == enabled) return;
    m_interactionEnabled = enabled;

    if (m_activeViewer) {
        if (enabled) {
            m_activeViewer->setInteractionMode(ImageViewer::Mode_Selection);
            m_activeViewer->setCursor(Qt::CrossCursor);
        } else {
            m_activeViewer->setInteractionMode(ImageViewer::Mode_PanZoom);
            m_activeViewer->setCursor(Qt::ArrowCursor);
            m_activeViewer->clearSelection();
        }
    }
}