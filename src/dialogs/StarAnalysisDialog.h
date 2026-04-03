#ifndef STARANALYSISDIALOG_H
#define STARANALYSISDIALOG_H

/**
 * @file StarAnalysisDialog.h
 * @brief Star analysis dialog providing HFR/Flux histograms and statistics.
 *
 * Contains three components:
 *   - StarHistogramWidget: Custom widget for rendering histogram distributions
 *   - StarAnalysisWorker: Background thread for star detection
 *   - StarAnalysisDialog: Main dialog integrating detection, statistics, and display
 */

#include <QDialog>
#include <QThread>
#include <vector>
#include "../ImageBuffer.h"

class QTableWidget;
class QLabel;
class QSlider;
class QPushButton;
class QCheckBox;
class QTimer;

// =============================================================================
// StarHistogramWidget
// =============================================================================

/**
 * @class StarHistogramWidget
 * @brief Custom widget that renders a histogram from floating-point data.
 *
 * Supports both linear and logarithmic bin spacing, with robust range
 * determination using 1st-99th percentile clipping.
 */
class StarHistogramWidget : public QWidget {
    Q_OBJECT

public:
    explicit StarHistogramWidget(QWidget* parent = nullptr);

    /**
     * @brief Set the data to display and trigger a repaint.
     * @param data     Vector of sample values.
     * @param label    Title string displayed above the histogram.
     * @param logScale Whether to use logarithmic bin spacing.
     */
    void setData(const std::vector<float>& data,
                 const QString& label, bool logScale);

    /**
     * @brief Toggle logarithmic bin spacing.
     */
    void setLogScale(bool log);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<float> m_data;
    QString m_label;
    bool m_logScale = false;
};

// =============================================================================
// StarAnalysisWorker
// =============================================================================

/**
 * @class StarAnalysisWorker
 * @brief Background thread for performing star detection on an image buffer.
 */
class StarAnalysisWorker : public QThread {
    Q_OBJECT

public:
    explicit StarAnalysisWorker(QObject* parent = nullptr);

    /**
     * @brief Configure the worker with a source image and detection threshold.
     * @param src       Image buffer to analyse (copied for thread safety).
     * @param threshold Detection threshold in units of background sigma.
     */
    void setup(const ImageBuffer& src, float threshold);

    void run() override;

signals:
    void finished(std::vector<ImageBuffer::DetectedStar> stars);
    void failed(QString msg);

private:
    ImageBuffer m_src;
    float m_threshold;
};

// =============================================================================
// StarAnalysisDialog
// =============================================================================

#include <QPointer>
#include "../ImageViewer.h"
#include "DialogBase.h"

/**
 * @class StarAnalysisDialog
 * @brief Dialog for interactive star analysis with HFR/Flux histograms and statistics.
 *
 * Performs background star detection, displays statistical summaries
 * (min, max, median, stddev) for multiple metrics, and renders
 * histograms for HFR or Flux distributions.
 */
class StarAnalysisDialog : public DialogBase {
    Q_OBJECT

public:
    explicit StarAnalysisDialog(QWidget* parent = nullptr,
                                ImageViewer* viewer = nullptr);
    ~StarAnalysisDialog();

    /**
     * @brief Update the target viewer and re-run analysis.
     * @param v Pointer to the new ImageViewer.
     */
    void setViewer(ImageViewer* v);

signals:
    /**
     * @brief Emitted with a summary status message after analysis completes.
     */
    void statusMsg(const QString& msg);

private slots:
    void onRunClicked();
    void onThresholdChanged();
    void onWorkerFinished(std::vector<ImageBuffer::DetectedStar> stars);
    void toggleLog(bool checked);
    void toggleMode();

private:
    void createUI();
    void updateStats();
    void updateHistogram();

    QPointer<ImageViewer> m_viewer;
    std::vector<ImageBuffer::DetectedStar> m_stars;

    StarAnalysisWorker* m_worker;

    /* UI widgets */
    StarHistogramWidget* m_histWidget;
    QTableWidget*        m_statsTable;
    QSlider*             m_threshSlider;
    QLabel*              m_threshLabel;
    QPushButton*         m_modeBtn;
    QCheckBox*           m_logCheck;
    QLabel*              m_statusLabel;
    QTimer*              m_debounceTimer;

    /** Histogram display mode. */
    enum Mode { Mode_HFR, Mode_Flux };
    Mode m_mode     = Mode_HFR;
    bool m_logScale = false;
};

#endif // STARANALYSISDIALOG_H