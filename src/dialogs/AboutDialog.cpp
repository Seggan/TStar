#include "AboutDialog.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QCoreApplication>

// =============================================================================
// Constructor
// =============================================================================

AboutDialog::AboutDialog(QWidget*       parent,
                         const QString& version,
                         const QString& buildTimestamp)
    : DialogBase(parent, tr("About TStar"), 450, 250)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 15, 20, 15);
    layout->setSpacing(10);

    // Resolve defaults for optional constructor arguments.
    QString v  = version.isEmpty()        ? QStringLiteral("1.0.0")                : version;
    QString bt = buildTimestamp.isEmpty() ? QStringLiteral(__DATE__ " " __TIME__)   : buildTimestamp;

    // -------------------------------------------------------------------------
    // Build the HTML content block.
    // -------------------------------------------------------------------------
    const QString linkStyle = QStringLiteral("color: #4da6ff; text-decoration: none;");

    QStringList lines;
    lines << QString("<h2>TStar %1</h2>").arg(v);
    lines << QString("<p>%1</p>").arg(tr("Written by Fabio Tempera"));

    lines << QString("<p>%1 <a href='https://github.com/ft2801' style='%2'>%3</a></p>")
                 .arg(tr("Link to my ")).arg(linkStyle).arg(tr("GitHub profile"));

    lines << QString("<p>%1 <a href='https://ft2801.github.io/Portfolio' style='%2'>%3</a></p>")
                 .arg(tr("Link to my ")).arg(linkStyle).arg(tr("portfolio"));

    lines << QString("<p>%1 <a href='https://ft2801.github.io/FT-Astrophotography' style='%2'>%3</a></p>")
                 .arg(tr("Link to my ")).arg(linkStyle).arg(tr("astronomy website"));

    lines << QString("<p>%1</p>").arg(tr("Copyright (C) 2026"));

    if (!bt.isEmpty())
        lines << QString("<p><b>%1</b> %2</p>").arg(tr("Build:")).arg(bt);

    // -------------------------------------------------------------------------
    // Primary information label with clickable external links.
    // -------------------------------------------------------------------------
    QLabel* infoLabel = new QLabel(lines.join(QString()), this);
    infoLabel->setTextFormat(Qt::RichText);
    infoLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    infoLabel->setOpenExternalLinks(true);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setWordWrap(true);

    // -------------------------------------------------------------------------
    // Secondary descriptive subtitle label.
    // -------------------------------------------------------------------------
    QLabel* descLabel = new QLabel(
        tr("TStar is a professional astrophotography image processing application."), this);
    descLabel->setStyleSheet("font-style: italic; color: #aaaaaa;");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);

    layout->addWidget(infoLabel);
    layout->addWidget(descLabel);

    setMinimumWidth(350);
    adjustSize();
}