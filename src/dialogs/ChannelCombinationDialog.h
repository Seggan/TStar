#ifndef CHANNEL_COMBINATION_DIALOG_H
#define CHANNEL_COMBINATION_DIALOG_H

// =============================================================================
// ChannelCombinationDialog.h
//
// Dialog for combining separate monochrome images into a single RGB image.
// Supports automatic channel assignment by name heuristics and optional
// linear fit (median equalization) before combination.
// =============================================================================

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <vector>

#include "../ImageBuffer.h"
#include "DialogBase.h"

class QCheckBox;

class ChannelCombinationDialog : public DialogBase {
    Q_OBJECT

public:
    /**
     * Describes an available image source that can be assigned
     * to a color channel.
     */
    struct ChannelSource {
        QString     name;
        ImageBuffer buffer;
    };

    explicit ChannelCombinationDialog(
        const std::vector<ChannelSource>& availableSources,
        QWidget* parent = nullptr);

    /** Returns the combined RGB image buffer after a successful apply. */
    ImageBuffer getResult() const { return m_result; }

private slots:
    void onApply();
    void onCancel();

private:
    QComboBox*  m_comboR;
    QComboBox*  m_comboG;
    QComboBox*  m_comboB;
    QCheckBox*  m_checkLinearFit = nullptr;

    std::vector<ChannelSource> m_sources;
    ImageBuffer                m_result;
};

#endif // CHANNEL_COMBINATION_DIALOG_H