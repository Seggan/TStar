#ifndef ANNOTATION_TOOL_DIALOG_H
#define ANNOTATION_TOOL_DIALOG_H

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStack>
#include <QToolButton>
#include <QVector>

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../ImageViewer.h"

class AnnotationOverlay;
class MainWindowCallbacks;
struct Annotation;
struct CatalogObject;

/**
 * @brief Non-modal dialog providing manual drawing tools and automatic
 *        catalog-based annotation for plate-solved images.
 *
 * Manual tools (circle, rectangle, arrow, text) are drawn onto an
 * AnnotationOverlay widget that is parented to the active ImageViewer.
 * Automatic annotations are sourced from bundled catalog CSV files and
 * projected onto the image using the WCS metadata.
 *
 * The dialog maintains a full undo/redo history and persists both the
 * annotation state and the history stacks across hide/show cycles by
 * serialising them into MainWindow::m_persisted* fields.
 */
class AnnotationToolDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit AnnotationToolDialog(QWidget* parent = nullptr);
    ~AnnotationToolDialog();

    /** Attaches the dialog to a new viewer, creating or migrating the overlay. */
    void setViewer(ImageViewer* viewer);

    /**
     * @brief Renders all current annotations via @p painter into @p imageRect.
     *        Called by the viewer's paint event when annotations should be
     *        burned into an exported image.
     */
    void renderAnnotations(QPainter& painter, const QRectF& imageRect);

    // --- Annotation persistence ------------------------------------------

    /** Returns the current overlay annotation list, or an empty vector. */
    QVector<Annotation> saveAnnotations() const;

    /** Replaces the overlay annotation list with @p annotations. */
    void restoreAnnotations(const QVector<Annotation>& annotations);

    /** Saves the active undo and redo stacks for later restoration. */
    void saveUndoRedoState();

    /** Restores the previously saved undo and redo stacks. */
    void restoreUndoRedoState();

    /** Returns the annotation list saved at the last hide event. */
    QVector<Annotation> getPersistedAnnotations() const
        { return m_savedAnnotations; }

    /** Sets the annotation list to restore at the next setViewer() call. */
    void setPersistedAnnotations(const QVector<Annotation>& annotations)
        { m_savedAnnotations = annotations; }

    /** Reloads catalog and WCS data and regenerates automatic annotations. */
    void refreshAutomaticAnnotations();

private slots:
    void onToolSelected(int toolId);
    void onClearAnnotations();
    void onColorChanged(int index);

    /** Called by the overlay signal immediately before a new annotation is committed. */
    void onAboutToAddAnnotation();

    /** Called when the user clicks in text mode; prompts for the label string. */
    void onTextInputRequested(const QPointF& imagePos);

    void onUndo();
    void onRedo();

private:
    void pushUndoState();
    void updateUndoRedoButtons();

    /** Synchronises the overlay draw mode with the currently checked tool button. */
    void syncOverlayDrawMode();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // --- Viewer and overlay ----------------------------------------------
    QPointer<ImageViewer>      m_viewer;
    QPointer<AnnotationOverlay> m_overlay;

    // --- Drawing tool buttons --------------------------------------------
    QButtonGroup* m_toolGroup   = nullptr;
    QToolButton*  m_selectBtn   = nullptr;
    QToolButton*  m_circleBtn   = nullptr;
    QToolButton*  m_rectBtn     = nullptr;
    QToolButton*  m_arrowBtn    = nullptr;
    QToolButton*  m_textBtn     = nullptr;

    // --- Catalog filter checkboxes ---------------------------------------
    QGroupBox* m_catGroup          = nullptr;
    QCheckBox* m_chkMessier        = nullptr;
    QCheckBox* m_chkNGC            = nullptr;
    QCheckBox* m_chkIC             = nullptr;
    QCheckBox* m_chkLdN            = nullptr;
    QCheckBox* m_chkSh2            = nullptr;
    QCheckBox* m_chkHyperLeda      = nullptr;
    QCheckBox* m_chkStars          = nullptr;
    QCheckBox* m_chkConstellations = nullptr;
    QPushButton* m_btnAnnotate     = nullptr;

    // --- WCS grid and compass controls -----------------------------------
    QCheckBox* m_chkWcsGrid         = nullptr;
    QCheckBox* m_chkCompass         = nullptr;
    QComboBox* m_cmbCompassPosition = nullptr;

    // --- Appearance options ----------------------------------------------
    QComboBox*   m_colorCombo = nullptr;
    QPushButton* m_clearBtn   = nullptr;
    QPushButton* m_undoBtn    = nullptr;
    QPushButton* m_redoBtn    = nullptr;

    // --- Status display --------------------------------------------------
    QLabel* m_statusLabel = nullptr;

    // --- Catalog data ----------------------------------------------------
    QVector<CatalogObject> m_currentWcsObjects;

    // --- Text tool state -------------------------------------------------
    QString m_pendingText = QStringLiteral("Label");

    // --- Undo / redo history stacks -------------------------------------
    QStack<QVector<Annotation>> m_undoStack;
    QStack<QVector<Annotation>> m_redoStack;

    // Copies of the stacks persisted across hide/show cycles.
    QStack<QVector<Annotation>> m_savedUndoStack;
    QStack<QVector<Annotation>> m_savedRedoStack;

    // Annotation list persisted when the overlay widget is destroyed.
    QVector<Annotation> m_savedAnnotations;
};

#endif // ANNOTATION_TOOL_DIALOG_H