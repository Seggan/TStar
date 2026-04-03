#ifndef ABEDIALOG_H
#define ABEDIALOG_H

/**
 * @file ABEDialog.h
 * @brief Dialog for Automatic Background Extraction (ABE).
 *
 * Provides a non-modal dialog that allows the user to configure and apply
 * polynomial + RBF-based background model subtraction. The user can draw
 * exclusion polygons on the image viewer to protect foreground objects
 * from being sampled during background estimation.
 */

#include <QDialog>
#include <QPointer>

#include "DialogBase.h"
#include "ImageBuffer.h"
#include "../ImageViewer.h"

class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QCloseEvent;

class ABEDialog : public DialogBase {
    Q_OBJECT

public:
    /**
     * @brief Constructs the ABE dialog.
     * @param parent         Parent widget.
     * @param viewer         The image viewer currently displaying the target image.
     * @param buffer         A copy of the current image buffer to work on.
     * @param initialStretch Unused; reserved for future stretch-mode integration.
     */
    explicit ABEDialog(QWidget* parent,
                       ImageViewer* viewer,
                       const ImageBuffer& buffer,
                       bool initialStretch);
    ~ABEDialog();

    /** Enables or disables ABE interaction mode on the associated viewer. */
    void setAbeMode(bool enabled);

    /** Assigns a new viewer, transferring ABE mode from any previous viewer. */
    void setViewer(ImageViewer* viewer);

signals:
    /** Emitted when the corrected image buffer is ready to be consumed. */
    void applyResult(const ImageBuffer& result);

    /** Emitted to report progress or error messages during processing. */
    void progressMsg(const QString& msg);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onApply();
    void clearPolys();

private:
    /** Generates the background model and applies correction to @p output in place. */
    void generateModel(ImageBuffer& output);

    // --- Associated viewer and image state ---
    QPointer<ImageViewer> m_viewer;
    ImageBuffer           m_originalBuffer;   ///< Backup of the original image data.
    bool                  m_applied = false;

    // --- UI controls ---
    QSpinBox*       m_spinDegree;
    QSpinBox*       m_spinSamples;
    QSpinBox*       m_spinDown;
    QSpinBox*       m_spinPatch;
    QCheckBox*      m_checkRBF;
    QDoubleSpinBox* m_spinSmooth;
    QCheckBox*      m_checkShowBG;
    QCheckBox*      m_checkNormalize;
};

#endif // ABEDIALOG_H