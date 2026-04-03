#include "SidebarWidget.h"

#include <QIcon>
#include <QLabel>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QDateTime>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QScrollArea>
#include <QPainter>
#include <QStyleOptionButton>
#include <QMouseEvent>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QSettings>
#include <QTextFrame>


// ============================================================================
// SidebarWidget - constructor
// ============================================================================

SidebarWidget::SidebarWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Transparent background prevents a grey box from appearing behind the overlay
    setStyleSheet("background-color: transparent;");
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // ------------------------------------------------------------------
    // 1. Tab strip (narrow left column with vertical tab buttons)
    // ------------------------------------------------------------------
    m_tabContainer = new QWidget(this);
    m_tabContainer->setFixedWidth(36);
    m_tabContainer->setStyleSheet(
        "background-color: #252525; border-right: 1px solid #1a1a1a;");

    m_tabLayout = new QVBoxLayout(m_tabContainer);
    m_tabLayout->setContentsMargins(2, 5, 2, 5);
    m_tabLayout->setSpacing(5);
    m_tabLayout->setAlignment(Qt::AlignHCenter);
    m_tabLayout->addStretch();

    // ------------------------------------------------------------------
    // 2. Content area (QScrollArea used for its horizontal clipping, which
    //    enables the slide-in animation without content overflowing)
    // ------------------------------------------------------------------
    auto* scrollArea = new QScrollArea(this);
    m_contentContainer = scrollArea;

    scrollArea->setFixedWidth(0); // Start in collapsed state
    scrollArea->setStyleSheet("background-color: rgba(0, 0, 0, 128); border: none;");
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_stack = new QStackedWidget();
    // The stack must maintain the full expanded width to prevent content
    // compression as the container animates from 0 to m_expandedWidth
    m_stack->setMinimumWidth(m_expandedWidth);

    scrollArea->setWidget(m_stack);
    scrollArea->setWidgetResizable(true);

    mainLayout->addWidget(m_tabContainer);
    mainLayout->addWidget(m_contentContainer);

    // Drag handle placed to the right of the content area
    m_resizeHandle = new ResizeHandle(this);
    mainLayout->addWidget(m_resizeHandle);

    // ------------------------------------------------------------------
    // Slide animation for the content container width
    // ------------------------------------------------------------------
    m_widthAnim = new QVariantAnimation(this);
    m_widthAnim->setDuration(250);
    m_widthAnim->setEasingCurve(QEasingCurve::OutQuad);

    connect(m_widthAnim, &QVariantAnimation::valueChanged,
            [this](const QVariant& val) {
        m_contentContainer->setFixedWidth(val.toInt());
        resize(totalVisibleWidth(), height());

        // Force repaint on all child widgets to prevent blank rectangles
        // during animation (particularly noticeable on macOS)
        if (m_stack)   m_stack->update();
        if (m_console) m_console->update();
        m_contentContainer->update();
        update();
    });

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);
    connect(m_tabGroup, &QButtonGroup::idClicked,
            this, &SidebarWidget::onTabClicked);
}


// ============================================================================
// Layout helpers
// ============================================================================

int SidebarWidget::totalVisibleWidth() const
{
    int w = m_tabContainer->width() + m_contentContainer->width();
    if (m_resizeHandle) w += m_resizeHandle->width();
    return w;
}


// ============================================================================
// Panel management
// ============================================================================

