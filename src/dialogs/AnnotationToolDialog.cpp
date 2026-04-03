#include "AnnotationToolDialog.h"

#include "../widgets/AnnotationOverlay.h"
#include "../ImageViewer.h"
#include "../MainWindow.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"

#include "../astrometry/AnnotationEngine.h"
#include "../astrometry/WCSUtils.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QInputDialog>
#include <QKeySequence>
#include <QMap>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QShowEvent>
#include <QTextStream>
#include <QVBoxLayout>

#include <cmath>

// =============================================================================
// Internal logging utility
// =============================================================================

/**
 * @brief Appends a timestamped message to the annotation debug log file.
 *        Used exclusively for diagnosing overlay lifecycle issues.
 */
static void logToFile(const QString& msg)
{
    QFile logFile(QDir::homePath() + "/TStar_annotation_debug.log");

    if (logFile.open(QIODevice::Append | QIODevice::Text))
    {
        QTextStream out(&logFile);
        out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
            << " | " << msg << "\n";
        logFile.close();
    }
}

// =============================================================================
// Internal helper: catalog deduplication
// =============================================================================

/**
 * @brief Merges @p news into @p current, skipping objects whose sky position
 *        falls within @p searchRadiusDeg of an already-present entry.
 *
 * When a duplicate is detected but the existing entry lacks ellipse geometry
 * data that the incoming entry provides, the geometry is transferred so that
 * the dominant catalog name is preserved while morphological detail is gained.
 */
