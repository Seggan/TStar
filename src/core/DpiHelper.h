#ifndef DPIHELPER_H
#define DPIHELPER_H

// ============================================================================
// DpiHelper.h
// DPI-aware scaling utilities for consistent UI element sizing across
// displays with varying pixel densities (HiDPI / Retina support).
// ============================================================================

#include <QWidget>
#include <QScreen>
#include <QApplication>
#include <QGuiApplication>

/**
 * @brief Static utility class providing DPI-aware pixel scaling.
 *
 * All methods accept an optional QWidget pointer to query the correct
 * screen. When nullptr, the primary screen is used as fallback.
 */
class DpiHelper {
public:

    // -- Core scaling ---------------------------------------------------------

    /** Get the device pixel ratio for the screen hosting the given widget. */
    static qreal dpr(QWidget* widget = nullptr)
    {
        if (widget && widget->screen())
            return widget->screen()->devicePixelRatio();
        if (QGuiApplication::primaryScreen())
            return QGuiApplication::primaryScreen()->devicePixelRatio();
        return 1.0;
    }

    /** Scale an integer pixel value by the device pixel ratio. */
    static int scale(int px, QWidget* widget = nullptr)
    {
        return qRound(px * dpr(widget));
    }

    /** Scale a floating-point pixel value by the device pixel ratio. */
    static qreal scaleF(qreal px, QWidget* widget = nullptr)
    {
        return px * dpr(widget);
    }

    // -- Standard UI element sizes (base values at 100% scaling) --------------

    static int sidebarWidth(QWidget* w = nullptr)    { return scale(24, w); }  ///< 24px
    static int titleBarHeight(QWidget* w = nullptr)  { return scale(28, w); }  ///< 28px
    static int buttonSize(QWidget* w = nullptr)      { return scale(22, w); }  ///< 22px
    static int iconSize(QWidget* w = nullptr)        { return scale(14, w); }  ///< 14px
    static int resizeMargin(QWidget* w = nullptr)    { return scale(8, w);  }  ///< 8px
    static int borderWidth(QWidget* w = nullptr)     { return scale(2, w);  }  ///< 2px

    // -- Minimum window dimensions --------------------------------------------

    static int minWindowWidth(QWidget* w = nullptr)  { return scale(150, w); } ///< 150px
    static int minWindowHeight(QWidget* w = nullptr) { return scale(80, w);  } ///< 80px
    static int minShadedWidth(QWidget* w = nullptr)  { return scale(200, w); } ///< 200px

    // -- Icon rendering -------------------------------------------------------

    static int iconPixmapSize(QWidget* w = nullptr)  { return scale(64, w); }  ///< 64px
    static int dragPixmapSize(QWidget* w = nullptr)  { return scale(20, w); }  ///< 20px
};

#endif // DPIHELPER_H