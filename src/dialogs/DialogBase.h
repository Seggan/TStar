#ifndef DIALOGBASE_H
#define DIALOGBASE_H

#include <QDialog>
#include <QIcon>
#include <QString>

class MainWindowCallbacks;

/**
 * @brief Base class for all application dialogs providing common initialization.
 *
 * Centralizes the following boilerplate that would otherwise be duplicated
 * across dozens of dialog subclasses:
 *   - Window title and icon setup
 *   - Default size initialization
 *   - WA_DeleteOnClose attribute management
 *   - Geometry persistence via QSettings
 *   - Centering on parent or primary screen
 *
 * Subclasses override setupDialogUI() to build their own content after the
 * base initialization has completed.
 */
class DialogBase : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Construct a dialog with standard application defaults.
     * @param parent         Parent widget (may be nullptr).
     * @param title          Window title; left empty if not needed.
     * @param defaultWidth   Initial width in pixels (0 = keep default).
     * @param defaultHeight  Initial height in pixels (0 = keep default).
     * @param deleteOnClose  If true, set Qt::WA_DeleteOnClose for modeless use.
     * @param showIcon       If true, apply the application logo icon.
     */
    explicit DialogBase(QWidget* parent = nullptr,
                        const QString& title = QString(),
                        int defaultWidth = 0,
                        int defaultHeight = 0,
                        bool deleteOnClose = false,
                        bool showIcon = true);

    virtual ~DialogBase() = default;

    /**
     * @brief Convenience setter for title and size after construction.
     * @param title  Window title (ignored if empty).
     * @param width  Width in pixels (0 = no change).
     * @param height Height in pixels (0 = no change).
     */
    void setWindowProperties(const QString& title, int width = 0, int height = 0);

    /**
     * @brief Toggle the WA_DeleteOnClose attribute at runtime.
     *
     * Useful for tool windows that should persist across show/hide cycles.
     */
    void setDeleteOnClose(bool enabled);

    /**
     * @brief Return the shared application icon loaded from resources.
     * @return Cached QIcon instance.
     */
    static QIcon getStandardIcon();

    /**
     * @brief Walk the parent chain to find a MainWindowCallbacks interface.
     * @return Pointer to the interface, or nullptr if none is found.
     */
    MainWindowCallbacks* getCallbacks();

protected:
    /** @brief Center the dialog on its parent (or screen) when first shown. */
    void showEvent(QShowEvent* event) override;

    /**
     * @brief Hook for subclasses to build their UI.
     *
     * Called once during initialize(), after the window properties have been
     * set but before geometry restoration.  Override this instead of doing
     * manual setWindowTitle / resize in the constructor.
     */
    virtual void setupDialogUI() {}

    /**
     * @brief Restore previously saved window geometry from QSettings.
     * @param settingsKey  Lookup key (defaults to the meta-object class name).
     */
    void restoreWindowGeometry(const QString& settingsKey = QString());

    /**
     * @brief Persist the current window geometry to QSettings.
     * @param settingsKey  Storage key (defaults to the meta-object class name).
     */
    void saveWindowGeometry(const QString& settingsKey = QString());

private:
    /** @brief Shared initialization logic called from the constructor. */
    void initialize(const QString& title,
                    int width,
                    int height,
                    bool deleteOnClose,
                    bool showIcon);
};

#endif // DIALOGBASE_H