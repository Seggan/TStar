#ifndef STARNETDIALOG_H
#define STARNETDIALOG_H

// =============================================================================
// StarNetDialog.h
// Dialog for configuring and executing StarNet++ star removal on the active image.
// =============================================================================

#include "DialogBase.h"

#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>

class MainWindowCallbacks;

class StarNetDialog : public DialogBase {
    Q_OBJECT

public:
    explicit StarNetDialog(QWidget* parent = nullptr);

private slots:
    void onRun();

private:
    // Parameter controls
    QCheckBox*   m_chkLinear;
    QCheckBox*   m_chkGenerateMask;
    QCheckBox*   m_chkGpu;
    QSpinBox*    m_spinStride;

    // Action button
    QPushButton* m_btnRun;
};

#endif // STARNETDIALOG_H