#include "DialogBase.h"
#include "MainWindowCallbacks.h"
#include <QIcon>
#include <QSettings>
#include <QShowEvent>

DialogBase::DialogBase(QWidget* parent,
                       const QString& title,
                       int defaultWidth,
                       int defaultHeight,
                       bool deleteOnClose,
                       bool showIcon)
    : QDialog(parent) {
    initialize(title, defaultWidth, defaultHeight, deleteOnClose, showIcon);
}

void DialogBase::initialize(const QString& title,
                           int width,
                           int height,
                           bool deleteOnClose,
                           bool showIcon) {
    // Set up standard properties
    if (showIcon) {
        setWindowIcon(getStandardIcon());
    }
    
    
    // Set delete on close for transient dialogs
    if (deleteOnClose) {
        setAttribute(Qt::WA_DeleteOnClose);
    }
    
    // Set initial properties
    setWindowProperties(title, width, height);
    
    // Allow subclass to configure UI
    setupDialogUI();
    
    // Try to restore saved geometry
    QString settingsKey = parent() ? parent()->objectName() : metaObject()->className();
    if (!settingsKey.isEmpty()) {
        restoreWindowGeometry(settingsKey);
    }
}

void DialogBase::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // adjustPosition centers this dialog on the top-level parent window,
    // correctly handling MDI-embedded widgets and screen boundaries.
    adjustPosition(parentWidget() ? parentWidget()->window() : nullptr);
}

void DialogBase::setWindowProperties(const QString& title, int width, int height) {
    if (!title.isEmpty()) {
        setWindowTitle(title);
    }
    
    if (width > 0) setMinimumWidth(width);
    if (height > 0) setMinimumHeight(height);
    
    if (width > 0 || height > 0) {
        resize(width > 0 ? width : size().width(), 
               height > 0 ? height : size().height());
    }
}

void DialogBase::setDeleteOnClose(bool enabled) {
    setAttribute(Qt::WA_DeleteOnClose, enabled);
}

QIcon DialogBase::getStandardIcon() {
    static QIcon icon;
    if (icon.isNull()) {
        icon = QIcon(":/images/Logo.png");
    }
    return icon;
}

void DialogBase::restoreWindowGeometry(const QString& settingsKey) {
    if (settingsKey.isEmpty()) {
        return;
    }
    
    QSettings settings("TStar", "TStar");
    QString geometryKey = settingsKey + "/geometry";
    
    if (settings.contains(geometryKey)) {
        restoreGeometry(settings.value(geometryKey).toByteArray());
    }
}

void DialogBase::saveWindowGeometry(const QString& settingsKey) {
    if (settingsKey.isEmpty()) {
        return;
    }
    
    QSettings settings("TStar", "TStar");
    QString geometryKey = settingsKey + "/geometry";
    settings.setValue(geometryKey, saveGeometry());
}

MainWindowCallbacks* DialogBase::getCallbacks() {
    QWidget* w = this;
    while (w) {
        if (MainWindowCallbacks* mw = dynamic_cast<MainWindowCallbacks*>(w)) return mw;
        w = w->parentWidget();
    }
    return nullptr;
}