static void deduplicateObjects(QVector<CatalogObject>&       current,
                               const QVector<CatalogObject>& incoming,
                               double searchRadiusDeg = 10.0 / 3600.0)
{
    for (const auto& n : incoming)
    {
        bool duplicate = false;

        for (auto& c : current)
        {
            if (std::abs(c.ra  - n.ra)  < searchRadiusDeg &&
                std::abs(c.dec - n.dec) < searchRadiusDeg)
            {
                duplicate = true;

                // Inherit ellipse data from the incoming entry if the existing
                // entry does not already carry it.
                if (c.minorDiameter <= 0 && n.minorDiameter > 0)
                {
                    c.minorDiameter = n.minorDiameter;
                    c.anglePA       = n.anglePA;

                    if (c.diameter <= 0)
                        c.diameter = n.diameter;
                }

                break;
            }
        }

        if (!duplicate)
            current.append(n);
    }
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

AnnotationToolDialog::AnnotationToolDialog(QWidget* parent)
    : DialogBase(parent, tr("Annotation Tool"))
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // -------------------------------------------------------------------------
    // Drawing tools group
    // -------------------------------------------------------------------------
    auto* toolsGroup  = new QGroupBox(tr("Drawing Tools"));
    auto* toolsLayout = new QHBoxLayout(toolsGroup);

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    auto makeToolButton = [](const QString& text, const QString& tip) -> QToolButton*
    {
        QToolButton* btn = new QToolButton();
        btn->setText(text);
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setMinimumWidth(50);
        return btn;
    };

    m_selectBtn = makeToolButton(tr("Select"), tr("Select / Pan (no drawing)"));
    m_selectBtn->setChecked(true);
    m_toolGroup->addButton(m_selectBtn, 0);

    m_circleBtn = makeToolButton(tr("Circle"), tr("Draw Circle"));
    m_toolGroup->addButton(m_circleBtn, 1);

    m_rectBtn = makeToolButton(tr("Rect"), tr("Draw Rectangle"));
    m_toolGroup->addButton(m_rectBtn, 2);

    m_arrowBtn = makeToolButton(tr("Arrow"), tr("Draw Arrow"));
    m_toolGroup->addButton(m_arrowBtn, 3);

    m_textBtn = makeToolButton(tr("Text"), tr("Add Text Label"));
    m_toolGroup->addButton(m_textBtn, 4);

    toolsLayout->addWidget(m_selectBtn);
    toolsLayout->addWidget(m_circleBtn);
    toolsLayout->addWidget(m_rectBtn);
    toolsLayout->addWidget(m_arrowBtn);
    toolsLayout->addWidget(m_textBtn);
    toolsLayout->addStretch();

    connect(m_toolGroup, &QButtonGroup::idClicked,
            this, &AnnotationToolDialog::onToolSelected);

    mainLayout->addWidget(toolsGroup);

    // -------------------------------------------------------------------------
    // Colour picker, undo/redo, and clear controls
    // -------------------------------------------------------------------------
    auto* optionsLayout = new QHBoxLayout();

    optionsLayout->addWidget(new QLabel(tr("Color:")));

    m_colorCombo = new QComboBox();
    m_colorCombo->addItem(tr("Yellow"), QColor(Qt::yellow));
    m_colorCombo->addItem(tr("Red"),    QColor(Qt::red));
    m_colorCombo->addItem(tr("Green"),  QColor(Qt::green));
    m_colorCombo->addItem(tr("Cyan"),   QColor(Qt::cyan));
    m_colorCombo->addItem(tr("White"),  QColor(Qt::white));

    connect(m_colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnnotationToolDialog::onColorChanged);
    optionsLayout->addWidget(m_colorCombo);

    m_undoBtn = new QPushButton(tr("Undo"));
    m_undoBtn->setEnabled(false);
    connect(m_undoBtn, &QPushButton::clicked, this, &AnnotationToolDialog::onUndo);
    optionsLayout->addWidget(m_undoBtn);

    m_redoBtn = new QPushButton(tr("Redo"));
    m_redoBtn->setEnabled(false);
    connect(m_redoBtn, &QPushButton::clicked, this, &AnnotationToolDialog::onRedo);
    optionsLayout->addWidget(m_redoBtn);

    optionsLayout->addStretch();

    m_clearBtn = new QPushButton(tr("Clear All"));
    connect(m_clearBtn, &QPushButton::clicked,
            this, &AnnotationToolDialog::onClearAnnotations);
    optionsLayout->addWidget(m_clearBtn);

    mainLayout->addLayout(optionsLayout);

    // -------------------------------------------------------------------------
    // Status label
    // -------------------------------------------------------------------------
    m_statusLabel = new QLabel(tr("Ready"));
    mainLayout->addWidget(m_statusLabel);

    // -------------------------------------------------------------------------
    // Catalog and overlay options group
    // -------------------------------------------------------------------------
    m_catGroup = new QGroupBox(tr("Catalogs && Grid"));
    auto* catLayout = new QGridLayout(m_catGroup);

    m_chkMessier        = new QCheckBox("Messier");
    m_chkNGC            = new QCheckBox("NGC");
    m_chkIC             = new QCheckBox("IC");
    m_chkLdN            = new QCheckBox("LdN");
    m_chkSh2            = new QCheckBox("Sh2");
    m_chkHyperLeda      = new QCheckBox("HyperLeda (PGC)");
    m_chkStars          = new QCheckBox(tr("Stars"));
    m_chkConstellations = new QCheckBox(tr("Constellations"));
    m_chkWcsGrid        = new QCheckBox(tr("WCS Grid (RA/Dec)"));
    m_chkCompass        = new QCheckBox(tr("Compass"));

    m_chkMessier->setChecked(true);
    m_chkNGC->setChecked(true);
    m_chkIC->setChecked(true);
    m_chkLdN->setChecked(true);
    m_chkSh2->setChecked(true);
    m_chkHyperLeda->setChecked(true);
    m_chkStars->setChecked(true);
    m_chkConstellations->setChecked(false);
    m_chkWcsGrid->setChecked(true);
    m_chkCompass->setChecked(true);

    // Compass position selector.
    m_cmbCompassPosition = new QComboBox();
    m_cmbCompassPosition->addItem(tr("Center"),
        static_cast<int>(AnnotationOverlay::CompassPosition::Center));
    m_cmbCompassPosition->addItem(tr("Top Left"),
        static_cast<int>(AnnotationOverlay::CompassPosition::TopLeft));
    m_cmbCompassPosition->addItem(tr("Top Right"),
        static_cast<int>(AnnotationOverlay::CompassPosition::TopRight));
    m_cmbCompassPosition->addItem(tr("Bottom Left"),
        static_cast<int>(AnnotationOverlay::CompassPosition::BottomLeft));
    m_cmbCompassPosition->addItem(tr("Bottom Right"),
        static_cast<int>(AnnotationOverlay::CompassPosition::BottomRight));
    m_cmbCompassPosition->setCurrentIndex(0);
    m_cmbCompassPosition->setToolTip(tr("Choose where the compass is placed on the image"));

    catLayout->addWidget(m_chkMessier,        0, 0);
    catLayout->addWidget(m_chkNGC,            0, 1);
    catLayout->addWidget(m_chkIC,             0, 2);
    catLayout->addWidget(m_chkLdN,            1, 0);
    catLayout->addWidget(m_chkSh2,            1, 1);
    catLayout->addWidget(m_chkHyperLeda,      1, 2);
    catLayout->addWidget(m_chkStars,          2, 0);
    catLayout->addWidget(m_chkConstellations, 2, 1, 1, 2);
    catLayout->addWidget(m_chkWcsGrid,        3, 0, 1, 1);
    catLayout->addWidget(m_chkCompass,        3, 1, 1, 1);
    catLayout->addWidget(m_cmbCompassPosition,3, 2, 1, 1);

    m_btnAnnotate = new QPushButton(tr("Annotate Image"));
    catLayout->addWidget(m_btnAnnotate, 4, 0, 1, 3);

    mainLayout->addWidget(m_catGroup);

    connect(m_btnAnnotate, &QPushButton::clicked,
            this, &AnnotationToolDialog::refreshAutomaticAnnotations);
    connect(m_chkWcsGrid, &QCheckBox::toggled,
            this, &AnnotationToolDialog::refreshAutomaticAnnotations);
    connect(m_chkCompass, &QCheckBox::toggled,
            this, &AnnotationToolDialog::refreshAutomaticAnnotations);
    connect(m_cmbCompassPosition,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnnotationToolDialog::refreshAutomaticAnnotations);

    // -------------------------------------------------------------------------
    // User guidance tip
    // -------------------------------------------------------------------------
    QLabel* tipLabel = new QLabel(
        tr("Tip: Annotations are saved as an overlay. They disappear when the "
           "tool is closed and reappear when it is reopened. Open this tool to "
           "continue editing with full undo/redo support. To burn annotations "
           "into the image, use File > Save while the tool is open."));
    tipLabel->setStyleSheet(
        "color: #AAAAAA; font-style: italic; "
        "border-top: 1px solid #444; padding-top: 5px;");
    tipLabel->setWordWrap(true);
    mainLayout->addWidget(tipLabel);

    mainLayout->addStretch();

    // -------------------------------------------------------------------------
    // Application-level keyboard shortcuts so undo/redo work even without focus.
    // -------------------------------------------------------------------------
    auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    undoShortcut->setContext(Qt::ApplicationShortcut);
    connect(undoShortcut, &QShortcut::activated, this, &AnnotationToolDialog::onUndo);

    auto* redoShortcut = new QShortcut(QKeySequence::Redo, this);
    redoShortcut->setContext(Qt::ApplicationShortcut);
    connect(redoShortcut, &QShortcut::activated, this, &AnnotationToolDialog::onRedo);

    setMinimumWidth(400);
    adjustSize();

    logToFile("[AnnotationToolDialog::CONSTRUCTOR] Dialog created");
}

