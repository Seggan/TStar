#ifndef HELPDIALOG_H
#define HELPDIALOG_H

// =============================================================================
// HelpDialog.h
// Application help and tutorial dialog. Displays HTML-formatted documentation
// inside a QTextBrowser.
// =============================================================================

#include <QDialog>
#include <QScrollArea>
#include <QTextBrowser>

#include "DialogBase.h"

class HelpDialog : public DialogBase {
    Q_OBJECT

public:
    explicit HelpDialog(QWidget* parent = nullptr);

private:
    // Build and configure all UI widgets.
    void setupUI();

    // Generate the complete HTML help content (translatable).
    QString buildHelpContent();

    QTextBrowser* m_browser = nullptr;
};

#endif // HELPDIALOG_H