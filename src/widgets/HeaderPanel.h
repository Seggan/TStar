#ifndef HEADERPANEL_H
#define HEADERPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QLineEdit>

#include "../ImageBuffer.h"

// ---------------------------------------------------------------------------
// HeaderPanel
// A reusable widget that displays the raw FITS header cards (keyword, value,
// comment) of an ImageBuffer in a searchable, copyable table.
//
// Refactored from the original modal HeaderViewerDialog so that the header
// can be embedded as a panel inside the left sidebar.
// ---------------------------------------------------------------------------
class HeaderPanel : public QWidget {
    Q_OBJECT

public:
    explicit HeaderPanel(QWidget* parent = nullptr);

    // Populates the table with the given image metadata
    void setMetadata(const ImageBuffer::Metadata& meta);

    // Clears all rows from the table
    void clear();

private:
    void setupUI();
    void filterRows(const QString& text);
    void copySelectedToClipboard();

    ImageBuffer::Metadata m_meta;
    QTableWidget*         m_table;
    QLineEdit*            m_searchBox;
};

#endif // HEADERPANEL_H