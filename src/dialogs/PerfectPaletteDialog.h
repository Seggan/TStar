#ifndef PERFECTPALETTEDIALOG_H
#define PERFECTPALETTEDIALOG_H

/**
 * @file PerfectPaletteDialog.h
 * @brief Narrowband palette composition dialog.
 *
 * Allows loading Ha, OIII, and SII narrowband channels, selecting from
 * predefined palette mappings (SHO, HOO, Foraxx, etc.), adjusting per-channel
 * intensities, and applying optional auto-stretch before compositing.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include <DialogBase.h>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>
#include <QScrollArea>
#include <QCheckBox>
#include <QPointer>

#include "../ImageBuffer.h"
#include "../algos/PerfectPaletteRunner.h"

class MainWindowCallbacks;
class ImageViewer;

class PerfectPaletteDialog : public DialogBase {
    Q_OBJECT

public:
    explicit PerfectPaletteDialog(QWidget* parent = nullptr);

    /** @brief Set the target viewer for channel loading context. */
    void setViewer(ImageViewer* v);

private slots:
    void onLoadChannel(const QString& channel);
    void onCreatePalettes();
    void onPaletteSelected(const QString& name);
    void onApply();
    void onIntensityChanged();
    void onStretchChanged();

private:
    void createUI();
    void updateThumbnails();

    // -- Core references --
    MainWindowCallbacks*    m_mainWin = nullptr;
    QPointer<ImageViewer>   m_viewer;
    PerfectPaletteRunner    m_runner;

    // -- Full-resolution channel buffers --
    ImageBuffer m_ha, m_oiii, m_sii;

    // -- Downscaled preview buffers for responsive UI --
    ImageBuffer m_previewHa, m_previewOiii, m_previewSii;

    // -- Channel status labels --
    QLabel* m_lblHa   = nullptr;
    QLabel* m_lblOiii = nullptr;
    QLabel* m_lblSii  = nullptr;

    // -- Preview and palette grid --
    QScrollArea*  m_scrollArea    = nullptr;
    QGridLayout*  m_gridPalettes  = nullptr;
    QLabel*       m_lblPreview    = nullptr;

    // -- Intensity sliders and value labels --
    QSlider* m_sliderHa   = nullptr;
    QSlider* m_sliderOiii = nullptr;
    QSlider* m_sliderSii  = nullptr;
    QLabel*  m_lblValHa   = nullptr;
    QLabel*  m_lblValOiii = nullptr;
    QLabel*  m_lblValSii  = nullptr;

    // -- Stretch controls --
    QCheckBox* m_chkAutoStretch  = nullptr;
    QSlider*   m_sliderStretch   = nullptr;
    QLabel*    m_lblStretchVal   = nullptr;

    // -- Palette selection state --
    QString m_selectedPalette;

    /** @brief Associates a palette button with its name for toggle management. */
    struct PaletteThumb {
        QPushButton* btn  = nullptr;
        QString      name;
    };
    QList<PaletteThumb> m_thumbs;
};

#endif // PERFECTPALETTEDIALOG_H