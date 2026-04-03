#include "HeaderEditorDialog.h"
#include "../ImageViewer.h"
#include "../MainWindow.h"
#include "../widgets/CustomMdiSubWindow.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HeaderEditorDialog::HeaderEditorDialog(ImageViewer* viewer, QWidget* parent)
    : DialogBase(parent, tr("Header Editor"), 700, 600)
    , m_viewer(viewer)
{
    if (viewer) {
        m_meta = viewer->getBuffer().metadata();
        setWindowTitle(tr("FITS Header Editor - %1").arg(viewer->windowTitle()));
    }

    setupUI();
    loadMetadata();
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void HeaderEditorDialog::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    // -- Input row for adding / updating a header card ----------------------
    QGroupBox*   addGroup  = new QGroupBox(tr("Add/Update Key"), this);
    QHBoxLayout* addLayout = new QHBoxLayout(addGroup);

    m_keyInput = new QLineEdit(this);
    m_keyInput->setPlaceholderText(tr("KEYWORD"));
    m_keyInput->setMaxLength(8);  // FITS standard keyword length

    m_valInput = new QLineEdit(this);
    m_valInput->setPlaceholderText(tr("Value"));

    m_commentInput = new QLineEdit(this);
    m_commentInput->setPlaceholderText(tr("Comment"));

    m_addBtn = new QPushButton(tr("Set"), this);
    connect(m_addBtn, &QPushButton::clicked, this, &HeaderEditorDialog::onAdd);

    addLayout->addWidget(m_keyInput, 1);
    addLayout->addWidget(m_valInput, 2);
    addLayout->addWidget(m_commentInput, 2);
    addLayout->addWidget(m_addBtn);

    layout->addWidget(addGroup);

    // -- Header table -------------------------------------------------------
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Key"), tr("Value"), tr("Comment")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);

    // Populate input fields when a row is selected.
    connect(m_table, &QTableWidget::itemSelectionChanged, [this]() {
        auto items = m_table->selectedItems();
        if (items.size() >= 3) {
            const int row = items[0]->row();
            m_keyInput->setText(m_table->item(row, 0)->text());
            m_valInput->setText(m_table->item(row, 1)->text());
            m_commentInput->setText(m_table->item(row, 2)->text());
        }
    });

    layout->addWidget(m_table);

    // -- Button bar ---------------------------------------------------------
    QHBoxLayout* btnLayout = new QHBoxLayout();

    m_delBtn = new QPushButton(tr("Delete Selected"), this);
    connect(m_delBtn, &QPushButton::clicked, this, &HeaderEditorDialog::onDelete);

    m_importBtn = new QPushButton(tr("Import..."), this);
    connect(m_importBtn, &QPushButton::clicked, this, &HeaderEditorDialog::onImport);

    m_saveBtn = new QPushButton(tr("Save Changes"), this);
    connect(m_saveBtn, &QPushButton::clicked, this, &HeaderEditorDialog::onSave);

    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addWidget(m_delBtn);
    btnLayout->addWidget(m_importBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(m_saveBtn);

    layout->addLayout(btnLayout);
}

// ---------------------------------------------------------------------------
// Metadata helpers
// ---------------------------------------------------------------------------

void HeaderEditorDialog::loadMetadata()
{
    m_table->setRowCount(0);
    m_table->setRowCount(static_cast<int>(m_meta.rawHeaders.size()));

    for (size_t i = 0; i < m_meta.rawHeaders.size(); ++i) {
        const auto& card = m_meta.rawHeaders[i];
        m_table->setItem(static_cast<int>(i), 0, new QTableWidgetItem(card.key));
        m_table->setItem(static_cast<int>(i), 1, new QTableWidgetItem(card.value));
        m_table->setItem(static_cast<int>(i), 2, new QTableWidgetItem(card.comment));
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void HeaderEditorDialog::onAdd()
{
    const QString key     = m_keyInput->text().trimmed().toUpper();
    const QString val     = m_valInput->text();
    const QString comment = m_commentInput->text();

    if (key.isEmpty()) return;

    // Update existing card or append a new one.
    bool found = false;
    for (auto& card : m_meta.rawHeaders) {
        if (card.key == key) {
            card.value   = val;
            card.comment = comment;
            found = true;
            break;
        }
    }

    if (!found) {
        m_meta.rawHeaders.push_back({key, val, comment});
    }

    loadMetadata();
}

void HeaderEditorDialog::onDelete()
{
    const int row = m_table->currentRow();
    if (row < 0) return;

    const QString key = m_table->item(row, 0)->text();

    for (auto it = m_meta.rawHeaders.begin(); it != m_meta.rawHeaders.end(); ++it) {
        if (it->key == key) {
            m_meta.rawHeaders.erase(it);
            break;
        }
    }

    loadMetadata();
}

void HeaderEditorDialog::onSave()
{
    if (!m_viewer) return;

    // Apply metadata changes to the in-memory buffer.
    ImageBuffer& buf = m_viewer->getBuffer();
    buf.setMetadata(m_meta);

    // Ask the user whether to persist to disk.
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("Save File"),
        tr("Apply changes and save file?\n"
           "Warning: This overwrites the existing file if you choose Yes.\n"
           "Choose No to only apply to memory (you can Save As later)."),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (reply == QMessageBox::Cancel) return;

    if (reply == QMessageBox::Yes) {
        QString path = QFileDialog::getSaveFileName(
            this, tr("Save FITS"), QString(), "FITS Files (*.fits)");
        if (!path.isEmpty()) {
            QString err;
            if (!buf.save(path, "FITS", ImageBuffer::Depth_32Float, &err)) {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Failed to save: %1").arg(err));
            } else {
                QMessageBox::information(this, tr("Success"), tr("File saved."));
            }
        }
    }

    m_viewer->setModified(true);
    accept();
}

void HeaderEditorDialog::onImport()
{
    MainWindow* mainWin = qobject_cast<MainWindow*>(parent());
    if (!mainWin) return;

    // Build a list of other open images whose headers can be imported.
    QStringList                items;
    std::vector<ImageViewer*>  viewers;

    const auto sublist = mainWin->findChildren<CustomMdiSubWindow*>();
    for (auto* sub : sublist) {
        if (sub->viewer() && sub->viewer() != m_viewer) {
            items << sub->windowTitle();
            viewers.push_back(sub->viewer());
        }
    }

    if (items.isEmpty()) {
        QMessageBox::information(this, tr("Import Header"),
                                 tr("No other open images to import from."));
        return;
    }

    bool ok = false;
    const QString item = QInputDialog::getItem(
        this, tr("Import Header"),
        tr("Select Image to import header from:"),
        items, 0, false, &ok);

    if (ok && !item.isEmpty()) {
        const int idx = items.indexOf(item);
        if (idx >= 0 && static_cast<size_t>(idx) < viewers.size()) {
            m_meta = viewers[idx]->getBuffer().metadata();
            loadMetadata();
            QMessageBox::information(
                this, tr("Import Header"),
                tr("Header imported successfully. Click Save Changes to apply."));
        }
    }
}