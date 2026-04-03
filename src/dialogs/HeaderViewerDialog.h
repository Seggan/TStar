#ifndef HEADERVIEWERDIALOG_H
#define HEADERVIEWERDIALOG_H

// =============================================================================
// HeaderViewerDialog.h
// Dialog for displaying FITS/XISF header metadata in a searchable table view.
// =============================================================================

#include <QDialog>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QLineEdit>

#include "ImageBuffer.h"
#include "DialogBase.h"

class ImageViewer;

class HeaderViewerDialog : public DialogBase {
    Q_OBJECT

public:
    explicit HeaderViewerDialog(const ImageBuffer::Metadata& meta,
                                QWidget* parent = nullptr);

    // Update the dialog contents to reflect a different viewer's metadata.
    void setViewer(ImageViewer* v);

protected:
    void showEvent(QShowEvent* event) override;

private:
    // Build and configure all UI widgets.
    void setupUI();

    // Show or hide table rows based on the filter text (case-insensitive).
    void filterRows(const QString& text);

    ImageBuffer::Metadata m_meta;
    QTableWidget*         m_table     = nullptr;
    QLineEdit*            m_searchBox = nullptr;
};

#endif // HEADERVIEWERDIALOG_H