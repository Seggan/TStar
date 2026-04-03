#include "ColorProfileDialog.h"
#include "MainWindowCallbacks.h"
#include "core/ColorProfileManager.h"
#include "ImageBuffer.h"
#include "ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDialogButtonBox>

// ----------------------------------------------------------------------------
// Construction
// ----------------------------------------------------------------------------

ColorProfileDialog::ColorProfileDialog(ImageBuffer* activeBuffer,
                                       ImageViewer*  viewer,
                                       QWidget*      parent)
    : DialogBase(parent, tr("Color Profile Management"), 450, 350)
    , m_activeBuffer(activeBuffer)
    , m_viewer(viewer)
{
    setupUI();
    loadCurrentInfo();
}

// ----------------------------------------------------------------------------
// Public interface
// ----------------------------------------------------------------------------

void ColorProfileDialog::setBuffer(ImageBuffer* buffer, ImageViewer* viewer)
{
    m_activeBuffer = buffer;
    m_viewer       = viewer;
    loadCurrentInfo();
}

// ----------------------------------------------------------------------------
// UI construction
// ----------------------------------------------------------------------------

void ColorProfileDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Current image info ---
    QGroupBox*   infoGroup = new QGroupBox(tr("Current Image"), this);
    QFormLayout* infoForm  = new QFormLayout(infoGroup);

    m_lblCurrentProfile = new QLabel(tr("Unknown"), this);
    infoForm->addRow(tr("Embedded Profile:"), m_lblCurrentProfile);

    mainLayout->addWidget(infoGroup);

    // --- Operation selection ---
    QGroupBox*   actionGroup  = new QGroupBox(tr("Action"), this);
    QVBoxLayout* actionLayout = new QVBoxLayout(actionGroup);

    m_radioAssign  = new QRadioButton(
        tr("Assign Profile (keep pixel values, change interpretation)"), this);
    m_radioConvert = new QRadioButton(
        tr("Convert to Profile (change pixel values to match visual intent)"), this);

    // Convert is the safer default for most workflows.
    m_radioConvert->setChecked(true);

    actionLayout->addWidget(m_radioAssign);
    actionLayout->addWidget(m_radioConvert);
    mainLayout->addWidget(actionGroup);

    // --- Destination profile ---
    QGroupBox*   targetGroup = new QGroupBox(tr("Destination Profile"), this);
    QFormLayout* targetForm  = new QFormLayout(targetGroup);

    m_targetProfileCombo = new QComboBox(this);
    m_targetProfileCombo->addItem("sRGB IEC61966-2.1",
                                  static_cast<int>(core::StandardProfile::sRGB));
    m_targetProfileCombo->addItem("Adobe RGB (1998)",
                                  static_cast<int>(core::StandardProfile::AdobeRGB));
    m_targetProfileCombo->addItem("ProPhoto RGB",
                                  static_cast<int>(core::StandardProfile::ProPhotoRGB));
    m_targetProfileCombo->addItem("Linear RGB",
                                  static_cast<int>(core::StandardProfile::LinearRGB));
    m_targetProfileCombo->addItem(tr("Custom ICC Profile..."),
                                  static_cast<int>(core::StandardProfile::Custom));

    targetForm->addRow(tr("Profile:"), m_targetProfileCombo);

    // Custom ICC file path row (enabled only when Custom is selected)
    QHBoxLayout* customPathLayout = new QHBoxLayout();

    m_customProfilePath = new QLineEdit(this);
    m_customProfilePath->setReadOnly(true);
    m_customProfilePath->setEnabled(false);

    m_btnBrowseProfile = new QPushButton(tr("Browse..."), this);
    m_btnBrowseProfile->setEnabled(false);

    customPathLayout->addWidget(m_customProfilePath);
    customPathLayout->addWidget(m_btnBrowseProfile);
    targetForm->addRow(tr("ICC File:"), customPathLayout);

    mainLayout->addWidget(targetGroup);
    mainLayout->addStretch();

    // --- Dialog buttons ---
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);

    m_btnApply = buttonBox->button(QDialogButtonBox::Apply);
    mainLayout->addWidget(buttonBox);

    // --- Signal/slot connections ---
    connect(m_btnBrowseProfile, &QPushButton::clicked,
            this, &ColorProfileDialog::browseCustomProfile);

    connect(m_targetProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ColorProfileDialog::onTargetProfileChanged);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_btnApply, &QPushButton::clicked,      this, &ColorProfileDialog::applyChanges);
}

