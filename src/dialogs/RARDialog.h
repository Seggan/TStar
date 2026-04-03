#ifndef RARDIALOG_H
#define RARDIALOG_H

/**
 * @file RARDialog.h
 * @brief Aberration Remover dialog using ONNX neural network inference.
 *
 * Provides a UI for configuring and running the RAR (Remove Aberrations)
 * tool, which uses an ONNX model to correct optical aberrations in
 * astronomical images. Supports CUDA, DirectML, and CPU execution providers.
 *
 * Copyright (C) 2026 TStar Team
 */

#include "DialogBase.h"

#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QPointer>

#include "../ImageViewer.h"

class MainWindowCallbacks;

/**
 * @class RARDialog
 * @brief Dialog for neural network-based optical aberration removal.
 */
class RARDialog : public DialogBase {
    Q_OBJECT

public:
    explicit RARDialog(QWidget* parent = nullptr);

    /** @brief Set the target image viewer for processing. */
    void setViewer(ImageViewer* v);

private slots:
    void onBrowseModel();
    void onDownloadModel();
    void onRun();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    /** @brief Persist user settings (model path, provider, patch params). */
    void saveSettings();

    // -- UI widgets --
    QLineEdit* m_editModelPath  = nullptr;
    QSpinBox*  m_spinPatch      = nullptr;
    QSpinBox*  m_spinOverlap    = nullptr;
    QComboBox* m_comboProvider   = nullptr;
    QLabel*    m_lblStatus       = nullptr;

    // -- Target viewer --
    QPointer<ImageViewer> m_viewer;
};

#endif // RARDIALOG_H