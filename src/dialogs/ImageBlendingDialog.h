#ifndef IMAGEBLENDINGDIALOG_H
#define IMAGEBLENDINGDIALOG_H

// =============================================================================
// ImageBlendingDialog.h
// Dialog for blending two images using Photoshop-style blend modes with
// opacity, range masking, and channel targeting. Includes a live preview.
// =============================================================================

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/ImageBlendingRunner.h"

#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>

class ImageViewer;

class ImageBlendingDialog : public DialogBase {
    Q_OBJECT

public:
    explicit ImageBlendingDialog(QWidget* parent = nullptr);

    // Pre-select a viewer as the base image in the combo box.
    void setViewer(ImageViewer* v);

protected slots:
    void onApply();
    void updatePreview();
    void populateCombos();
    void onTopImageChanged();
    void showEvent(QShowEvent* event) override;

private:
    // Build and configure all UI widgets and connections.
    void createUI();

    // -- Image selection ------
    QComboBox* m_cmbBase          = nullptr;
    QComboBox* m_cmbTop           = nullptr;
    QComboBox* m_cmbMode          = nullptr;
    QComboBox* m_cmbTargetChannel = nullptr;
    QLabel*    m_lblTargetChannel = nullptr;

    // -- Blending parameters --
    QSlider* m_sldOpacity = nullptr;
    QSlider* m_sldLow     = nullptr;
    QSlider* m_sldHigh    = nullptr;
    QSlider* m_sldFeather = nullptr;

    QLabel* m_lblOpacity = nullptr;
    QLabel* m_lblLow     = nullptr;
    QLabel* m_lblHigh    = nullptr;
    QLabel* m_lblFeather = nullptr;

    // -- Preview options ------
    QCheckBox* m_chkShowPreview = nullptr;
    QCheckBox* m_chkHighRes     = nullptr;

    ImageViewer*        m_previewViewer = nullptr;
    ImageBlendingRunner m_runner;

    // -- State tracking -------
    bool m_initializing      = true;
    bool m_firstPreview      = true;
    int  m_lastPreviewWidth  = 0;
    int  m_lastPreviewHeight = 0;
};

#endif // IMAGEBLENDINGDIALOG_H