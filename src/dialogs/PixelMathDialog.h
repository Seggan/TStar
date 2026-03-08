#pragma once

#include "DialogBase.h"
#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QPointer>
#include <QVector>
#include "../ImageBuffer.h"
#include "../ImageViewer.h"

class MainWindowCallbacks;

// Represents an image available as a variable (I1, I2, ...) in Pixel Math expressions.
struct PMImageRef {
    QString varId;     // "I1", "I2", etc.
    QString name;      // Display name (window title)
    ImageBuffer* buffer = nullptr;
};

class PixelMathDialog : public DialogBase {
    Q_OBJECT
public:
    explicit PixelMathDialog(QWidget* parent, ImageViewer* viewer);
    ~PixelMathDialog();
    
    void setViewer(ImageViewer* viewer);

    // Update the list of images available as I1, I2, ... variables.
    void setImages(const QVector<PMImageRef>& images);
    
    // Evaluate expression on buf. images provides I1..IN cross-references.
    // Partial assignment: only channels with explicit R=..; G=..; B=..; are modified.
    // If no R=/G=/B= prefix is found the expression applies to all channels.
    static bool evaluateExpression(const QString& expr, ImageBuffer& buf,
                                   const QVector<PMImageRef>& images,
                                   bool rescale = false, QString* errorMsg = nullptr);

    // Backward-compatible overload (no cross-image references).
    static bool evaluateExpression(const QString& expr, ImageBuffer& buf,
                                   bool rescale = false, QString* errorMsg = nullptr);

signals:
    void apply(const QString& expression, bool rescale);

private slots:
    void onApply();

private:
    void setupUI();
    void updateImageListLabel();

    QPointer<ImageViewer> m_viewer;
    QVector<PMImageRef>   m_images;

    QLabel*        m_imageListLabel;
    QPlainTextEdit* m_exprEdit;
    QCheckBox*     m_checkRescale;
    QPushButton*   m_btnApply;
    QPushButton*   m_btnCancel;
    QLabel*        m_statusLabel;
};
