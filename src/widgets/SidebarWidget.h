#ifndef SIDEBARWIDGET_H
#define SIDEBARWIDGET_H

#include <QWidget>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QVariantAnimation>
#include <QButtonGroup>
#include <QMap>
#include <QMouseEvent>
#include <functional>

class QEnterEvent;
class QEvent;

/**
 * @brief Collapsible left-side panel widget hosting multiple named content panels.
 *
 * A narrow tab strip on the left edge contains one VerticalButton per registered
 * panel. Clicking a button expands the content area via a smooth width animation
 * and brings the corresponding panel to the front of the internal QStackedWidget.
 * Clicking the active button again collapses the panel.
 *
 * The widget also wraps a QTextEdit console panel with an "Auto-open" preference
 * checkbox and exposes logToConsole() / updateLastLogLine() helpers.
 *
 * The content area width is user-adjustable via the ResizeHandle on the right edge.
 */
class SidebarWidget : public QWidget
{
    Q_OBJECT
    friend class ResizeHandle;

public:
    // -------------------------------------------------------------------------
    // VerticalButton - tab button with -90 degree rotated text label
    // -------------------------------------------------------------------------
    class VerticalButton : public QPushButton
    {
    public:
        explicit VerticalButton(const QString& text, QWidget* parent = nullptr);
        QSize sizeHint() const override;

    protected:
        void paintEvent(QPaintEvent* event) override;
    };

    explicit SidebarWidget(QWidget* parent = nullptr);

    /**
     * @brief Registers a panel and creates the corresponding tab button.
     * @param name      Display name used both as the tab label and lookup key.
     * @param iconPath  Path to an icon resource (currently unused; reserved).
     * @param panel     Widget to display in the content area for this tab.
     */
    void addPanel(const QString& name, const QString& iconPath, QWidget* panel);

    /** Returns the panel widget registered under @p name, or nullptr. */
    QWidget* getPanel(const QString& name);

    /** Programmatically opens the named panel, expanding the sidebar if needed. */
    void openPanel(const QString& name);

    /** Appends an HTML-formatted message to the console panel. */
    void logToConsole(const QString& htmlMsg);

    /** Replaces the last line of the console panel with an HTML-formatted message. */
    void updateLastLogLine(const QString& htmlMsg);

    /**
     * @brief Adds an icon button below the tab strip.
     * @param icon      Button icon.
     * @param tooltip   Tooltip text.
     * @param callback  Invoked when the button is clicked.
     */
    void addBottomAction(const QIcon& icon,
                         const QString& tooltip,
                         std::function<void()> callback);

    /** Sets the content area width used when the panel is fully expanded. */
    void setExpandedWidth(int width);

    /** Returns the combined width of the tab strip, content area, and resize handle. */
    int totalVisibleWidth() const;

    /** Collapses the content area if currently expanded. */
    void collapse() { setExpanded(false); }

    bool isExpanded()        const { return m_expanded; }
    bool isAutoOpenEnabled() const { return m_autoOpenConsole; }

    /** Returns true if the cursor is within the sidebar or a child widget has focus. */
    bool isInteracting() const;

    /** Returns the name of the currently visible panel, or an empty string. */
    QString currentPanel() const;

signals:
    void interactionStarted();
    void interactionEnded();

    /** Emitted whenever the expanded/collapsed state changes. */
    void expandedToggled(bool expanded);

    /** Emitted when the "Auto-open console on log" checkbox is toggled. */
    void autoOpenToggled(bool enabled);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent*      event) override;

private slots:
    void onTabClicked(int id);
    void onAnimationFinished();

private:
    void createTab(const QString& name, const QString& iconPath, int id);
    void setExpanded(bool expanded);

    // Tab strip (left column)
    QWidget*     m_tabContainer = nullptr;
    QVBoxLayout* m_tabLayout    = nullptr;

    // Content area
    QWidget*        m_contentContainer = nullptr;
    QStackedWidget* m_stack            = nullptr;

    // Width animation
    QVariantAnimation* m_widthAnim = nullptr;

    bool m_expanded      = false;
    int  m_expandedWidth = 350;   ///< Default content area width (px)

    // Tab management
    QButtonGroup*      m_tabGroup  = nullptr;
    QMap<int, QString> m_idToName;
    QMap<QString, int> m_nameToId;

    int  m_currentId       = -1;
    bool m_autoOpenConsole = true;

    // Optional references to child widgets
    class QTextEdit*    m_console      = nullptr;
    class ResizeHandle* m_resizeHandle = nullptr;
};


// ============================================================================
// ResizeHandle - drag handle that adjusts the sidebar's expanded width
// ============================================================================

/**
 * @brief Thin draggable strip placed at the right edge of SidebarWidget.
 *
 * Dragging the handle calls SidebarWidget::setExpandedWidth() in real time,
 * allowing the user to resize the content area interactively.
 */
class ResizeHandle : public QWidget
{
public:
    explicit ResizeHandle(SidebarWidget* parent)
        : QWidget(parent)
        , m_sidebar(parent)
    {
        setFixedWidth(5);
        setCursor(Qt::SizeHorCursor);
        setStyleSheet("background: transparent;");
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_dragging   = true;
            m_startX     = static_cast<int>(e->globalPosition().x());
            m_startWidth = m_sidebar->m_expandedWidth;
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_dragging) {
            int delta    = static_cast<int>(e->globalPosition().x()) - m_startX;
            int newWidth = m_startWidth + delta;
            m_sidebar->setExpandedWidth(newWidth);
        }
    }

    void mouseReleaseEvent(QMouseEvent*) override
    {
        m_dragging = false;
    }

    void enterEvent(QEnterEvent*) override
    {
        setStyleSheet("background: #444;");
    }

    void leaveEvent(QEvent*) override
    {
        setStyleSheet("background: transparent;");
    }

private:
    SidebarWidget* m_sidebar    = nullptr;
    bool           m_dragging   = false;
    int            m_startX     = 0;
    int            m_startWidth = 0;
};

#endif // SIDEBARWIDGET_H