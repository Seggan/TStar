#include "HeaderPanel.h"

#include <QHeaderView>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QAbstractItemModel>

// ===========================================================================
// Construction
// ===========================================================================

HeaderPanel::HeaderPanel(QWidget* parent) : QWidget(parent)
{
    setupUI();
}

// ===========================================================================
// UI Setup
// ===========================================================================

void HeaderPanel::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // -- Search / filter field -------------------------------------------
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Filter FITS keys..."));
    m_searchBox->setStyleSheet(
        "padding: 4px; border: 1px solid #555; background: #333; color: white;"
    );
    connect(m_searchBox, &QLineEdit::textChanged, this, &HeaderPanel::filterRows);
    layout->addWidget(m_searchBox);

    // -- Header table ----------------------------------------------------
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Key"), tr("Value"), tr("Comment")});

    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setStretchLastSection(true);

    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(true);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    m_table->setStyleSheet(
        "QTableWidget                { background: #222; color: #ddd; gridline-color: #444; }"
        "QHeaderView::section        { background: #333; color: #eee; padding: 4px; border: 1px solid #444; }"
        "QTableWidget::item          { padding: 4px; }"
        "QTableWidget::item:selected { background: #0055aa; }"
    );

    layout->addWidget(m_table, 1);

    // -- Context menu: copy selected rows --------------------------------
    connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu;
        menu.addAction(tr("Copy Selected"), this, &HeaderPanel::copySelectedToClipboard);
        menu.exec(m_table->mapToGlobal(pos));
    });

    // Ctrl+C shortcut for the same copy action
    auto* copyAction = new QAction(this);
    copyAction->setShortcut(Qt::CTRL | Qt::Key_C);
    m_table->addAction(copyAction);
    connect(copyAction, &QAction::triggered, this, &HeaderPanel::copySelectedToClipboard);
}

// ===========================================================================
// Public Interface
// ===========================================================================

void HeaderPanel::setMetadata(const ImageBuffer::Metadata& meta)
{
    m_meta = meta;
    m_table->setRowCount(0);
    m_table->setRowCount(static_cast<int>(m_meta.rawHeaders.size()));

    for (size_t i = 0; i < m_meta.rawHeaders.size(); ++i) {
        const auto& card = m_meta.rawHeaders[i];

        auto* k = new QTableWidgetItem(card.key);
        auto* v = new QTableWidgetItem(card.value);
        auto* c = new QTableWidgetItem(card.comment);

        // Make cells read-only
        const Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        k->setFlags(flags);
        v->setFlags(flags);
        c->setFlags(flags);

        m_table->setItem(static_cast<int>(i), 0, k);
        m_table->setItem(static_cast<int>(i), 1, v);
        m_table->setItem(static_cast<int>(i), 2, c);
    }

    m_table->resizeRowsToContents();

    // Re-apply any active search filter after the table is repopulated
    if (!m_searchBox->text().isEmpty())
        filterRows(m_searchBox->text());
}

void HeaderPanel::clear()
{
    m_table->setRowCount(0);
}

// ===========================================================================
// Private Helpers
// ===========================================================================

void HeaderPanel::filterRows(const QString& text)
{
    if (text.isEmpty()) {
        for (int i = 0; i < m_table->rowCount(); ++i)
            m_table->setRowHidden(i, false);
        return;
    }

    const QString lower = text.toLower();

    for (int i = 0; i < m_table->rowCount(); ++i) {
        const bool match =
            m_table->item(i, 0)->text().toLower().contains(lower) ||
            m_table->item(i, 1)->text().toLower().contains(lower) ||
            m_table->item(i, 2)->text().toLower().contains(lower);

        m_table->setRowHidden(i, !match);
    }
}

void HeaderPanel::copySelectedToClipboard()
{
    const QModelIndexList selected = m_table->selectionModel()->selectedRows();
    if (selected.isEmpty()) return;

    QString text;
    for (const QModelIndex& index : selected) {
        const int     row     = index.row();
        const QString key     = m_table->item(row, 0)->text();
        const QString value   = m_table->item(row, 1)->text();
        const QString comment = m_table->item(row, 2)->text();

        text += key + " = " + value;
        if (!comment.isEmpty()) text += " / " + comment;
        text += "\n";
    }

    QApplication::clipboard()->setText(text);
}