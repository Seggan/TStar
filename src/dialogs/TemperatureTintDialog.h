#ifndef TEMPERATURETINTDIALOG_H
#define TEMPERATURETINTDIALOG_H

// =============================================================================
// TemperatureTintDialog.h
// Dialog for adjusting colour temperature and tint via per-channel gain
// multiplication, with live preview and optional shadow/highlight protection.
// =============================================================================

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../ImageViewer.h"

#include <QPointer>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>

class TemperatureTintDialog : public DialogBase {
    Q_OBJECT

public:
    explicit TemperatureTintDialog(QWidget* parent, ImageViewer* viewer);
    ~TemperatureTintDialog();

    void setViewer(ImageViewer* viewer);
    void triggerPreview();

    /// Serializable state for save/restore of slider positions.
    struct State {
        int temperature;  ///< Range: -100 to +100
        int tint;         ///< Range: -100 to +100
    };

    State getState() const;
    void  setState(const State& s);
    void  resetState();

signals:
    /// Emitted after the apply action completes.
    void applyInternal();

private slots:
    void onSliderChanged();
    void handleApply();

private:
    /// Computes per-channel RGB gain factors from current slider positions.
    void computeGain(float& r, float& g, float& b) const;
    void setupUI();

    // Viewer and buffer state
    QPointer<ImageViewer> m_viewer;
    ImageBuffer*          m_buffer;
    ImageBuffer           m_originalBuffer;
    bool                  m_applied = false;

    // Slider controls
    QSlider* m_sldTemperature;
    QSlider* m_sldTint;
    QLabel*  m_valTemperature;
    QLabel*  m_valTint;

    // Options
    QCheckBox* m_chkPreview;
    QCheckBox* m_chkProtect;   ///< Protect shadows and highlights from colour casts
};

#endif // TEMPERATURETINTDIALOG_H