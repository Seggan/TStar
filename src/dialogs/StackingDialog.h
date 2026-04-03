#ifndef STACKING_DIALOG_H
#define STACKING_DIALOG_H

/**
 * @file StackingDialog.h
 * @brief Image stacking dialog interface.
 *
 * Provides a comprehensive UI for:
 *   - Loading and managing image sequences (from folders or individual files)
 *   - Selecting and filtering images by quality metrics
 *   - Configuring stacking parameters (method, rejection, normalization, etc.)
 *   - Executing the stacking operation on a background thread
 *   - Viewing quality analysis plots and comet alignment setup
 */

#include <QDialog>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QProgressBar>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QTextEdit>
#include <QTabWidget>
#include <memory>

#include "../widgets/SimplePlotWidget.h"
#include "../stacking/StackingTypes.h"
#include "../stacking/StackingSequence.h"
#include "../stacking/StackingEngine.h"

class MainWindow;
class ImageViewer;

/**
 * @class StackingDialog
 * @brief Main dialog for image stacking workflow.
 */
class StackingDialog : public QDialog {
    Q_OBJECT

public:
    explicit StackingDialog(MainWindow* parent = nullptr);
    ~StackingDialog() override;

    /**
     * @brief Set an externally-created image sequence.
     * @param sequence Unique pointer to the image sequence (ownership transferred).
     */
    void setSequence(std::unique_ptr<Stacking::ImageSequence> sequence);

    /**
     * @brief Retrieve the stacking result buffer.
     * @return Pointer to the result ImageBuffer, or nullptr if not yet available.
     */
    ImageBuffer* result() { return m_result.get(); }

signals:
    /**
     * @brief Emitted when the stacking operation completes successfully.
     * @param result Pointer to the resulting ImageBuffer.
     */
    void stackingComplete(ImageBuffer* result);

private slots:
    /* Sequence management */
    void onLoadSequence();
    void onAddFiles();
    void onRemoveSelected();
    void onSelectAll();
    void onDeselectAll();
    void onSetReference();

    /* Parameter changes */
    void onMethodChanged(int index);
    void onRejectionChanged(int index);
    void onNormalizationChanged(int index);

    /* Stacking execution */
    void onStartStacking();
    void onCancel();

    /* Progress reporting */
    void onProgressChanged(const QString& message, double progress);
    void onLogMessage(const QString& message, const QString& color);
    void onStackingFinished(bool success);

    /* Table interaction */
    void onTableSelectionChanged();
    void onTableItemDoubleClicked(int row, int column);
    void onTableItemChanged(QTableWidgetItem* item);

    /* Comet alignment */
    void onPickCometFirst();
    void onPickCometLast();
    void onComputeCometShifts();
    void onViewerPointPicked(QPointF p);

private:
    /* UI construction */
    void setupUI();
    void setupSequenceGroup();
    void setupPlotTab();
    void setupCometTab();
    void setupParametersGroup();
    void setupOutputGroup();
    void setupProgressGroup();

    /* State updates */
    void updateTable();
    void updateParameterVisibility();
    void updateSummary();
    void updatePlot();
    void applyCurrentFilter();
    void onPlotTypeChanged(int index);

    /* Parameter collection */
    Stacking::StackingParams gatherParams() const;
    QString generateOutputFilename() const;

    /* --- UI Components: Sequence tab --- */
    QGroupBox*    m_sequenceGroup;
    QTableWidget* m_imageTable;
    QPushButton*  m_loadBtn;
    QPushButton*  m_addBtn;
    QPushButton*  m_removeBtn;
    QPushButton*  m_selectAllBtn;
    QPushButton*  m_deselectAllBtn;
    QPushButton*  m_setRefBtn;
    QLabel*       m_sequenceSummary;

    /* --- UI Components: Filtering --- */
    QComboBox*      m_filterCombo;
    QComboBox*      m_filterModeCombo;
    QDoubleSpinBox* m_filterValue;

    /* --- UI Components: Stacking parameters --- */
    QGroupBox*      m_paramsGroup;
    QComboBox*      m_methodCombo;
    QComboBox*      m_rejectionCombo;
    QDoubleSpinBox* m_sigmaLow;
    QDoubleSpinBox* m_sigmaHigh;
    QComboBox*      m_normCombo;
    QComboBox*      m_weightingCombo;
    QSpinBox*       m_featherSpin;

    /* --- UI Components: Stacking options --- */
    QCheckBox* m_force32BitCheck;
    QCheckBox* m_outputNormCheck;
    QCheckBox* m_equalizeRGBCheck;
    QCheckBox* m_maximizeFramingCheck;
    QCheckBox* m_createRejMapsCheck;
    QCheckBox* m_fastNormCheck;
    QCheckBox* m_overlapNormCheck;

    /* Debayer options */
    QCheckBox* m_debayerCheck;
    QComboBox* m_bayerPatternCombo;
    QComboBox* m_debayerAlgoCombo;

    /* Drizzle options */
    QCheckBox*      m_drizzleCheck;
    QDoubleSpinBox* m_drizzleScale;
    QDoubleSpinBox* m_drizzlePixFrac;
    QCheckBox*      m_drizzleFastCheck;

    /* --- UI Components: Analysis plot tab --- */
    QTabWidget*       m_tabWidget;
    QWidget*          m_plotTab;
    SimplePlotWidget* m_plotWidget;
    QComboBox*        m_plotTypeCombo;

    /* --- UI Components: Comet alignment tab --- */
    QWidget*     m_cometTab;
    QLabel*      m_cometStatusLabel;
    QPushButton* m_pickCometFirstBtn;
    QPushButton* m_pickCometLastBtn;
    QPushButton* m_computeCometBtn;
    int          m_cometRef1Index = -1;
    int          m_cometRef2Index = -1;

    /* --- UI Components: Output --- */
    QGroupBox*   m_outputGroup;
    QLineEdit*   m_outputPath;
    QPushButton* m_browseBtn;

    /* --- UI Components: Progress --- */
    QGroupBox*    m_progressGroup;
    QProgressBar* m_progressBar;
    QTextEdit*    m_logText;
    QPushButton*  m_startBtn;
    QPushButton*  m_cancelBtn;

    /* --- Application state --- */
    std::unique_ptr<Stacking::ImageSequence> m_sequence;
    std::unique_ptr<Stacking::StackingWorker> m_worker;
    std::unique_ptr<ImageBuffer> m_result;
    MainWindow* m_mainWindow;
    bool m_isRunning = false;
};

#endif // STACKING_DIALOG_H