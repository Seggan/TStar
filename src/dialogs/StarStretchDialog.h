#ifndef STARSTRETCHDIALOG_H
#define STARSTRETCHDIALOG_H

// =============================================================================
// StarStretchDialog.h
// Dialog for applying a non-linear star stretch with optional colour boost
// and SCNR green removal. Provides live preview with undo support.
// =============================================================================

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/StarStretchRunner.h"

class ImageViewer;
class QLabel;
class QSlider;
class QCheckBox;
class QPushButton;

class StarStretchDialog : public DialogBase {
    Q_OBJECT

public:
    explicit StarStretchDialog(QWidget* parent, ImageViewer* viewer);
    ~StarStretchDialog();

    /// Switch the target viewer, restoring the previous one if un-applied.
    void setViewer(ImageViewer* v);

public slots:
    void onSliderChanged();
    void onApply();
    void updatePreview();
    void reject() override;

private:
    void createUI();

    // Target viewer and buffer state
    ImageViewer*     m_viewer;
    ImageBuffer      m_originalBuffer;
    ImageBuffer      m_previewBuffer;
    StarStretchRunner m_runner;
    bool             m_applied = false;

    // Stretch amount controls
    QLabel*  m_lblStretch;
    QSlider* m_sliderStretch;

    // Colour boost controls
    QLabel*  m_lblBoost;
    QSlider* m_sliderBoost;

    // Options
    QCheckBox*   m_chkScnr;
    QCheckBox*   m_chkPreview;

    // Action button
    QPushButton* m_btnApply;
};

#endif // STARSTRETCHDIALOG_H