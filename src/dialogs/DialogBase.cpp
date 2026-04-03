#include "DialogBase.h"
#include "MainWindowCallbacks.h"

#include <QGuiApplication>
#include <QIcon>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DialogBase::DialogBase(QWidget* parent,
                       const QString& title,
                       int defaultWidth,
                       int defaultHeight,
                       bool deleteOnClose,
                       bool showIcon)
    : QDialog(parent)
{
    initialize(title, defaultWidth, defaultHeight, deleteOnClose, showIcon);
}

// ---------------------------------------------------------------------------
// Private initialization
// ---------------------------------------------------------------------------

void DialogBase::initialize(const QString& title,
                            int width,
                            int height,
                            bool deleteOnClose,
                            bool showIcon)
{
    // Apply the application icon if requested.
    if (showIcon) {
        setWindowIcon(getStandardIcon());
    }

    // Mark dialog for automatic deletion when closed (useful for modeless dialogs).
    if (deleteOnClose) {
        setAttribute(Qt::WA_DeleteOnClose);
    }

    // Apply title and initial dimensions.
    setWindowProperties(title, width, height);

    // Allow the concrete subclass to populate its layout.
    setupDialogUI();

    // Attempt to restore geometry from a previous session.
    QString settingsKey = parent() ? parent()->objectName()
                                   : metaObject()->className();
    if (!settingsKey.isEmpty()) {
        restoreWindowGeometry(settingsKey);
    }
}

// ---------------------------------------------------------------------------
// Event overrides
// ---------------------------------------------------------------------------

void DialogBase::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // Center on parent window if available, otherwise on the primary screen.
    QWidget* p = parentWidget();
    if (p) {
        p = p->window();
        const QRect parentRect = p->frameGeometry();
        const QRect dlgRect    = frameGeometry();

        const int x = parentRect.x() + (parentRect.width()  - dlgRect.width())  / 2;
        const int y = parentRect.y() + (parentRect.height() - dlgRect.height()) / 2;
        move(x, y);
    } else {
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            const QRect screenRect = screen->availableGeometry();
            const QRect dlgRect    = frameGeometry();

            const int x = screenRect.x() + (screenRect.width()  - dlgRect.width())  / 2;
            const int y = screenRect.y() + (screenRect.height() - dlgRect.height()) / 2;
            move(x, y);
        }
    }
}

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------

void DialogBase::setWindowProperties(const QString& title, int width, int height)
{
    if (!title.isEmpty()) {
        setWindowTitle(title);
    }
    if (width > 0 || height > 0) {
        resize(width  > 0 ? width  : size().width(),
               height > 0 ? height : size().height());
    }
}

void DialogBase::setDeleteOnClose(bool enabled)
{
    setAttribute(Qt::WA_DeleteOnClose, enabled);
}

QIcon DialogBase::getStandardIcon()
{
    static QIcon icon;
    if (icon.isNull()) {
        icon = QIcon(":/images/Logo.png");
    }
    return icon;
}

MainWindowCallbacks* DialogBase::getCallbacks()
{
    QWidget* w = this;
    while (w) {
        if (auto* mw = dynamic_cast<MainWindowCallbacks*>(w)) {
            return mw;
        }
        w = w->parentWidget();
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Geometry persistence
// ---------------------------------------------------------------------------

void DialogBase::restoreWindowGeometry(const QString& settingsKey)
{
    if (settingsKey.isEmpty()) {
        return;
    }

    QSettings settings("TStar", "TStar");
    const QString geometryKey = settingsKey + "/geometry";

    if (settings.contains(geometryKey)) {
        restoreGeometry(settings.value(geometryKey).toByteArray());
    }
}

void DialogBase::saveWindowGeometry(const QString& settingsKey)
{
    if (settingsKey.isEmpty()) {
        return;
    }

    QSettings settings("TStar", "TStar");
    const QString geometryKey = settingsKey + "/geometry";
    settings.setValue(geometryKey, saveGeometry());
}