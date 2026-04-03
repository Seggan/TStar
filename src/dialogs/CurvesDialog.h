#ifndef CURVESDIALOG_H
#define CURVESDIALOG_H

#include "DialogBase.h"
#include "algos/CubicSpline.h"

#include <QCheckBox>
#include <QLabel>
#include <QPointer>
#include <QToolButton>
#include <vector>

#include "../ImageViewer.h"

class ImageBuffer;
class QComboBox;

// ============================================================================
// CurvesGraph
//
// Interactive spline curve editor widget.  Control points are added with a
// left-click, removed with a right-click, and dragged to reshape the curve.
// The background renders a per-channel histogram that updates in real time as
// the curve is modified.
// ============================================================================
class CurvesGraph : public QWidget
{
    Q_OBJECT

public:
    explicit CurvesGraph(QWidget* parent = nullptr);

    /**
     * @brief Replaces the displayed histogram data and resamples it for the
     *        current widget width.
     */
    void setHistogram(const std::vector<std::vector<int>>& hist);

    /**
     * @brief Sets the active channel display mode.
     *        0 = RGB/K composite, 1 = Red, 2 = Green, 3 = Blue.
     */
    void setChannelMode(int mode);

    std::vector<SplinePoint> getPoints() const { return m_points; }

    /**
     * @brief Replaces all control points and re-emits curvesChanged().
     */
    void setPoints(const std::vector<SplinePoint>& pts);

    /**
     * @brief Resets to the identity curve (two endpoint control points).
     */
    void reset();

    /**
     * @brief Fits and returns the cubic spline through the current control points.
     */
    SplineData getSpline() const;

    void setLogScale(bool enabled);
    void setGridVisible(bool visible);

signals:
    void curvesChanged();
    void mouseHover(double x, double y);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void sortPoints();
    void updatePaths();

    std::vector<std::vector<int>> m_hist;
    std::vector<SplinePoint>      m_points;

    // Cached resampled histogram bins for the current widget width
    std::vector<std::vector<float>> m_resampledBins;
    double m_maxVal  = 0;
    int    m_lastW   = 0;

    int  m_channelMode = 0;
    bool m_logScale    = false;
    bool m_showGrid    = true;

    int m_dragIdx  = -1;
    int m_hoverIdx = -1;
};

// ============================================================================
// CurvesDialog
//
// Full tone-curve adjustment dialog.  Provides per-channel control toggles,
// a real-time histogram preview that reflects the curve transformation,
// and a live LUT-based image preview on the associated ImageViewer.
// ============================================================================
class CurvesDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit CurvesDialog(ImageViewer* viewer, QWidget* parent = nullptr);
    ~CurvesDialog();

    /**
     * @brief Returns true if the user confirmed the Apply action.
     */
    bool applied() const { return m_applied; }

    /**
     * @brief Overridden to clear the viewer's preview LUT on cancel.
     */
    void reject() override;

    /**
     * @brief Serializable dialog state for preset save/restore.
     */
    struct State
    {
        std::vector<SplinePoint> points;
        bool logScale;
        bool showGrid;
        bool ch[3];   ///< Per-channel enable flags (R, G, B)
        bool preview;
    };

    State getState() const;
    void  setState(const State& s);
    void  resetState();

    /**
     * @brief Provides a pre-computed histogram, bypassing the internal computation.
     *        Useful when the caller already holds the histogram data.
     */
    void setInputHistogram(const std::vector<std::vector<int>>& hist);

    /**
     * @brief Switches the dialog to operate on a different ImageViewer.
     *        Clears the preview LUT on the previous viewer before switching.
     */
    void setViewer(ImageViewer* viewer);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

signals:
    void previewRequested(const std::vector<std::vector<float>>& lut);
    void applyRequested(const SplineData& spline, const bool channels[3]);

private slots:
    void onChannelToggled();
    void onReset();
    void onApply();
    void onPreviewToggled(bool checked);
    void onLogToggled(bool checked);
    void onGridToggled(bool checked);
    void onCurvesChanged(bool isFinal);

private:
    QPointer<CurvesGraph>   m_graph;
    QCheckBox*              m_previewCheck;
    QToolButton*            m_logBtn;
    QToolButton*            m_gridBtn;
    QLabel*                 m_statsLabel;

    class QToolButton*      m_redBtn;
    class QToolButton*      m_greenBtn;
    class QToolButton*      m_blueBtn;

    QPointer<ImageViewer>   m_viewer;
    bool                    m_applied = false;

    std::vector<std::vector<int>> m_origHist; ///< Full-resolution original histogram
    std::vector<std::vector<int>> m_uiHist;   ///< Downsampled histogram for real-time UI
};

#endif // CURVESDIALOG_H