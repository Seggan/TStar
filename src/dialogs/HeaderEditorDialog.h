#ifndef HEADEREDITORDIALOG_H
#define HEADEREDITORDIALOG_H

#include "DialogBase.h"
#include "ImageBuffer.h"

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

class ImageViewer;

/**
 * @brief Dialog for inspecting and editing FITS header cards.
 *
 * Provides a table view of all header key/value/comment triples, with
 * the ability to add, modify, delete, and import headers from other
 * open images.  Changes can be persisted to disk as a FITS file.
 */
class HeaderEditorDialog : public DialogBase {
    Q_OBJECT

public:
    explicit HeaderEditorDialog(ImageViewer* viewer, QWidget* parent = nullptr);

private slots:
    /** @brief Add or update a header card from the input fields. */
    void onAdd();

    /** @brief Delete the currently selected header card. */
    void onDelete();

    /** @brief Commit changes to the ImageViewer buffer and optionally save to file. */
    void onSave();

    /** @brief Import header cards from another open image. */
    void onImport();

private:
    /** @brief Build the dialog layout (input row, table, button bar). */
    void setupUI();

    /** @brief Refresh the table widget from m_meta. */
    void loadMetadata();

    // -- State --
    ImageViewer*          m_viewer;  ///< Viewer whose buffer is being edited.
    ImageBuffer::Metadata m_meta;   ///< Working copy of the metadata.

    // -- Widgets --
    QTableWidget* m_table;
    QPushButton*  m_addBtn;
    QPushButton*  m_delBtn;
    QPushButton*  m_importBtn;
    QPushButton*  m_saveBtn;
    QLineEdit*    m_keyInput;
    QLineEdit*    m_valInput;
    QLineEdit*    m_commentInput;
};

#endif // HEADEREDITORDIALOG_H