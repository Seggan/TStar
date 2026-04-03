#ifndef GHSDIALOG_H
#define GHSDIALOG_H

#include "DialogBase.h"
#include "ImageBuffer.h"

#include <QPointer>
#include <vector>

// Forward declarations
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QScrollBar;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;
class HistogramWidget;
class ImageViewer;
class MainWindowCallbacks;

/**
 * @brief Generalized Hyperbolic Stretch (GHS) dialog.
 *
 * Provides interactive histogram display, parameter sliders, LUT-based
 * live preview, and multi-mode stretch (GHS, ArcSinh, inverse variants).
 * Operates non-destructively via a preview LUT until the user commits
 * changes with Apply.
 */
class GHSDialog : public DialogBase {
    Q_OBJECT

public:
    explicit GHSDialog(QWidget* parent = nullptr);
    ~GHSDialog();

    /** @brief Override reject (Close / Esc) to restore the original image. */
    void reject() override;

    // -- Data setters -------------------------------------------------------
    void setHistogramData(const std::vector<std::vector<int>>& bins, int channels);
    void setSymmetryPoint(double sp);
    void setClippingStats(float lowPct, float highPct);

    // -- Context switching ---------------------------------------------------
    /** @brief Bind to a new viewer, restoring the previous one if needed. */
    void setTarget(ImageViewer* viewer);
    /** @brief Enable / disable region-selection interaction for SP picking. */
    void setInteractionEnabled(bool enabled);

    // -- Serializable state -------------------------------------------------
    /** @brief Snapshot of every dialog control, for save / restore. */
    struct State {
        double d, b, sp, lp, hp, bp;
        int    mode, colorMode, clipMode;
        bool   channels[3];
        bool   logScale;
        int    sliderD, sliderB, sliderSP, sliderLP, sliderHP, sliderBP;
    };

    State getState() const;
    void  setState(const State& s);
    void  resetState();

    /** @brief Read the current parameter values from the UI. */
    ImageBuffer::GHSParams getParams() const;

signals:
    void apply(const ImageBuffer::GHSParams& params);
    void previewLUT(const std::vector<std::vector<float>>& luts);
    void previewRequested(const ImageBuffer::GHSParams& params);
    void resetRequested();
    void undoRequested();
    void redoRequested();
    void applied(const QString& msg);

public slots:
    /** @brief Full reset of all parameters to factory defaults. */
    void onReset();
    /** @brief Partial reset: clear D, B, BP but keep SP, mode, and color settings. */
    void resetIntensity();

private slots:
    void onApply();
    void onValueChange();
    void onPreviewTrigger();
    void onZoomChanged();
    void onChannelToggled();

private:
    // -- UI construction helpers ---------------------------------------------
    void setupUI();
    void connectSignals();
    void updateHistogram();

    // -- Histogram widget ---------------------------------------------------
    HistogramWidget* m_histWidget;

    // -- Mode and color combos ----------------------------------------------
    QComboBox* m_modeCombo;
    QComboBox* m_colorCombo;
    QComboBox* m_clipModeCombo;

    // -- GHS parameter controls (slider + spin pairs) -----------------------
    QDoubleSpinBox* m_dSpin;    QSlider* m_dSlider;    ///< Stretch factor D [0..10]
    QDoubleSpinBox* m_bSpin;    QSlider* m_bSlider;    ///< Local intensity B [-5..15]
    QDoubleSpinBox* m_spSpin;   QSlider* m_spSlider;   ///< Symmetry point SP [0..1]
    QDoubleSpinBox* m_lpSpin;   QSlider* m_lpSlider;   ///< Shadow protection LP [0..1]
    QDoubleSpinBox* m_hpSpin;   QSlider* m_hpSlider;   ///< Highlight protection HP [0..1]
    QDoubleSpinBox* m_bpSpin;   QSlider* m_bpSlider;   ///< Black point BP [0..0.3]

    // -- Histogram toolbar controls -----------------------------------------
    QToolButton* m_zoomInBtn;
    QToolButton* m_zoomOutBtn;
    QToolButton* m_zoomResetBtn;
    QLabel*      m_zoomLabel;
    QLabel*      m_lowClipLabel;
    QLabel*      m_highClipLabel;
    QCheckBox*   m_logScaleCheck;
    QToolButton* m_redBtn;
    QToolButton* m_greenBtn;
    QToolButton* m_blueBtn;
    QToolButton* m_gridBtn;
    QToolButton* m_curveBtn;
    QCheckBox*   m_previewCheck;
    QScrollArea* m_scrollArea;
    QScrollBar*  m_histScrollBar;

    // -- Internal state -----------------------------------------------------
    std::vector<std::vector<int>> m_origBins;
    int  m_channels        = 3;
    int  m_zoomLevel       = 1;
    bool m_showRed         = true;
    bool m_showGreen       = true;
    bool m_showBlue        = true;
    bool m_showGrid        = true;
    bool m_showCurve       = true;

    // -- Preview throttling -------------------------------------------------
    QTimer* m_previewTimer;
    bool    m_previewPending;

    // -- Active viewer context ----------------------------------------------
    ImageBuffer              m_bufferAtOpening;  ///< Snapshot at dialog open (for cancel).
    ImageBuffer              m_originalBuffer;   ///< Current clean base (updated by Apply).
    QPointer<ImageViewer>    m_activeViewer;
    bool m_selfUpdating      = false;            ///< Guard against recursive buffer updates.
    bool m_interactionEnabled = false;
    bool m_applied           = false;
};

#endif // GHSDIALOG_H