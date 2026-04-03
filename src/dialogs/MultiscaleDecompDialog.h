#ifndef MULTISCALE_DECOMP_DIALOG_H
#define MULTISCALE_DECOMP_DIALOG_H

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QScrollBar>

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"
#include "../MainWindowCallbacks.h"

class ImageViewer;

// =============================================================================
// MultiscaleDecompDialog
//
// Interactive multiscale decomposition tool. Decomposes an image into a
// configurable number of Gaussian detail layers plus a residual, then allows
// per-layer gain, threshold, amount, and denoise adjustments before
// reconstructing and optionally applying the result.
// =============================================================================
class MultiscaleDecompDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit MultiscaleDecompDialog(QWidget* parent = nullptr);
    ~MultiscaleDecompDialog();

    // Attach a viewer; loads its buffer and triggers an initial decomposition.
    void setViewer(ImageViewer* v);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onLayersChanged(int val);
    void onSigmaChanged(double val);
    void onModeChanged(int idx);
    void onPreviewComboChanged(int idx);
    void onTableSelectionChanged();
    void onTableItemChanged(QTableWidgetItem* item);
    void onLayerEditorChanged();
    void onGainSliderChanged(int v);
    void onThrSliderChanged(int v);
    void onAmtSliderChanged(int v);
    void onDenoiseSliderChanged(int v);
    void onApplyToImage();
    void onSendToNewImage();
    void rebuildPreview();

private:
    // --- UI construction ---
    void buildUI();

    // --- Decomposition ---
    void recomputeDecomp(bool force = false);
    void syncCfgsAndUI();
    void buildTunedLayers(std::vector<std::vector<float>>& tuned,
                          std::vector<float>& residual);

    // --- Table management ---
    void rebuildTable();
    void refreshPreviewCombo();
    void loadLayerIntoEditor(int idx);
    void updateParamWidgetsForMode();
    void schedulePreview();

    // --- Rendering ---
    QPixmap floatToPixmap(const std::vector<float>& img, int w, int h, int ch);

    // --- Viewer ---
    ImageViewer*          m_viewer     = nullptr;
    MainWindowCallbacks*  m_mainWindow = nullptr;

    // --- Source image data (float32 [0,1], always 3ch internally) ---
    std::vector<float> m_image;
    int  m_imgW   = 0;
    int  m_imgH   = 0;
    int  m_imgCh  = 3;
    bool m_origMono = false;
    int  m_origCh   = 0;

    // --- Decomposition cache ---
    std::vector<std::vector<float>> m_cachedDetails;
    std::vector<float>              m_cachedResidual;
    std::vector<float>              m_layerNoise;
    float m_cachedSigma  = -1.0f;
    int   m_cachedLayers = -1;

    // --- Per-layer configuration ---
    int   m_layers    = 4;
    float m_baseSigma = 1.0f;
    std::vector<ChannelOps::LayerCfg> m_cfgs;
    bool m_residualEnabled = true;

    // --- UI state ---
    int m_selectedLayer = -1;

    // --- Preview debounce ---
    QTimer* m_previewTimer = nullptr;

    // --- Left panel: preview ---
    QGraphicsScene*      m_scene   = nullptr;
    QGraphicsView*       m_view    = nullptr;
    QGraphicsPixmapItem* m_pixBase = nullptr;

    // --- Right panel: global controls ---
    QSpinBox*       m_spinLayers   = nullptr;
    QDoubleSpinBox* m_spinSigma    = nullptr;
    QCheckBox*      m_cbLinkedRGB  = nullptr;
    QComboBox*      m_comboMode    = nullptr;
    QComboBox*      m_comboPreview = nullptr;
    QTableWidget*   m_table        = nullptr;

    // --- Per-layer editor ---
    QLabel*         m_lblSel       = nullptr;
    QDoubleSpinBox* m_spinGain     = nullptr;
    QDoubleSpinBox* m_spinThr      = nullptr;
    QDoubleSpinBox* m_spinAmt      = nullptr;
    QDoubleSpinBox* m_spinDenoise  = nullptr;
    QSlider*        m_sliderGain   = nullptr;
    QSlider*        m_sliderThr    = nullptr;
    QSlider*        m_sliderAmt    = nullptr;
    QSlider*        m_sliderDenoise = nullptr;

    // --- Action buttons ---
    QPushButton* m_btnApply  = nullptr;
    QPushButton* m_btnNewDoc = nullptr;
    QPushButton* m_btnClose  = nullptr;
};

#endif // MULTISCALE_DECOMP_DIALOG_H