// ----------------------------------------------------------------------------
// Slots
// ----------------------------------------------------------------------------

void ColorProfileDialog::loadCurrentInfo()
{
    if (!m_activeBuffer)
    {
        m_lblCurrentProfile->setText(tr("No active image."));
        m_btnApply->setEnabled(false);
        return;
    }

    const auto& meta = m_activeBuffer->metadata();
    QString profileName;

    if (!meta.iccProfileName.isEmpty())
    {
        profileName = meta.iccProfileName;
    }
    else if (!meta.iccData.isEmpty())
    {
        core::ColorProfile embedded(meta.iccData);
        profileName = embedded.name();
    }

    if (!profileName.isEmpty())
    {
        m_lblCurrentProfile->setText(profileName);
    }
    else
    {
        // No embedded profile - report the workspace default.
        core::ColorProfile workspace =
            core::ColorProfileManager::instance().workspaceProfile();
        m_lblCurrentProfile->setText(workspace.name() + tr(" (Workspace Default)"));
    }

    m_btnApply->setEnabled(true);
}

void ColorProfileDialog::onTargetProfileChanged(int /*index*/)
{
    const bool isCustom =
        m_targetProfileCombo->currentData().toInt() ==
        static_cast<int>(core::StandardProfile::Custom);

    m_customProfilePath->setEnabled(isCustom);
    m_btnBrowseProfile->setEnabled(isCustom);

    // Require a file path before allowing Apply when Custom is selected.
    if (isCustom && m_customProfilePath->text().isEmpty())
        m_btnApply->setEnabled(false);
    else
        m_btnApply->setEnabled(true);
}

void ColorProfileDialog::browseCustomProfile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select ICC Profile"),
        QString(),
        tr("ICC Profiles (*.icc *.icm);;All Files (*.*)"));

    if (!path.isEmpty())
    {
        m_customProfilePath->setText(path);
        m_btnApply->setEnabled(true);
    }
}

// ----------------------------------------------------------------------------
// Private helpers
// ----------------------------------------------------------------------------

core::ColorProfile ColorProfileDialog::getSelectedProfile() const
{
    const auto profileType =
        static_cast<core::StandardProfile>(
            m_targetProfileCombo->currentData().toInt());

    if (profileType == core::StandardProfile::Custom)
        return core::ColorProfile(m_customProfilePath->text());

    return core::ColorProfile(profileType);
}

void ColorProfileDialog::applyChanges()
{
    if (!m_activeBuffer)
        return;

    const core::ColorProfile targetProfile = getSelectedProfile();

    if (!targetProfile.isValid())
    {
        QMessageBox::warning(this, tr("Error"), tr("The selected profile is invalid."));
        return;
    }

    // Determine the source profile from the buffer, falling back to the workspace default.
    core::ColorProfile sourceProfile;
    if (!m_activeBuffer->metadata().iccData.isEmpty())
        sourceProfile = core::ColorProfile(m_activeBuffer->metadata().iccData);
    else
        sourceProfile = core::ColorProfileManager::instance().workspaceProfile();

    // Snapshot the current state for undo before any modification.
    if (m_viewer)
        m_viewer->pushUndo(tr("Color Profile"));

    if (m_radioConvert->isChecked())
    {
        // Convert: pixel data is transformed asynchronously to match the target profile.
        core::ColorProfileManager::instance().convertProfileAsync(
            *m_activeBuffer, sourceProfile, targetProfile);

        if (auto mw = getCallbacks())
            mw->logMessage(tr("Color Profile transformation started..."), 0);

        accept();
    }
    else
    {
        // Assign: only the metadata interpretation changes; pixel data is untouched.
        ImageBuffer::Metadata meta = m_activeBuffer->metadata();
        meta.iccProfileName  = targetProfile.name();
        meta.iccProfileType  = static_cast<int>(targetProfile.type());

        if (targetProfile.type() == core::StandardProfile::Custom)
            meta.iccData = targetProfile.iccData();
        else
            meta.iccData.clear(); // Standard profiles are identified by type enum alone.

        meta.colorProfileHandled = true;
        m_activeBuffer->setMetadata(meta);
        m_activeBuffer->setModified(true);

        // Refresh the viewer - color management may render differently under the new
        // interpretation even though the underlying pixel values have not changed.
        if (m_viewer)
            m_viewer->refreshDisplay(true);

        if (auto mw = getCallbacks())
            mw->logMessage(tr("Color Profile assigned."), 1, true);

        accept();
    }
}