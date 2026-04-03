#pragma once

// =============================================================================
// BackgroundNeutralizationDialog.h
//
// Dialog for background neutralization. The user selects a reference region
// on the main image viewer, and per-channel offsets are computed and subtracted
// to equalize the RGB background levels.
// =============================================================================

#include "DialogBase.h"

#include <QPushButton>
#include <QLabel>
#include <QPointer>

#include "../ImageBuffer.h"
#include "../ImageViewer.h"

class MainWindowCallbacks;

class BackgroundNeutralizationDialog : public DialogBase {
    Q_OBJECT

public:
    explicit BackgroundNeutralizationDialog(QWidget* parent = nullptr);
    ~BackgroundNeutralizationDialog();

    /**
     * Performs background neutralization on the given image buffer.
     * Computes per-channel mean offsets from the selected rectangle,
     * then subtracts them to equalize background levels.
     *
     * @param img  Image buffer to modify in-place (must be 3-channel).
     * @param rect Reference region from which to sample background values.
     */
    static void neutralizeBackground(ImageBuffer& img, const QRect& rect);

    /** Enable or disable interactive region selection on the viewer. */
    void setInteractionEnabled(bool enabled);

    /** Set the active image viewer for region selection. */
    void setViewer(ImageViewer* viewer);

signals:
    /** Emitted when the user clicks Apply with a valid selection. */
    void apply(const QRect& rect);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onApply();
    void onRectSelected(const QRectF& r);

private:
    void setupUI();
    void setSelectionMode(bool active);

    // --- State ---
    QPointer<ImageViewer> m_activeViewer;
    QRect                 m_selection;
    bool                  m_hasSelection       = false;
    bool                  m_interactionEnabled = false;

    // --- UI widgets ---
    QLabel*      m_statusLabel;
    QPushButton* m_btnApply;
    QPushButton* m_btnCancel;
};