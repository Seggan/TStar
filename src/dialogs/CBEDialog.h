#ifndef CBEDIALOG_H
#define CBEDIALOG_H

// =============================================================================
// CBEDialog.h
//
// Catalog Background Extraction (CBE) dialog. Downloads a reference sky
// survey image via HiPS, aligns it to the target using WCS reprojection,
// and extracts the large-scale background gradient for subtraction.
// =============================================================================

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../astrometry/HiPSClient.h"

#include <QPointer>
#include <atomic>

class ImageViewer;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QProgressDialog;

class CBEDialog : public DialogBase {
    Q_OBJECT

public:
    explicit CBEDialog(QWidget* parent, ImageViewer* viewer,
                       const ImageBuffer& buffer);
    ~CBEDialog();

    /** Update the active viewer reference. */
    void setViewer(ImageViewer* viewer);

signals:
    /** Emitted with the corrected image buffer after successful extraction. */
    void applyResult(const ImageBuffer& result);

    /** Emitted with progress/status messages during processing. */
    void progressMsg(const QString& msg);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onApply();
    void onHiPSImageReady(const ImageBuffer& refImg);
    void onHiPSError(const QString& err);
    void onCancel();

private:
    // --- Core references ---
    QPointer<ImageViewer> m_viewer;
    ImageBuffer           m_originalBuffer;
    HiPSClient*           m_hipsClient = nullptr;

    // --- UI widgets ---
    QComboBox*   m_comboSurvey;
    QSpinBox*    m_spinScale;
    QCheckBox*   m_checkProtectStars;
    QCheckBox*   m_checkGradientMap;
    QPushButton* m_btnApply;
    QPushButton* m_btnCancel;

    // --- Processing state ---
    std::atomic<bool> m_cancelFlag{false};
    int    m_targetWidth   = 0;       // Full-resolution target dimensions
    int    m_targetHeight  = 0;
    bool   m_parityFlipped = false;   // Whether reference needs horizontal flip
    double m_paddingFactor = 2.0;     // Download area padding for rotation margin

    QProgressDialog* m_busyDialog = nullptr;
};

#endif // CBEDIALOG_H