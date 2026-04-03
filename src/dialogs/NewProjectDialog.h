#ifndef NEW_PROJECT_DIALOG_H
#define NEW_PROJECT_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

#include "../stacking/StackingProject.h"

// =============================================================================
// NewProjectDialog
//
// Prompts the user for a project name and base directory, then creates
// the standard stacking folder structure (biases, darks, flats, lights,
// process, output) via StackingProject::create().
// =============================================================================
class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(QWidget* parent = nullptr);

    // Returns the resolved absolute path for the new project folder.
    QString projectPath() const;

    // Returns the user-entered project name.
    QString projectName() const;

    // Instantiates, creates, and returns a new StackingProject.
    // Returns nullptr if creation fails; caller takes ownership.
    Stacking::StackingProject* createProject();

private slots:
    void onBrowse();
    void onAccept();
    void validateInputs();

private:
    void setupUI();

    QLineEdit*   m_pathEdit  = nullptr;
    QLineEdit*   m_nameEdit  = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QPushButton* m_createBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};

#endif // NEW_PROJECT_DIALOG_H