AnnotationToolDialog::~AnnotationToolDialog()
{
    logToFile(QString("[AnnotationToolDialog::DESTRUCTOR] Dialog destroyed. "
                      "Saved annotations: %1")
                  .arg(m_savedAnnotations.size()));
    // Overlay state was already saved in the last hideEvent; nothing extra needed.
}

// =============================================================================
// Public interface
// =============================================================================

void AnnotationToolDialog::setViewer(ImageViewer* viewer)
{
    logToFile(QString("[setViewer] viewer=%1  existing overlay=%2")
                  .arg(viewer ? "valid" : "null")
                  .arg(m_overlay ? "exists" : "null"));

    m_viewer = viewer;

    if (!viewer)
    {
        m_statusLabel->setText(tr("No image loaded"));
        return;
    }

    // -------------------------------------------------------------------------
    // Check for a stale overlay that survived a previous dialog instance.
    // AnnotationOverlay is parented to the ImageViewer, so it outlives the
    // dialog that created it. If found, salvage its annotations then remove it.
    // -------------------------------------------------------------------------
    for (QObject* child : viewer->children())
    {
        AnnotationOverlay* old = qobject_cast<AnnotationOverlay*>(child);
        if (old)
        {
            logToFile("[setViewer] Found stale overlay from previous instance. Salvaging annotations.");
            m_savedAnnotations = old->annotations();
            delete old;
            break;
        }
    }

    // -------------------------------------------------------------------------
    // Create a new overlay or reuse the existing one if it already belongs to
    // this viewer.
    // -------------------------------------------------------------------------
    if (!m_overlay || m_overlay->parent() != viewer)
    {
        if (m_overlay)
        {
            logToFile("[setViewer] Overlay belongs to a different viewer. Migrating annotations.");
            m_savedAnnotations = m_overlay->annotations();
            delete m_overlay;
            m_overlay = nullptr;
        }

        logToFile(QString("[setViewer] Creating new overlay. "
                          "Saved annotations to restore: %1")
                      .arg(m_savedAnnotations.size()));

        m_overlay = new AnnotationOverlay(viewer);
        m_overlay->setGeometry(0, 0, viewer->width(), viewer->height());

        // Show or hide the overlay based on the current dialog visibility.
        if (this->isVisible())
        {
            m_overlay->show();
            m_overlay->setEnabled(true);
        }
        else
        {
            m_overlay->hide();
            m_overlay->setEnabled(false);
        }

        // ---------------------------------------------------------------------
        // Restore persisted annotations from MainWindow if available, otherwise
        // fall back to the local saved list.
        // ---------------------------------------------------------------------
        QWidget*     w       = parentWidget();
        MainWindow*  mainWin = nullptr;
        int          depth   = 0;

        while (w && !mainWin && depth < 10)
        {
            mainWin = qobject_cast<MainWindow*>(w);
            if (!mainWin)
                w = w->parentWidget();
            ++depth;
        }

        if (mainWin && !mainWin->m_persistedAnnotations.isEmpty())
        {
            logToFile(QString("[setViewer] Restoring %1 annotations from MainWindow.")
                          .arg(mainWin->m_persistedAnnotations.size()));
            m_overlay->setAnnotations(mainWin->m_persistedAnnotations);
            m_savedAnnotations = mainWin->m_persistedAnnotations;
            m_savedUndoStack   = mainWin->m_persistedUndoStack;
            m_savedRedoStack   = mainWin->m_persistedRedoStack;
        }
        else if (!m_savedAnnotations.isEmpty())
        {
            logToFile(QString("[setViewer] Restoring %1 annotations from local cache.")
                          .arg(m_savedAnnotations.size()));
            m_overlay->setAnnotations(m_savedAnnotations);
        }

        // Keep the overlay sized to the viewer at all times.
        connect(viewer, &ImageViewer::resized, this, [this]()
        {
            if (m_overlay && m_viewer)
                m_overlay->setGeometry(0, 0, m_viewer->width(), m_viewer->height());
        });

        connect(m_overlay, &AnnotationOverlay::aboutToAddAnnotation,
                this, &AnnotationToolDialog::onAboutToAddAnnotation);
        connect(m_overlay, &AnnotationOverlay::textInputRequested,
                this, &AnnotationToolDialog::onTextInputRequested);
    }
    else
    {
        // Overlay already targets this viewer; just sync visibility.
        logToFile(QString("[setViewer] Reusing existing overlay (%1 annotations).")
                      .arg(m_overlay->annotations().size()));

        if (this->isVisible())
        {
            m_overlay->show();
            m_overlay->setEnabled(true);
        }
        else
        {
            m_overlay->hide();
            m_overlay->setEnabled(false);
        }
    }

    syncOverlayDrawMode();

    // Auto-annotate when the image has valid WCS and no annotations exist yet.
    if (m_overlay && m_overlay->annotations().isEmpty() &&
        WCSUtils::hasValidWCS(m_viewer->getBuffer().metadata()))
    {
        refreshAutomaticAnnotations();
    }

    m_statusLabel->setText(tr("Ready to draw"));
}