void SidebarWidget::addPanel(const QString& name,
                             const QString& iconPath,
                             QWidget*       panel)
{
    if (!panel) return;

    QWidget* widgetToAdd = panel;

    // Special handling for the Console panel: wrap it with a top bar
    // containing the "Auto-open console on log" preference checkbox.
    if (name == "Console" || name == tr("Console")) {
        m_console = panel->findChild<QTextEdit*>();
        if (!m_console) m_console = qobject_cast<QTextEdit*>(panel);

        if (m_console) {
            auto* container = new QWidget();
            auto* layout    = new QVBoxLayout(container);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);

            // Top bar with the auto-open preference checkbox
            auto* topContainer = new QWidget(container);
            topContainer->setFixedHeight(26);
            topContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            topContainer->setStyleSheet(
                "background-color: #202020; border-bottom: 1px solid #1a1a1a;");

            auto* topLayout = new QHBoxLayout(topContainer);
            topLayout->setContentsMargins(8, 0, 8, 0);
            topLayout->setSpacing(0);

            auto* chk = new QCheckBox(tr("Auto-open console on log"), topContainer);
            chk->setStyleSheet(
                "QCheckBox { color: #aaa; font-size: 11px; }"
                "QCheckBox::indicator { width: 12px; height: 12px; }");

            QSettings settings;
            m_autoOpenConsole = settings.value("Console/autoOpen", true).toBool();
            chk->setChecked(m_autoOpenConsole);

            connect(chk, &QCheckBox::toggled, [this](bool checked) {
                m_autoOpenConsole = checked;
                QSettings s;
                s.setValue("Console/autoOpen", checked);
                emit autoOpenToggled(checked);
            });

            topLayout->addWidget(chk);
            topLayout->addStretch();

            layout->addWidget(topContainer, 0, Qt::AlignTop);

            // Ensure the console panel fills the remaining space
            panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            if (panel->layout())
                panel->layout()->setContentsMargins(0, 0, 0, 0);

            layout->addWidget(panel, 1);
            widgetToAdd = container;
        }
    }

    int id = m_stack->count();
    m_stack->addWidget(widgetToAdd);

    createTab(name, iconPath, id);
    m_idToName[id]   = name;
    m_nameToId[name] = id;

    // Register additional lookup keys for call sites that use hard-coded English names,
    // ensuring lookups are stable regardless of the application locale.
    if (name == "Console" || name == tr("Console")) {
        m_nameToId["Console"]       = id;
        m_nameToId[tr("Console")]   = id;
    } else if (name == "Header" || name == tr("Header")) {
        m_nameToId["Header"]        = id;
        m_nameToId[tr("Header")]    = id;
    }
}

void SidebarWidget::createTab(const QString& name,
                              [[maybe_unused]] const QString& iconPath,
                              int id)
{
    auto* btn = new VerticalButton(name, this);
    btn->setCheckable(true);
    btn->setToolTip(name);
    btn->setFixedSize(32, 120);

    // Disable styled background so the custom paintEvent has full rendering control
    btn->setAttribute(Qt::WA_StyledBackground, false);

    m_tabGroup->addButton(btn, id);
    // Insert before the trailing stretch, centred horizontally
    m_tabLayout->insertWidget(m_tabLayout->count() - 1, btn, 0, Qt::AlignHCenter);
}


// ============================================================================
// Tab click handling
// ============================================================================

void SidebarWidget::onTabClicked(int id)
{
    bool sameTab = (id == m_currentId) && m_expanded;

    if (sameTab) {
        // Clicking the active tab again collapses the panel
        setExpanded(false);
        m_tabGroup->button(id)->setChecked(false);
        m_currentId = -1;
    } else {
        // Switch to the new panel (expand if currently collapsed)
        m_stack->setCurrentIndex(id);
        m_currentId = id;
        if (!m_expanded) setExpanded(true);
    }
}


// ============================================================================
// Bottom action buttons
// ============================================================================

void SidebarWidget::addBottomAction(const QIcon& icon,
                                    const QString& tooltip,
                                    std::function<void()> callback)
{
    auto* btn = new QPushButton(this);
    btn->setIcon(icon);
    btn->setIconSize(QSize(24, 24));
    btn->setFixedSize(32, 32);
    btn->setToolTip(tooltip);
    btn->setFlat(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        "QPushButton { background-color: transparent; border: none; }"
        "QPushButton:hover { background-color: #3d3d3d; border-radius: 4px; }");

    m_tabLayout->addWidget(btn, 0, Qt::AlignHCenter);
    connect(btn, &QPushButton::clicked, callback);
}


// ============================================================================
// Expand / collapse
// ============================================================================

void SidebarWidget::setExpanded(bool expanded)
{
    if (m_expanded == expanded) return;
    m_expanded = expanded;
    emit expandedToggled(expanded);

    if (!expanded) {
        // Uncheck the active tab button when collapsing programmatically
        if (m_tabGroup->checkedButton()) {
            m_tabGroup->setExclusive(false);
            m_tabGroup->checkedButton()->setChecked(false);
            m_tabGroup->setExclusive(true);
        }
        m_currentId = -1;
    }

    m_widthAnim->stop();
    m_widthAnim->setStartValue(m_contentContainer->width());
    m_widthAnim->setEndValue(expanded ? m_expandedWidth : 0);
    m_widthAnim->start();
}

void SidebarWidget::onAnimationFinished()
{
    // Reserved for any post-animation cleanup if needed in future
}


// ============================================================================
// Programmatic panel access
// ============================================================================

void SidebarWidget::openPanel(const QString& name)
{
    if (!m_nameToId.contains(name)) return;
    int id = m_nameToId[name];

    m_stack->setCurrentIndex(id);
    m_currentId = id;

    if (m_tabGroup->button(id))
        m_tabGroup->button(id)->setChecked(true);

    if (!m_expanded)
        setExpanded(true);
}

