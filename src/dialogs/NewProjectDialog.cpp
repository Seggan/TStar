#include "NewProjectDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QDir>

// =============================================================================
// Constructor
// =============================================================================

NewProjectDialog::NewProjectDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Stacking Project"));
    setMinimumWidth(500);
    setupUI();
}

// =============================================================================
// UI Construction
// =============================================================================

void NewProjectDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // -------------------------------------------------------------------------
    // Informational label describing the folder structure that will be created
    // -------------------------------------------------------------------------
    QLabel* infoLabel = new QLabel(tr(
        "Create a new stacking project with organised folder structure:\n"
        "  biases/   - Bias frames\n"
        "  darks/    - Dark frames\n"
        "  flats/    - Flat frames\n"
        "  lights/   - Light frames\n"
        "  process/  - Calibrated and registered files\n"
        "  output/   - Final stacked results"
    ));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #aaa; padding: 10px;");
    mainLayout->addWidget(infoLabel);

    // -------------------------------------------------------------------------
    // Project location: base directory browser and project name field
    // -------------------------------------------------------------------------
    QGroupBox*   locationGroup  = new QGroupBox(tr("Project Location"));
    QVBoxLayout* locationLayout = new QVBoxLayout(locationGroup);

    QHBoxLayout* pathLayout = new QHBoxLayout();
    m_pathEdit = new QLineEdit();
    m_pathEdit->setPlaceholderText(tr("Select folder for project..."));
    m_browseBtn = new QPushButton(tr("Browse..."));
    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(m_browseBtn);
    locationLayout->addLayout(pathLayout);

    QHBoxLayout* nameLayout = new QHBoxLayout();
    nameLayout->addWidget(new QLabel(tr("Project Name:")));
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText(tr("My Stacking Project"));
    nameLayout->addWidget(m_nameEdit);
    locationLayout->addLayout(nameLayout);

    mainLayout->addWidget(locationGroup);

    // -------------------------------------------------------------------------
    // Windows-only: warn if symbolic link creation is unavailable
    // -------------------------------------------------------------------------
#ifdef Q_OS_WIN
    if (!Stacking::StackingProject::canCreateSymlinks()) {
        QLabel* symlinkWarning = new QLabel(tr(
            "Warning: Symbolic links require Administrator or Developer Mode.\n"
            "Files will be copied instead of linked."
        ));
        symlinkWarning->setStyleSheet("color: orange; padding: 5px;");
        mainLayout->addWidget(symlinkWarning);
    }
#endif

    // -------------------------------------------------------------------------
    // Dialog buttons: Cancel (left) and Create Project (right, default)
    // -------------------------------------------------------------------------
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_cancelBtn = new QPushButton(tr("Cancel"));
    m_createBtn = new QPushButton(tr("Create Project"));
    m_createBtn->setEnabled(false);
    m_createBtn->setDefault(true);

    buttonLayout->addWidget(m_cancelBtn);
    buttonLayout->addWidget(m_createBtn);
    mainLayout->addLayout(buttonLayout);

    // -------------------------------------------------------------------------
    // Connections
    // -------------------------------------------------------------------------
    connect(m_browseBtn, &QPushButton::clicked, this, &NewProjectDialog::onBrowse);
    connect(m_createBtn, &QPushButton::clicked, this, &NewProjectDialog::onAccept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_pathEdit,  &QLineEdit::textChanged, this, &NewProjectDialog::validateInputs);
    connect(m_nameEdit,  &QLineEdit::textChanged, this, &NewProjectDialog::validateInputs);
}

// =============================================================================
// Slots
// =============================================================================

void NewProjectDialog::onBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Project Location"), QDir::currentPath());

    if (!dir.isEmpty()) {
        m_pathEdit->setText(dir);
        // Auto-fill the name field from the selected directory name if still empty
        if (m_nameEdit->text().isEmpty())
            m_nameEdit->setText(QDir(dir).dirName());
    }
}

void NewProjectDialog::onAccept()
{
    const QString path = projectPath();
    QDir          dir(path);

    if (dir.exists() && !dir.isEmpty()) {
        if (Stacking::StackingProject::isProjectDirectory(path)) {
            QMessageBox::warning(this, tr("Project Exists"),
                tr("A project already exists at this location."));
            return;
        }

        const int ret = QMessageBox::question(
            this, tr("Directory Not Empty"),
            tr("The selected directory is not empty.\nCreate project structure anyway?"),
            QMessageBox::Yes | QMessageBox::No);

        if (ret != QMessageBox::Yes) return;
    }

    accept();
}

// Enable the Create button only when both path and name are non-empty.
void NewProjectDialog::validateInputs()
{
    m_createBtn->setEnabled(
        !m_pathEdit->text().isEmpty() && !m_nameEdit->text().isEmpty());
}

// =============================================================================
// Accessors
// =============================================================================

// Returns the full project path. If the name differs from the selected
// directory's own name, a sub-directory with the project name is appended.
QString NewProjectDialog::projectPath() const
{
    const QString base = m_pathEdit->text();
    const QString name = m_nameEdit->text();

    if (!base.isEmpty() && !name.isEmpty()) {
        if (QDir(base).dirName() != name)
            return base + "/" + name;
    }

    return base;
}

QString NewProjectDialog::projectName() const
{
    return m_nameEdit->text();
}

// =============================================================================
// Project Creation
// =============================================================================

Stacking::StackingProject* NewProjectDialog::createProject()
{
    auto* project = new Stacking::StackingProject();

    if (!project->create(projectPath())) {
        delete project;
        return nullptr;
    }

    project->setName(projectName());
    project->save();
    return project;
}