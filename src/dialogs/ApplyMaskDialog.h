#ifndef APPLYMASKDIALOG_H
#define APPLYMASKDIALOG_H

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QVBoxLayout>

#include "DialogBase.h"
#include "core/MaskLayer.h"

/**
 * @brief Dialog for selecting and previewing a mask to apply to the active image.
 *
 * Presents a list of available masks sourced from open viewer windows and
 * from the saved mask store, together with a scaled grayscale preview of
 * the currently selected entry. The caller retrieves the confirmed selection
 * via getSelectedMask() after the dialog is accepted.
 */
class ApplyMaskDialog : public DialogBase
{
    Q_OBJECT

public:
    ApplyMaskDialog(int targetWidth, int targetHeight, QWidget* parent = nullptr);

    /**
     * @brief Registers a mask for display in the selection list.
     * @param name    Display name for the mask entry.
     * @param mask    The mask data.
     * @param isView  True if the mask originates from an open viewer window.
     */
    void addAvailableMask(const QString& name, const MaskLayer& mask, bool isView = false);

    /** Returns the mask corresponding to the confirmed selection. */
    MaskLayer getSelectedMask() const;

private slots:
    void onSelectionChanged();

private:
    /** Renders the mask data into the preview label as a scaled grayscale image. */
    void updatePreview(const MaskLayer& mask);

    // --- Image dimensions for which the mask must be compatible -----------
    int m_targetWidth;
    int m_targetHeight;

    // --- UI controls ------------------------------------------------------
    QListWidget* m_listWidget;
    QLabel*      m_previewLabel;

    // --- Data storage -----------------------------------------------------
    QMap<QString, MaskLayer> m_availableMasks;
    MaskLayer                m_selectedMask;
};

#endif // APPLYMASKDIALOG_H