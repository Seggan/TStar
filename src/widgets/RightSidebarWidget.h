#ifndef RIGHTSIDEBARWIDGET_H
#define RIGHTSIDEBARWIDGET_H

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QPushButton>
#include <QVariantAnimation>
#include <QLabel>
#include <QMap>
#include <QCheckBox>

class CustomMdiSubWindow;
class QEnterEvent;
class QEvent;

/**
 * @brief Collapsible right-side panel that displays thumbnail previews of
 *        shaded (collapsed) image subwindows.
 *
 * A narrow tab strip on the right edge toggles the content area, which slides
 * in from the right using a QVariantAnimation. Each thumbnail is rendered at a
 * fixed size (THUMB_W x THUMB_H) so that varying title lengths do not affect
 * the layout. The panel's right edge is anchored to the viewport boundary via
 * setAnchorGeometry(), which must be called from MainWindow::resizeEvent().
 */
class RightSidebarWidget : public QWidget
{
    Q_OBJECT

public:
    static constexpr int THUMB_W = 160;  ///< Fixed thumbnail display width  (px)
    static constexpr int THUMB_H = 120;  ///< Fixed thumbnail display height (px)

    explicit RightSidebarWidget(QWidget* parent = nullptr);

    /** Returns the total widget width when fully expanded (tab strip + content). */
    int totalVisibleWidth() const;

    /**
     * @brief Anchors the right edge of this widget to a fixed viewport coordinate.
     *
     * Call this from MainWindow::resizeEvent() whenever the MDI area changes size.
     * @param rightX  X coordinate of the right anchor edge in parent coordinates.
     * @param topY    Y coordinate of the top edge.
     * @param h       Desired widget height.
     */
    void setAnchorGeometry(int rightX, int topY, int h);

    /**
     * @brief Registers a shaded subwindow and adds a thumbnail tile to the list.
     * @param sub               The subwindow that was shaded.
     * @param thumb             Pixmap snapshot taken just before shading.
     * @param title             Display name shown below the thumbnail.
     * @param creationSortIndex Optional sort key; tiles are ordered ascending.
     */
    void addThumbnail(CustomMdiSubWindow* sub,
                      const QPixmap&      thumb,
                      const QString&      title,
                      int                 creationSortIndex = -1);

    /**
     * @brief Removes the thumbnail tile associated with a subwindow.
     *
     * Called when a shaded subwindow is restored or closed.
     */
    void removeThumbnail(CustomMdiSubWindow* sub);

    /** Programmatically collapses the content panel. */
    void collapse() { setExpanded(false); }

    bool isExpanded()                const { return m_expanded; }
    bool isHideMinimizedViewsEnabled() const { return m_hideMinimizedViews; }

signals:
    /** Emitted whenever the panel is expanded or collapsed. */
    void expandedToggled(bool expanded);

    /** Emitted when the user clicks a thumbnail tile. */
    void thumbnailActivated(CustomMdiSubWindow* sub);

    /** Emitted when the "Hide minimized views" checkbox state changes. */
    void hideMinimizedViewsToggled(bool hidden);

private slots:
    void onTabClicked();

private:
    void setExpanded(bool expanded);

    // Tab strip (right edge column)
    QWidget*     m_tabContainer = nullptr;
    QPushButton* m_tabBtn       = nullptr;

    // Sliding content area
    QWidget*      m_contentWrapper   = nullptr;
    QScrollArea*  m_contentContainer = nullptr;
    QWidget*      m_listWidget       = nullptr;
    QVBoxLayout*  m_listLayout       = nullptr;

    // Top bar within the content area
    QWidget*   m_topContainer        = nullptr;
    QCheckBox* m_hideMinimizedViewsCb = nullptr;

    // Slide animation
    QVariantAnimation* m_widthAnim = nullptr;

    bool m_expanded           = false;
    bool m_hideMinimizedViews = false;
    int  m_expandedWidth      = 175;  ///< Content area width when fully open (px)
    int  m_anchorRight        = -1;   ///< Right-edge X anchor in parent coordinates

    /** Maps each tracked subwindow to its thumbnail tile widget. */
    QMap<CustomMdiSubWindow*, QWidget*> m_items;
};

#endif // RIGHTSIDEBARWIDGET_H