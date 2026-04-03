// =============================================================================
// HeaderViewerDialog.cpp
// Implementation of the FITS/XISF header viewer with word-wrap support and
// live keyword filtering.
// =============================================================================

#include "HeaderViewerDialog.h"

#include <QHeaderView>
#include <QVBoxLayout>
#include <QTimer>
#include <QTableWidgetItem>
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QPainter>

// =============================================================================
// HeaderWordWrapDelegate
// Custom item delegate that enables proper word wrapping inside QTableWidget
// cells. The default delegate truncates long text; this paints the full text
// with Qt::TextWordWrap and computes an appropriate size hint.
// =============================================================================

class HeaderWordWrapDelegate : public QStyledItemDelegate {
public:
    explicit HeaderWordWrapDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    // -- Painting -------------------------------------------------------------
    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();

        // Draw cell background (handles selection highlight).
        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, opt.palette.highlight());
        } else {
            painter->fillRect(opt.rect, opt.palette.base());
        }

        // Draw cell text with word wrapping and internal padding.
        const QRect textRect = opt.rect.adjusted(4, 4, -4, -4);
        const QColor textColor = (opt.state & QStyle::State_Selected)
            ? opt.palette.highlightedText().color()
            : opt.palette.text().color();
        painter->setPen(textColor);
        painter->drawText(textRect,
                          Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                          opt.text);

        painter->restore();
    }

    // -- Size hint ------------------------------------------------------------
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        // Determine the available column width from the owning table widget.
        const QTableWidget* table =
            qobject_cast<const QTableWidget*>(option.widget);
        int columnWidth = 200; // fallback if the widget is unavailable
        if (table) {
            columnWidth = table->columnWidth(index.column());
        }
        columnWidth -= 8; // account for internal padding

        // Measure how much vertical space the wrapped text requires.
        QFontMetrics fm(opt.font);
        QRect bound = fm.boundingRect(
            QRect(0, 0, columnWidth, 10000),
            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
            opt.text);

        return QSize(columnWidth, bound.height() + 10);
    }
};

// =============================================================================
// Construction
// =============================================================================

HeaderViewerDialog::HeaderViewerDialog(const ImageBuffer::Metadata& meta,
                                       QWidget* parent)
    : DialogBase(parent, tr("FITS Header"), 800, 600)
    , m_meta(meta)
{
    setupUI();

    // Centre the dialog over its parent window.
    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

// =============================================================================
// UI construction
// =============================================================================

void HeaderViewerDialog::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(5);

    // -- Search / filter box --------------------------------------------------
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Filter keys..."));
    m_searchBox->setStyleSheet(
        "padding: 5px; border: 1px solid #555; "
        "background: #333; color: white;");
    connect(m_searchBox, &QLineEdit::textChanged,
            this, &HeaderViewerDialog::filterRows);
    layout->addWidget(m_searchBox);

    // -- Header table ---------------------------------------------------------
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        { tr("Key"), tr("Value"), tr("Comment") });

    // Column resize behaviour.
    m_table->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);

    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);

    m_table->setStyleSheet(
        "QTableWidget { background: #222; color: #ddd; "
        "  gridline-color: #444; outline: 0; }"
        "QHeaderView::section { background: #333; color: #eee; "
        "  padding: 4px; border: 1px solid #444; }"
        "QTableWidget::item { padding: 6px; "
        "  border-bottom: 1px solid #333; }"
        "QTableWidget::item:selected { background: #0055aa; }");

    // Enable word wrapping and auto row height.
    m_table->setWordWrap(true);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->verticalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);

    // -- Populate rows from raw header cards ----------------------------------
    const int rowCount = static_cast<int>(m_meta.rawHeaders.size());
    m_table->setRowCount(rowCount);

    for (int i = 0; i < rowCount; ++i) {
        const auto& card = m_meta.rawHeaders[i];

        auto* keyItem     = new QTableWidgetItem(card.key);
        auto* valueItem   = new QTableWidgetItem(card.value);
        auto* commentItem = new QTableWidgetItem(card.comment);

        // Items are read-only but selectable.
        const Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        keyItem->setFlags(flags);
        valueItem->setFlags(flags);
        commentItem->setFlags(flags);

        m_table->setItem(i, 0, keyItem);
        m_table->setItem(i, 1, valueItem);
        m_table->setItem(i, 2, commentItem);
    }

    // Assign word-wrap delegates to Value and Comment columns.
    m_table->setItemDelegateForColumn(
        1, new HeaderWordWrapDelegate(m_table));
    m_table->setItemDelegateForColumn(
        2, new HeaderWordWrapDelegate(m_table));

    // Set initial key column width; value column stretches automatically.
    m_table->setColumnWidth(0, 150);
    m_table->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);

    // Trigger deferred row resize after layout has been computed so that
    // word-wrap height calculations are accurate.
    QTimer::singleShot(50, this, [this]() {
        m_table->resizeRowsToContents();
    });

    layout->addWidget(m_table);
}

// =============================================================================
// Row filtering
// =============================================================================

void HeaderViewerDialog::filterRows(const QString& text)
{
    // Show all rows when the filter is empty.
    if (text.isEmpty()) {
        for (int i = 0; i < m_table->rowCount(); ++i) {
            m_table->setRowHidden(i, false);
        }
        return;
    }

    const QString lower = text.toLower();

    for (int i = 0; i < m_table->rowCount(); ++i) {
        bool match = false;
        for (int col = 0; col < 3 && !match; ++col) {
            if (m_table->item(i, col)->text().toLower().contains(lower)) {
                match = true;
            }
        }
        m_table->setRowHidden(i, !match);
    }
}

// =============================================================================
// Event overrides
// =============================================================================

void HeaderViewerDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // Re-calculate row heights once the widget is visible and has valid
    // geometry, ensuring word-wrap layout is correct.
    QTimer::singleShot(100, this, [this]() {
        m_table->resizeRowsToContents();
    });
}

// =============================================================================
// Viewer binding
// =============================================================================

#include "../ImageViewer.h"

void HeaderViewerDialog::setViewer(ImageViewer* v)
{
    if (!v || !v->getBuffer().isValid()) return;

    m_meta = v->getBuffer().metadata();

    // Derive a meaningful window title from the viewer.
    QString title = v->windowTitle();
    if (title.isEmpty()) title = tr("Image");
    setWindowTitle(tr("FITS/XISF Header: %1").arg(title));

    // Re-populate the table with the new metadata.
    const int rowCount = static_cast<int>(m_meta.rawHeaders.size());
    m_table->setRowCount(0);
    m_table->setRowCount(rowCount);

    for (int i = 0; i < rowCount; ++i) {
        const auto& card = m_meta.rawHeaders[i];

        auto* keyItem     = new QTableWidgetItem(card.key);
        auto* valueItem   = new QTableWidgetItem(card.value);
        auto* commentItem = new QTableWidgetItem(card.comment);

        const Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        keyItem->setFlags(flags);
        valueItem->setFlags(flags);
        commentItem->setFlags(flags);

        m_table->setItem(i, 0, keyItem);
        m_table->setItem(i, 1, valueItem);
        m_table->setItem(i, 2, commentItem);
    }

    // Re-apply any active search filter and update row heights.
    filterRows(m_searchBox->text());
    m_table->resizeRowsToContents();
}