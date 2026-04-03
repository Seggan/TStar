#ifndef STARRECOMPOSITIONDIALOG_H
#define STARRECOMPOSITIONDIALOG_H

// =============================================================================
// StarRecompositionDialog.h
// Dialog for recombining a starless image with a stars-only image, with
// configurable GHS stretch parameters and a live preview.
// =============================================================================

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/StarRecompositionRunner.h"

#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>

class QDoubleSpinBox;
class MainWindowCallbacks;
class ImageViewer;

class StarRecompositionDialog : public DialogBase {
    Q_OBJECT

public:
    explicit StarRecompositionDialog(QWidget* parent = nullptr);

    /// Assign or reassign the active viewer (refreshes combo boxes).
    void setViewer(ImageViewer* v);

    /// Returns true if the given viewer is currently selected in either
    /// the starless or stars-only combo box.
    bool isUsingViewer(ImageViewer* v) const;

private slots:
    void onRefreshViews();
    void onUpdatePreview();
    void onApply();

private:
    void createUI();
    void populateCombos();

    // Processing back-end
    StarRecompositionRunner m_runner;

    // Source selection combo boxes
    QComboBox* m_cmbStarless;
    QComboBox* m_cmbStars;

    // GHS stretch mode selectors
    QComboBox* m_cmbStretchMode;
    QComboBox* m_cmbColorMode;
    QComboBox* m_cmbClipMode;

    // GHS parameter sliders and spin boxes
    QSlider*        m_sliderD;
    QDoubleSpinBox* m_spinD;
    QSlider*        m_sliderB;
    QDoubleSpinBox* m_spinB;
    QSlider*        m_sliderSP;
    QDoubleSpinBox* m_spinSP;

    // Preview panel
    ImageViewer* m_previewViewer;
    QPushButton* m_btnFit;

    // Downscaled preview buffers (unused externally but reserved for performance)
    ImageBuffer m_previewBufferSll;
    ImageBuffer m_previewBufferStr;
    float       m_previewScale = 1.0f;

    // Initialization guard: suppresses preview updates during construction
    bool m_initializing = true;
};

#endif // STARRECOMPOSITIONDIALOG_H