void AnnotationToolDialog::refreshAutomaticAnnotations()
{
    if (!m_viewer || !m_overlay)
        return;

    const auto meta = m_viewer->getBuffer().metadata();

    if (!WCSUtils::hasValidWCS(meta))
    {
        QMessageBox::warning(this, tr("WCS Error"),
                             tr("The image must be plate-solved before annotating."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    // -------------------------------------------------------------------------
    // Locate the catalog data directory by probing a set of candidate paths.
    // -------------------------------------------------------------------------
    QString dataPath;

    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/data",
        QCoreApplication::applicationDirPath() + "/../Resources/data",
        QCoreApplication::applicationDirPath() + "/../data",
        QCoreApplication::applicationDirPath() + "/../../data",
        QDir::currentPath() + "/data"
    };

    for (const QString& p : candidates)
    {
        if (QDir(p).exists("messier.csv"))
        {
            dataPath = p;
            break;
        }
    }

    if (dataPath.isEmpty())
    {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, tr("Data Error"),
                             tr("Could not find the catalog data directory."));
        return;
    }

    m_currentWcsObjects.clear();

    const int w = m_viewer->getBuffer().width();
    const int h = m_viewer->getBuffer().height();

    // -------------------------------------------------------------------------
    // Load catalogs in priority order. Messier objects take naming precedence;
    // later catalogs inherit geometry from earlier duplicates when appropriate.
    // Stars use a tighter deduplication radius to avoid being merged with DSOs.
    // -------------------------------------------------------------------------

    if (m_chkMessier->isChecked())
        deduplicateObjects(m_currentWcsObjects,
            AnnotationEngine::loadCatalog(dataPath + "/messier.csv", meta, "Messier", w, h));

    if (m_chkNGC->isChecked())
        deduplicateObjects(m_currentWcsObjects,
            AnnotationEngine::loadCatalog(dataPath + "/ngc.csv", meta, "NGC", w, h));

    if (m_chkIC->isChecked())
        deduplicateObjects(m_currentWcsObjects,
            AnnotationEngine::loadCatalog(dataPath + "/ic.csv", meta, "IC", w, h));

    if (m_chkLdN->isChecked())
        deduplicateObjects(m_currentWcsObjects,
            AnnotationEngine::loadCatalog(dataPath + "/ldn.csv", meta, "LdN", w, h));

    if (m_chkSh2->isChecked())
        deduplicateObjects(m_currentWcsObjects,
            AnnotationEngine::loadCatalog(dataPath + "/sh2.csv", meta, "Sh2", w, h));

    if (m_chkHyperLeda->isChecked())
        deduplicateObjects(m_currentWcsObjects,
            AnnotationEngine::loadCatalog(dataPath + "/pgc.csv", meta, "PGC", w, h));

    if (m_chkStars->isChecked())
        deduplicateObjects(m_currentWcsObjects,
            AnnotationEngine::loadCatalog(dataPath + "/stars.csv", meta, "Star", w, h),
            10.0 / 3600.0);

    if (m_chkConstellations->isChecked())
    {
        m_currentWcsObjects.append(
            AnnotationEngine::loadConstellationLines(
                dataPath + "/constellations.csv", meta, w, h));

        deduplicateObjects(m_currentWcsObjects,
            AnnotationEngine::loadCatalog(
                dataPath + "/constellationsnames.csv", meta, "ConstellationName", w, h));
    }

    // -------------------------------------------------------------------------
    // Push all gathered data to the overlay.
    // -------------------------------------------------------------------------
    m_overlay->setWCSGridVisible(m_chkWcsGrid->isChecked());
    m_overlay->setCompassVisible(m_chkCompass->isChecked());
    m_overlay->setCompassPosition(
        static_cast<AnnotationOverlay::CompassPosition>(
            m_cmbCompassPosition->currentData().toInt()));

    m_overlay->setWCSObjects(m_currentWcsObjects);
    m_overlay->setWCSObjectsVisible(true);

    QApplication::restoreOverrideCursor();

    m_statusLabel->setText(
        tr("Annotations updated. Found %1 unique objects.")
            .arg(m_currentWcsObjects.size()));
}

QVector<Annotation> AnnotationToolDialog::saveAnnotations() const
{
    return m_overlay ? m_overlay->annotations() : QVector<Annotation>();
}

void AnnotationToolDialog::restoreAnnotations(const QVector<Annotation>& annotations)
{
    if (m_overlay)
        m_overlay->setAnnotations(annotations);
}

void AnnotationToolDialog::saveUndoRedoState()
{
    m_savedUndoStack = m_undoStack;
    m_savedRedoStack = m_redoStack;
}

void AnnotationToolDialog::restoreUndoRedoState()
{
    m_undoStack = m_savedUndoStack;
    m_redoStack = m_savedRedoStack;
    updateUndoRedoButtons();
}

void AnnotationToolDialog::renderAnnotations(QPainter& painter, const QRectF& imageRect)
{
    if (m_overlay)
        m_overlay->renderToPainter(painter, imageRect);
}

// =============================================================================
// Private helpers
// =============================================================================

void AnnotationToolDialog::syncOverlayDrawMode()
{
    if (!m_overlay || !m_toolGroup)
        return;

    int id = m_toolGroup->checkedId();

    if (id == -1)
    {
        m_selectBtn->setChecked(true);
        id = 0;
    }

    onToolSelected(id);
}

void AnnotationToolDialog::pushUndoState()
{
    if (!m_overlay)
        return;

    const QVector<Annotation> state = m_overlay->annotations();

    logToFile(QString("[pushUndoState] Pushing %1 annotations. Stack before: %2")
                  .arg(state.size()).arg(m_undoStack.size()));

    m_undoStack.push(state);
    m_redoStack.clear();

    // Cap history depth to prevent unbounded memory growth.
    while (m_undoStack.size() > 20)
        m_undoStack.removeFirst();

    logToFile(QString("[pushUndoState] Stack after: %1").arg(m_undoStack.size()));

    updateUndoRedoButtons();
}

void AnnotationToolDialog::updateUndoRedoButtons()
{
    m_undoBtn->setEnabled(!m_undoStack.isEmpty());
    m_redoBtn->setEnabled(!m_redoStack.isEmpty());
}

// =============================================================================
// Private slots
// =============================================================================

void AnnotationToolDialog::onToolSelected(int toolId)
{
    if (!m_overlay)
        return;

    switch (toolId)
    {
    case 0:
        m_overlay->setDrawMode(AnnotationOverlay::DrawMode::None);
        m_statusLabel->setText(tr("Select mode"));
        break;
    case 1:
        m_overlay->setDrawMode(AnnotationOverlay::DrawMode::Circle);
        m_statusLabel->setText(tr("Click and drag to draw a circle"));
        break;
    case 2:
        m_overlay->setDrawMode(AnnotationOverlay::DrawMode::Rectangle);
        m_statusLabel->setText(tr("Click and drag to draw a rectangle"));
        break;
    case 3:
        m_overlay->setDrawMode(AnnotationOverlay::DrawMode::Arrow);
        m_statusLabel->setText(tr("Click and drag to draw an arrow"));
        break;
    case 4:
        m_overlay->setDrawMode(AnnotationOverlay::DrawMode::Text);
        m_statusLabel->setText(tr("Click on the image to place a text label"));
        break;
    default:
        break;
    }
}

void AnnotationToolDialog::onTextInputRequested(const QPointF& imagePos)
{
    bool    ok   = false;
    QString text = QInputDialog::getText(this,
                                         tr("Text Label"),
                                         tr("Enter text:"),
                                         QLineEdit::Normal,
                                         m_pendingText, &ok);

    if (ok && !text.isEmpty())
    {
        m_pendingText = text;
        pushUndoState();

        if (m_overlay)
        {
            const QColor color =
                m_colorCombo->itemData(m_colorCombo->currentIndex()).value<QColor>();
            m_overlay->placeTextAt(imagePos, text, color);
        }

        m_statusLabel->setText(tr("Text label added. Click again to add more."));
    }
}

void AnnotationToolDialog::onClearAnnotations()
{
    if (!m_overlay)
        return;

    pushUndoState();
    m_overlay->clearManualAnnotations();
}

void AnnotationToolDialog::onColorChanged(int index)
{
    if (!m_overlay)
        return;

    const QColor color = m_colorCombo->itemData(index).value<QColor>();
    m_overlay->setDrawColor(color);
}

void AnnotationToolDialog::onAboutToAddAnnotation()
{
    // Capture current state before the overlay commits the new annotation.
    pushUndoState();
}

void AnnotationToolDialog::onUndo()
{
    if (m_undoStack.isEmpty() || !m_overlay)
        return;

    logToFile(QString("[onUndo] Overlay annotations: %1  Undo stack: %2")
                  .arg(m_overlay->annotations().size())
                  .arg(m_undoStack.size()));

    m_redoStack.push(m_overlay->annotations());
    const QVector<Annotation> prevState = m_undoStack.pop();
    m_overlay->setAnnotations(prevState);

    logToFile(QString("[onUndo] Restored %1 annotations.").arg(prevState.size()));

    updateUndoRedoButtons();
}

void AnnotationToolDialog::onRedo()
{
    if (m_redoStack.isEmpty() || !m_overlay)
        return;

    m_undoStack.push(m_overlay->annotations());
    const QVector<Annotation> redoState = m_redoStack.pop();
    m_overlay->setAnnotations(redoState);

    updateUndoRedoButtons();
}

// =============================================================================
// Protected event overrides
// =============================================================================

void AnnotationToolDialog::showEvent(QShowEvent* event)
{
    DialogBase::showEvent(event);

    logToFile("[showEvent] Dialog opening");
    logToFile(QString("[showEvent] Overlay annotations: %1")
                  .arg(m_overlay ? m_overlay->annotations().size() : 0));

    restoreUndoRedoState();

    logToFile(QString("[showEvent] Undo stack after restore: %1 entries")
                  .arg(m_undoStack.size()));

    // Push an initial state only when there is no prior history to restore.
    if (m_undoStack.isEmpty() && m_overlay && !m_overlay->annotations().isEmpty())
    {
        logToFile("[showEvent] No history found; seeding undo stack with current state.");
        m_undoStack.push(m_overlay->annotations());
    }

    if (m_overlay)
    {
        m_overlay->show();
        m_overlay->setEnabled(true);
        logToFile("[showEvent] Overlay shown and enabled");
    }

    updateUndoRedoButtons();
}

void AnnotationToolDialog::hideEvent(QHideEvent* event)
{
    logToFile("[hideEvent] Dialog closing");
    logToFile(QString("[hideEvent] Overlay annotations: %1  Undo: %2  Redo: %3")
                  .arg(m_overlay ? m_overlay->annotations().size() : 0)
                  .arg(m_undoStack.size())
                  .arg(m_redoStack.size()));

    // Persist the undo/redo history for the next show cycle.
    saveUndoRedoState();

    if (m_overlay)
    {
        m_savedAnnotations = m_overlay->annotations();

        logToFile(QString("[hideEvent] Saved %1 annotations.")
                      .arg(m_savedAnnotations.size()));

        // Disable drawing while the tool is closed.
        m_overlay->hide();
        m_overlay->setEnabled(false);

        // Walk the parent hierarchy to find MainWindow and persist state there
        // so it survives potential dialog destruction and re-creation.
        QWidget*    w       = parentWidget();
        MainWindow* mainWin = nullptr;
        int         depth   = 0;

        while (w && !mainWin && depth < 10)
        {
            logToFile(QString("[hideEvent] Checking parent[%1] = %2")
                          .arg(depth).arg(w->metaObject()->className()));
            mainWin = qobject_cast<MainWindow*>(w);
            if (!mainWin)
                w = w->parentWidget();
            ++depth;
        }

        if (mainWin)
        {
            mainWin->m_persistedAnnotations = m_savedAnnotations;
            mainWin->m_persistedUndoStack   = m_savedUndoStack;
            mainWin->m_persistedRedoStack   = m_savedRedoStack;

            logToFile(QString("[hideEvent] Persisted to MainWindow: "
                              "%1 annotations  %2 undo states  %3 redo states")
                          .arg(mainWin->m_persistedAnnotations.size())
                          .arg(mainWin->m_persistedUndoStack.size())
                          .arg(mainWin->m_persistedRedoStack.size()));
        }
        else
        {
            logToFile("[hideEvent] WARNING: Could not locate MainWindow in parent hierarchy.");
        }
    }

    logToFile(QString("[hideEvent] SavedUndo size: %1").arg(m_savedUndoStack.size()));

    DialogBase::hideEvent(event);
}