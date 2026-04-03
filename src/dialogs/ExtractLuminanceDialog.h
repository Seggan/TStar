#ifndef EXTRACTLUMINANCEDIALOG_H
#define EXTRACTLUMINANCEDIALOG_H

#include "DialogBase.h"

#include <QDialog>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QCheckBox;
class MainWindowCallbacks;

/**
 * @brief Dialog for extracting a luminance channel from an RGB image.
 *
 * Supports multiple luminance computation methods (Rec.709, Rec.601, Rec.2020,
 * average, max, median, SNR-weighted, and fully custom weights).
 * The result is opened in a new viewer window.
 */
class ExtractLuminanceDialog : public DialogBase {
    Q_OBJECT

public:
    explicit ExtractLuminanceDialog(QWidget* parent = nullptr);

    /** @brief Collected user settings returned by getParams(). */
    struct Params {
        int                methodIndex;        ///< Maps to ChannelOps::LumaMethod enum.
        std::vector<float> customWeights;      ///< Per-channel weights when method == CUSTOM.
        std::vector<float> customNoiseSigma;   ///< Per-channel noise sigma for SNR method.
        bool               autoEstimateNoise;  ///< If true, estimate noise automatically.
    };

    /** @brief Read the current UI state into a Params struct. */
    Params getParams() const;

private slots:
    /** @brief Show/hide method-specific parameter groups. */
    void onMethodChanged(int index);

    /** @brief Compute luminance and open the result in a new window. */
    void onApply();

private:
    // -- Method selection --
    QComboBox* m_methodCombo;

    // -- Custom weights group --
    QGroupBox*      m_customGroup;
    QDoubleSpinBox* m_weightR;
    QDoubleSpinBox* m_weightG;
    QDoubleSpinBox* m_weightB;

    // -- SNR settings group --
    QGroupBox*      m_snrGroup;
    QCheckBox*      m_autoNoiseCheck;
    QDoubleSpinBox* m_sigmaR;
    QDoubleSpinBox* m_sigmaG;
    QDoubleSpinBox* m_sigmaB;

    // -- Cached interface pointer (obtained lazily via getCallbacks()) --
    MainWindowCallbacks* m_mainWindow;
};

#endif // EXTRACTLUMINANCEDIALOG_H