#ifndef BINNINGDIALOG_H
#define BINNINGDIALOG_H

// =============================================================================
// BinningDialog.h
//
// Simple dialog for applying pixel binning (1x1, 2x2, 3x3) to reduce
// image resolution by averaging superpixel groups.
// =============================================================================

#include "DialogBase.h"

#include <QPointer>
#include <QComboBox>
#include <QPushButton>

class ImageViewer;

class BinningDialog : public DialogBase {
    Q_OBJECT

public:
    explicit BinningDialog(QWidget* parent = nullptr);
    ~BinningDialog() = default;

    /** Set the target image viewer (may be overridden at apply time). */
    void setViewer(ImageViewer* v);

private slots:
    void onApply();

private:
    QPointer<ImageViewer> m_viewer;
    QComboBox*            m_binCombo;
};

#endif // BINNINGDIALOG_H