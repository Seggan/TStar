#ifndef BINNINGDIALOG_H
#define BINNINGDIALOG_H

#include "DialogBase.h"
#include <QPointer>
#include <QComboBox>
#include <QPushButton>

class BinningDialog : public DialogBase {
    Q_OBJECT
public:
    explicit BinningDialog(QWidget* parent = nullptr);
    ~BinningDialog() = default;

    void setViewer(class ImageViewer* v);

private slots:
    void onApply();

private:
    QPointer<class ImageViewer> m_viewer;
    QComboBox* m_binCombo;
};

#endif // BINNINGDIALOG_H
