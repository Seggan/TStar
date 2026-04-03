#ifndef SPLASHSCREEN_H
#define SPLASHSCREEN_H

#include <QWidget>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QTimer>

/**
 * @brief Frameless splash screen displayed during application startup.
 *
 * Renders a logo, version string, subtitle, a smooth progress bar, and a
 * loading message using QPainter. The progress bar interpolates smoothly
 * towards its target value via a 60 FPS timer-driven animation.
 *
 * Usage:
 *   auto* splash = new SplashScreen(":/images/Logo.png");
 *   splash->show();
 *   splash->startFadeIn();
 *   splash->setProgress(30);
 *   splash->setMessage("Loading modules...");
 *   // ... startup work ...
 *   splash->startFadeOut();  // triggers finish() -> deleteLater()
 */
class SplashScreen : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the splash screen and loads the logo from @p logoPath.
     *
     * The widget automatically attempts to load a background image from the same
     * directory as the logo (replacing "Logo.png" with "background.jpg").
     */
    explicit SplashScreen(const QString& logoPath, QWidget* parent = nullptr);

    /** Updates the status message displayed below the progress bar. */
    void setMessage(const QString& message);

    /**
     * @brief Sets the target progress value.
     * @param value  Progress percentage in the range [0, 100].
     */
    void setProgress(int value);

    /** Starts a fade-in animation from transparent to fully opaque. */
    void startFadeIn();

    /**
     * @brief Starts a fade-out animation and schedules destruction on completion.
     *
     * Connects QPropertyAnimation::finished to finish(), which calls
     * hide() + close() + deleteLater().
     */
    void startFadeOut();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    /** Called when the fade-out animation completes; hides and destroys the widget. */
    void finish();

    /** Advances m_displayedProgress towards m_targetProgress each frame. */
    void updateSmoothProgress();

private:
    QPixmap m_logoPixmap;       ///< Scaled application logo
    QPixmap m_bgPixmap;         ///< Optional background image
    QString m_currentMessage;   ///< Status message shown below the progress bar

    float m_displayedProgress = 0.0f;   ///< Interpolated display value
    float m_targetProgress    = 0.0f;   ///< Target value set by setProgress()

    int m_splashWidth  = 500;
    int m_splashHeight = 320;

    QPropertyAnimation* m_anim          = nullptr;
    QTimer*             m_progressTimer = nullptr;
};

#endif // SPLASHSCREEN_H