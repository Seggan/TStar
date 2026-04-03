#ifndef BLINKCOMPARATORDIALOG_H
#define BLINKCOMPARATORDIALOG_H

// =============================================================================
// BlinkComparatorDialog.h
//
// Blink comparator tool for alternating between two image views at a
// configurable rate. Useful for identifying transient objects, alignment
// errors, or processing differences between frames.
// =============================================================================

#include "DialogBase.h"

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QPoint>

// Forward declarations
class QComboBox;
class QSpinBox;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class ImageViewer;
class QPaintEvent;
class QMouseEvent;
class QWheelEvent;
class QResizeEvent;
class MainWindow;

// =============================================================================
// Class: BlinkCanvas
// Pan-and-zoom image display widget for the blink comparator.
// =============================================================================
class BlinkCanvas : public QWidget {
    Q_OBJECT

public:
    explicit BlinkCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    void zoomIn();
    void zoomOut();
    void fitToView();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateBounds();

    QImage m_image;
    float  m_zoom   = 1.0f;
    float  m_panX   = 0.0f;
    float  m_panY   = 0.0f;
    QPoint m_lastMousePos;
    bool   m_dragging = false;
};

// =============================================================================
// Class: BlinkComparatorDialog
// Main dialog that manages view selection, blink timing, and display.
// =============================================================================
class BlinkComparatorDialog : public DialogBase {
    Q_OBJECT

public:
    explicit BlinkComparatorDialog(MainWindow* mainWindow,
                                   QWidget* parent = nullptr);
    ~BlinkComparatorDialog();

    /** Refresh the list of available views from the MDI workspace. */
    void updateViewLists();

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onPlayPauseClicked();
    void onBlinkTimeout();
    void onRateChanged(int rateMs);
    void onAutoStretchToggled(bool checked);
    void onViewsSelectionChanged();
    void refreshCurrentImage();
    void onSwitchClicked();

private:
    void   setupUI();
    QImage renderViewImage(ImageViewer* viewer);

    // --- References ---
    MainWindow* m_mainWindow;

    // --- UI widgets ---
    QComboBox*   m_view1Combo;
    QComboBox*   m_view2Combo;
    QSpinBox*    m_rateSpinBox;
    QPushButton* m_playPauseBtn;
    QPushButton* m_switchBtn;
    QPushButton* m_refreshBtn;
    QToolButton* m_autoStretchBtn;
    QToolButton* m_btnZoomIn;
    QToolButton* m_btnZoomOut;
    QToolButton* m_btnFit;
    BlinkCanvas* m_canvas;
    QLabel*      m_filenameLabel;

    // --- Blink state ---
    QTimer m_blinkTimer;
    bool   m_showingView1    = true;
    bool   m_isPlaying       = false;
    bool   m_useAutoStretch  = false;
    bool   m_needsInitialFit = true;

    // --- Cached rendered images (avoid re-rendering every blink cycle) ---
    QImage       m_img1;
    QImage       m_img2;
    ImageViewer* m_lastView1 = nullptr;
    ImageViewer* m_lastView2 = nullptr;
};

#endif // BLINKCOMPARATORDIALOG_H