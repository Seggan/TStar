// =============================================================================
// MainWindowCallbacks.h
//
// Abstract callback interface that decouples tool dialogs from the MainWindow
// class. Dialogs receive a pointer to this interface rather than including
// MainWindow.h, breaking circular header dependencies while still providing
// access to essential window-level operations.
// =============================================================================

#ifndef MAINWINDOW_CALLBACKS_H
#define MAINWINDOW_CALLBACKS_H

#include <QString>
#include "ImageBuffer.h"

// Forward declaration
class ImageViewer;

// =============================================================================
// MainWindowCallbacks interface
// =============================================================================

class MainWindowCallbacks {
public:
    virtual ~MainWindowCallbacks() = default;

    // ---- Image access -------------------------------------------------------

    /// Returns a pointer to the currently active ImageBuffer, or nullptr
    /// if no image is loaded.
    virtual ImageBuffer* getCurrentImageBuffer() = 0;

    /// Returns a pointer to the currently active ImageViewer, or nullptr
    /// if no viewer is active.
    virtual ImageViewer* getCurrentViewer() = 0;

    // ---- Result output ------------------------------------------------------

    /// Create a new image window populated with the given buffer and title.
    /// @param mode    Display mode override (-1 = inherit current).
    /// @param median  Target median for auto-stretch.
    /// @param linked  Whether to link channel stretch statistics.
    virtual void createResultWindow(
        const ImageBuffer& buffer, const QString& title,
        int mode = -1, float median = 0.25f, bool linked = true
    ) = 0;

    // ---- Logging ------------------------------------------------------------

    /// Log a message to the application console.
    /// @param severity  0 = info, 1 = warning, 2 = error.
    /// @param showPopup If true, also display a message box.
    virtual void logMessage(const QString& message, int severity,
                            bool showPopup = false) = 0;

    // ---- Display refresh ----------------------------------------------------

    /// Trigger a display refresh of the main window's active viewer.
    virtual void updateDisplay() = 0;

    // ---- Long operation indicators ------------------------------------------

    /// Signal the start of a long operation (show progress, disable UI).
    virtual void startLongProcess() = 0;

    /// Signal the end of a long operation (hide progress, enable UI).
    virtual void endLongProcess() = 0;

    // ---- Viewer state queries -----------------------------------------------

    /// Returns true if the given viewer is currently retained by a tool dialog.
    /// Optionally writes the tool name into @p toolName.
    virtual bool isViewerInUse(ImageViewer* vi,
                               QString* toolName = nullptr) const {
        (void)vi;
        (void)toolName;
        return false;
    }

    /// Refresh the FITS header panel to reflect the active viewer.
    /// Useful after batch operations that modify non-active windows.
    virtual void refreshHeaderPanel() {}

    // ---- Utility helpers ----------------------------------------------------

    /// Build a child window title by appending @p suffix to @p parentTitle,
    /// stripping any trailing '*' marker and known image-file extensions.
    ///
    /// Dialogs should call this BEFORE starting background work (capturing
    /// the viewer title while it is still valid), rather than reading the
    /// title at the time the result window is created.
    static QString buildChildTitle(const QString& parentTitle,
                                   const QString& suffix)
    {
        QString t = parentTitle;

        // Remove modification marker
        if (t.endsWith(QLatin1Char('*')))
            t.chop(1);

        // Strip common image-file extensions from the base name
        static const char* const kExts[] = {
            "fits", "fit", "tif", "tiff", "png", "jpg", "jpeg", "xisf", "bmp",
            nullptr
        };

        const int dot = t.lastIndexOf(QLatin1Char('.'));
        if (dot >= 0) {
            const QString ext = t.mid(dot + 1).toLower();
            for (int i = 0; kExts[i]; ++i) {
                if (ext == QLatin1String(kExts[i])) {
                    t = t.left(dot);
                    break;
                }
            }
        }

        return t.trimmed() + suffix;
    }
};

#endif // MAINWINDOW_CALLBACKS_H