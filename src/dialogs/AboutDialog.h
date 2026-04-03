#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include "DialogBase.h"

/**
 * @brief Simple informational dialog displaying application version,
 *        author details, and external links.
 */
class AboutDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget*       parent         = nullptr,
                         const QString& version        = QString(),
                         const QString& buildTimestamp = QString());
};

#endif // ABOUTDIALOG_H