QWidget* SidebarWidget::getPanel(const QString& name)
{
    if (!m_nameToId.contains(name)) return nullptr;
    return m_stack->widget(m_nameToId[name]);
}


// ============================================================================
// Resize handle integration
// ============================================================================

void SidebarWidget::setExpandedWidth(int width)
{
    width = std::clamp(width, 100, 800);
    m_expandedWidth = width;

    if (m_expanded) {
        m_contentContainer->setFixedWidth(m_expandedWidth);
        resize(totalVisibleWidth(), height());

        if (m_stack)   m_stack->update();
        if (m_console) m_console->update();
        m_contentContainer->update();
        update();
    }

    // Synchronise the stack minimum width so the slide effect remains correct
    if (m_stack) {
        m_stack->setMinimumWidth(m_expandedWidth);
        m_stack->update();
    }
}


// ============================================================================
// Interaction state detection
// ============================================================================

bool SidebarWidget::isInteracting() const
{
    // Use global cursor position for cross-platform reliability
    QPoint globalCursor = QCursor::pos();
    QRect  globalRect   = QRect(mapToGlobal(QPoint(0, 0)), size());

    if (globalRect.contains(globalCursor))
        return true;

    if (hasFocus() || (m_console && m_console->hasFocus()))
        return true;

    return false;
}

void SidebarWidget::enterEvent(QEnterEvent* event)
{
    emit interactionStarted();
    QWidget::enterEvent(event);
}

void SidebarWidget::leaveEvent(QEvent* event)
{
    emit interactionEnded();
    QWidget::leaveEvent(event);
}


// ============================================================================
// Accessors
// ============================================================================

QString SidebarWidget::currentPanel() const
{
    if (m_currentId != -1 && m_idToName.contains(m_currentId))
        return m_idToName[m_currentId];
    return QString();
}


// ============================================================================
// Console output helpers
// ============================================================================

void SidebarWidget::logToConsole(const QString& htmlMsg)
{
    if (!m_console) return;

    // Ensure a bottom margin so the user can scroll past the last visible line
    QTextFrameFormat fmt = m_console->document()->rootFrame()->frameFormat();
    if (fmt.bottomMargin() != 150) {
        fmt.setBottomMargin(150);
        m_console->document()->rootFrame()->setFrameFormat(fmt);
    }

    m_console->append(htmlMsg);
    m_console->verticalScrollBar()->setValue(
        m_console->verticalScrollBar()->maximum());
}

void SidebarWidget::updateLastLogLine(const QString& htmlMsg)
{
    if (!m_console) return;

    QTextFrameFormat fmt = m_console->document()->rootFrame()->frameFormat();
    if (fmt.bottomMargin() != 150) {
        fmt.setBottomMargin(150);
        m_console->document()->rootFrame()->setFrameFormat(fmt);
    }

    QTextCursor cursor = m_console->textCursor();
    cursor.movePosition(QTextCursor::End);

    if (m_console->document()->isEmpty()) {
        m_console->append(htmlMsg);
    } else {
        // Replace the content of the current (last) block in-place
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.insertHtml(htmlMsg);
    }

    m_console->verticalScrollBar()->setValue(
        m_console->verticalScrollBar()->maximum());
}


// ============================================================================
// VerticalButton implementation
// ============================================================================

SidebarWidget::VerticalButton::VerticalButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize SidebarWidget::VerticalButton::sizeHint() const
{
    return QSize(32, 120);
}

void SidebarWidget::VerticalButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Walk up the parent hierarchy to find the owning SidebarWidget
    SidebarWidget* sb      = nullptr;
    QWidget*       current = parentWidget();
    while (current) {
        sb = qobject_cast<SidebarWidget*>(current);
        if (sb) break;
        current = current->parentWidget();
    }

    bool isSidebarExpanded = sb ? sb->m_expanded : false;

    // Draw background based on checked / hover state
    if (isChecked() && isSidebarExpanded)
        p.fillRect(rect(), QColor("#0055aa"));
    else if (underMouse())
        p.fillRect(rect(), QColor("#444"));

    // Rotate the painter -90 degrees to draw vertical text.
    // After rotation, width and height are swapped in the text rectangle.
    p.save();
    p.translate(0, height());
    p.rotate(-90);

    p.setPen((isChecked() && isSidebarExpanded) ? Qt::white : QColor("#ccc"));
    p.drawText(QRect(0, 0, height(), width()), Qt::AlignCenter, text());
    p.restore();
}