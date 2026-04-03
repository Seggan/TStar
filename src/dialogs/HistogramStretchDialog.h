#ifndef HISTOGRAMSTRETCHDIALOG_H
#define HISTOGRAMSTRETCHDIALOG_H

// =============================================================================
// HistogramStretchDialog.h
// Dialog providing interactive Midtone Transfer Function (MTF) histogram
// transformation with shadows/midtones/highlights controls, per-channel
// toggles, real-time histogram preview, and auto-stretch computation.
// =============================================================================

#include "DialogBase.h"
#include "ImageBuffer.h"
#include "ImageViewer.h"
#include "widgets/HistogramWidget.h"

#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QToolButton>
#include <QPointer>

#include <vector>

class QScrollArea;
class QScrollBar;

class HistogramStretchDialog : public DialogBase {
    Q_OBJECT

public:
    explicit HistogramStretchDialog(ImageViewer* viewer,
                                    QWidget* parent = nullptr);
    ~HistogramStretchDialog();

    // Bind this dialog to a different ImageViewer instance.
    void setViewer(ImageViewer* v);

protected:
    // Restore backup state when the dialog is dismissed without applying.
    void reject() override;

signals:
    // Emitted after the transformation has been applied to the viewer.
    void applied();

private slots:
    void onShadowsChanged();
    void onMidtonesChanged();
    void onHighlightsChanged();
    void onSliderValueChanged();    // Real-time histogram update during drag.
    void onSliderReleased();        // Full image preview update on release.
    void onChannelToggled();
    void onPreviewToggled(bool checked);
    void onAutoStretch();
    void onReset();
    void onApply();
    void onZoomChanged();

private:
    void setupUI();
    void updatePreview();
    void updateHistogramOnly();     // Histogram-only update (no image render).
    void updateClippingStats(const ImageBuffer& buffer);
    void updateClippingStatsOnly(); // Clipping stats without image preview.

    // Apply the Midtone Transfer Function to an ImageBuffer in-place.
    void applyMTF(ImageBuffer& buffer,
                  float shadows, float midtones, float highlights,
                  bool doRed, bool doGreen, bool doBlue);

    // Compute optimal auto-stretch parameters from image statistics.
    void computeAutostretch(const ImageBuffer& buffer,
                            float& shadows, float& midtones, float& highlights);

    // Evaluate the MTF for a single pixel value.
    float MTF(float x, float m, float lo, float hi);

    // Statistical helper functions.
    float computeMedian(const float* data, size_t n);
    float computeMAD(const float* data, size_t n, float median);

    // -- Viewer state ---------------------------------------------------------
    QPointer<ImageViewer>      m_viewer;
    ImageBuffer                m_backup;
    bool                       m_applied             = false;
    ImageBuffer::DisplayMode   m_originalDisplayMode = ImageBuffer::Display_Linear;
    bool                       m_originalDisplayLinked = true;

    // -- UI widgets -----------------------------------------------------------
    HistogramWidget* m_histogram      = nullptr;
    QScrollArea*     m_scrollArea     = nullptr;
    QScrollBar*      m_histScrollBar  = nullptr;
    QToolButton*     m_zoomInBtn      = nullptr;
    QToolButton*     m_zoomOutBtn     = nullptr;
    QToolButton*     m_zoomResetBtn   = nullptr;
    QLabel*          m_zoomLabel      = nullptr;

    QSlider*         m_shadowsSlider    = nullptr;
    QSlider*         m_midtonesSlider   = nullptr;
    QSlider*         m_highlightsSlider = nullptr;
    QDoubleSpinBox*  m_shadowsSpin      = nullptr;
    QDoubleSpinBox*  m_midtonesSpin     = nullptr;
    QDoubleSpinBox*  m_highlightsSpin   = nullptr;

    QToolButton*     m_redBtn        = nullptr;
    QToolButton*     m_greenBtn      = nullptr;
    QToolButton*     m_blueBtn       = nullptr;
    QCheckBox*       m_previewCheck  = nullptr;
    QPushButton*     m_autoStretchBtn = nullptr;
    QLabel*          m_lowClipLabel  = nullptr;
    QLabel*          m_highClipLabel = nullptr;

    // -- Transformation parameters --------------------------------------------
    float m_shadows    = 0.0f;
    float m_midtones   = 0.5f;
    float m_highlights = 1.0f;
    bool  m_doRed      = true;
    bool  m_doGreen    = true;
    bool  m_doBlue     = true;
    int   m_zoomLevel  = 1;

    // -- Cached histogram data for performance --------------------------------
    std::vector<std::vector<int>> m_baseHist;
};

#endif // HISTOGRAMSTRETCHDIALOG_H