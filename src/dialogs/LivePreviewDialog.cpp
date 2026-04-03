// =============================================================================
// LivePreviewDialog.cpp
// Floating mask preview window with mouse-wheel zoom, pan, and fit-to-view.
// Uses ImageBuffer's display rendering for consistent visualization with
// the main application.
// =============================================================================

#include "LivePreviewDialog.h"
#include "../ImageBuffer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QWheelEvent>
#include <QTimer>
#include <QImage>
#include <algorithm>

// =============================================================================
// Construction
// =============================================================================

LivePreviewDialog::LivePreviewDialog(int width, int height, QWidget* parent)
    : DialogBase(parent, tr("Live Mask Preview"), 0, 0)
    , m_targetWidth(width)
    , m_targetHeight(height)
{
    // Float as a tool window above the main application.
    setWindowFlags(windowFlags() | Qt::Tool);

    // Scale down to a reasonable subwindow size (max 800x600).
    const int maxW = 800;
    const int maxH = 600;
    float scaleW = (width  > maxW) ? (float)maxW / width  : 1.0f;
    float scaleH = (height > maxH) ? (float)maxH / height : 1.0f;
    float scale  = std::min(scaleW, scaleH);

    m_targetWidth  = static_cast<int>(width  * scale);
    m_targetHeight = static_cast<int>(height * scale);

    // -- Graphics scene and view ----------------------------------------------
    m_scene   = new QGraphicsScene(this);
    m_pixItem = m_scene->addPixmap(QPixmap());
    m_pixItem->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);

    m_view = new QGraphicsView(m_scene, this);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform);
    m_view->viewport()->installEventFilter(this);

    // -- Zoom control buttons -------------------------------------------------
    QPushButton* btnZoomIn  = new QPushButton("+",       this);
    QPushButton* btnZoomOut = new QPushButton("--",      this);
    QPushButton* btnFit     = new QPushButton(tr("Fit"), this);
    btnZoomIn->setFixedWidth(32);
    btnZoomOut->setFixedWidth(32);

    connect(btnZoomIn,  &QPushButton::clicked,
            this, &LivePreviewDialog::onZoomIn);
    connect(btnZoomOut, &QPushButton::clicked,
            this, &LivePreviewDialog::onZoomOut);
    connect(btnFit,     &QPushButton::clicked,
            this, &LivePreviewDialog::onFit);

    QHBoxLayout* btnRow = new QHBoxLayout;
    btnRow->addWidget(btnZoomIn);
    btnRow->addWidget(btnZoomOut);
    btnRow->addWidget(btnFit);
    btnRow->addStretch();
    btnRow->setContentsMargins(2, 2, 2, 2);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_view);
    layout->addLayout(btnRow);
    layout->setContentsMargins(0, 0, 0, 4);

    resize(m_targetWidth + 20, m_targetHeight + 36);

    // Centre over the parent window.
    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center()
             - rect().center());
    }
}

// =============================================================================
// Mouse wheel zoom (event filter on viewport)
// =============================================================================

bool LivePreviewDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_view->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent* we = static_cast<QWheelEvent*>(event);
        float factor  = (we->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
        float newZoom = std::clamp(m_zoom * factor, 0.05f, 16.0f);
        float actual  = newZoom / m_zoom;
        m_zoom = newZoom;
        m_view->scale(actual, actual);
        return true;
    }
    return DialogBase::eventFilter(obj, event);
}

// =============================================================================
// Zoom actions
// =============================================================================

void LivePreviewDialog::onZoomIn()  { setZoom(m_zoom * 1.25f); }
void LivePreviewDialog::onZoomOut() { setZoom(m_zoom / 1.25f); }

void LivePreviewDialog::setZoom(float z)
{
    m_zoom = std::clamp(z, 0.05f, 16.0f);
    m_view->resetTransform();
    m_view->scale(m_zoom, m_zoom);
}

void LivePreviewDialog::onFit()
{
    if (m_pixItem->pixmap().isNull()) return;
    m_view->fitInView(m_pixItem, Qt::KeepAspectRatio);
    m_zoom = static_cast<float>(m_view->transform().m11());
}

// =============================================================================
// Mask data update
// =============================================================================

void LivePreviewDialog::updateMask(const std::vector<float>& maskData,
                                    int width, int height,
                                    ImageBuffer::DisplayMode mode,
                                    bool inverted, bool falseColor)
{
    if (maskData.empty() || width == 0 || height == 0) {
        // Retain the last valid image to prevent black flickering.
        return;
    }

    // Render the mask data through ImageBuffer for consistent display.
    ImageBuffer buf;
    buf.setData(width, height, 1, maskData);
    QImage img = buf.getDisplayImage(mode, true, nullptr,
                                     width, height,
                                     false, inverted, falseColor);

    m_pixItem->setPixmap(QPixmap::fromImage(img));
    m_scene->setSceneRect(m_pixItem->boundingRect());

    if (m_firstUpdate) {
        // Defer fit-to-view until the dialog is visible and has valid
        // geometry. fitInView is a no-op on hidden widgets.
        QTimer::singleShot(0, this, &LivePreviewDialog::onFit);
        m_firstUpdate = false;
    }
}