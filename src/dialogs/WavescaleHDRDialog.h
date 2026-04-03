#ifndef WAVESCALEHDRDIALOG_H
#define WAVESCALEHDRDIALOG_H

// =============================================================================
// WavescaleHDRDialog.h
// Dialog for wavelet-based HDR compression of astronomical images.
// Decomposes the luminance channel via a trous wavelets, modulates wavelet
// planes according to a brightness mask, and reconstructs with optional
// dimming gamma and opacity blending.
// =============================================================================

#include "DialogBase.h"
#include "../ImageBuffer.h"

#include <QDialog>
#include <QThread>
#include <QTimer>
#include <QGroupBox>
#include <QPointer>
#include <vector>

class ImageViewer;
class QSlider;
class QLabel;
class QCheckBox;
class QPushButton;
class QProgressBar;

// =============================================================================
// WavescaleHDRWorker  --  Background thread that performs the wavelet
// decomposition, mask-weighted plane modulation, and reconstruction.
// =============================================================================
class WavescaleHDRWorker : public QThread {
    Q_OBJECT

public:
    explicit WavescaleHDRWorker(QObject* parent = nullptr);

    /// Configure the worker parameters before starting the thread.
    void setup(const ImageBuffer& src,
               int   scales,
               float compression,
               float maskGamma,
               float dimmingGamma);

    void run() override;

signals:
    void progress(int pct);
    void finished(ImageBuffer result, ImageBuffer mask);

private:
    ImageBuffer m_src;
    int         m_scales;
    float       m_compression;
    float       m_maskGamma;
    float       m_dimmingGamma;
};

// =============================================================================
// WavescaleHDRDialog  --  Main dialog providing the user interface for the
// wavelet HDR tool, including an embedded preview viewer and mask preview.
// =============================================================================
class WavescaleHDRDialog : public DialogBase {
    Q_OBJECT

public:
    explicit WavescaleHDRDialog(QWidget* parent = nullptr,
                                ImageViewer* targetViewer = nullptr);
    ~WavescaleHDRDialog();

    void setViewer(ImageViewer* v);
    ImageViewer* viewer() const { return m_targetViewer; }

signals:
    /// Emitted when the user clicks Apply, carrying the final processed buffer.
    void applyInternal(const ImageBuffer& result);

private slots:
    void onPreviewClicked();
    void onApplyClicked();
    void onWorkerFinished(ImageBuffer result, ImageBuffer mask);
    void toggleOriginal(bool showOriginal);
    void updateMaskPreview(const ImageBuffer& mask);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void createUI();
    void startPreview();

    /// Fast mask preview update using cached downscaled luminance data.
    void updateQuickMask();

    /// Blends the raw worker result with the original buffer using the
    /// current opacity setting and any active mask.
    void applyOpacityBlend();

    // --- UI widgets ---
    ImageViewer* m_viewer;         ///< Embedded preview viewer
    QLabel*      m_maskLabel;      ///< Thumbnail mask preview

    QSlider* m_scalesSlider;
    QSlider* m_compSlider;
    QSlider* m_gammaSlider;
    QSlider* m_dimmingSlider;
    QSlider* m_opacitySlider = nullptr;

    QLabel* m_scalesLabel;
    QLabel* m_compLabel;
    QLabel* m_gammaLabel;
    QLabel* m_dimmingLabel;
    QLabel* m_opacityLabel  = nullptr;

    QCheckBox*    m_showOriginalCheck;
    QPushButton*  m_previewBtn;
    QPushButton*  m_applyBtn;
    QPushButton*  m_closeBtn;
    QProgressBar* m_progressBar;

    // --- Processing state ---
    QPointer<ImageViewer> m_targetViewer;   ///< Source image window (safe pointer)
    ImageBuffer           m_previewBuffer;  ///< Current result (mask + opacity blended)
    ImageBuffer           m_rawResult;      ///< Unblended worker output
    ImageBuffer           m_maskBuffer;     ///< Current mask from the worker
    ImageBuffer           m_originalBuffer; ///< Snapshot of the source image

    // --- Downscaled luminance cache for fast mask preview ---
    std::vector<float> m_L_channel_cache;
    int                m_cacheW = 0;
    int                m_cacheH = 0;

    // --- Worker ---
    WavescaleHDRWorker* m_worker;
    bool                m_isPreviewDirty;
    bool                m_isFirstShow = true;
};

#endif // WAVESCALEHDRDIALOG_H