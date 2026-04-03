#pragma once

/**
 * @file PixelMathDialog.h
 * @brief Per-pixel mathematical expression evaluator dialog.
 *
 * Supports arbitrary expressions with channel variables (r, g, b),
 * cross-image references (I1..I10 with optional .r/.g/.b qualifiers),
 * partial channel assignment (R=...; G=...; B=...;), and a comprehensive
 * function library including trigonometric, logarithmic, relational,
 * and logical operations.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

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

/**
 * @brief Represents an image available as a variable (I1, I2, ...) in expressions.
 */
struct PMImageRef {
    QString      varId;             ///< Variable identifier ("I1", "I2", etc.)
    QString      name;              ///< Display name (e.g., window title)
    ImageBuffer* buffer = nullptr;  ///< Pointer to the image data
};

/**
 * @class PixelMathDialog
 * @brief Dialog for composing and evaluating per-pixel math expressions.
 */
class PixelMathDialog : public DialogBase {
    Q_OBJECT

public:
    explicit PixelMathDialog(QWidget* parent, ImageViewer* viewer);
    ~PixelMathDialog();

    /** @brief Update the target viewer reference. */
    void setViewer(ImageViewer* viewer);

    /** @brief Update the list of images available as I1..IN variables. */
    void setImages(const QVector<PMImageRef>& images);

    /**
     * @brief Evaluate an expression on the given buffer.
     *
     * Supports partial channel assignment (R=...; G=...; B=...;).
     * Channels without explicit assignment are left unchanged.
     * If no channel prefix is found, the expression applies to all channels.
     *
     * @param expr     The expression string.
     * @param buf      Target image buffer (modified in-place).
     * @param images   Cross-image references for I1..IN variables.
     * @param rescale  If true, rescale result range to [0, 1].
     * @param errorMsg Optional output for error description.
     * @return True on success.
     */
    static bool evaluateExpression(const QString& expr, ImageBuffer& buf,
                                   const QVector<PMImageRef>& images,
                                   bool rescale = false,
                                   QString* errorMsg = nullptr);

    /** @brief Backward-compatible overload without cross-image references. */
    static bool evaluateExpression(const QString& expr, ImageBuffer& buf,
                                   bool rescale = false,
                                   QString* errorMsg = nullptr);

signals:
    /** @brief Emitted when the user clicks Apply. */
    void apply(const QString& expression, bool rescale);

private slots:
    void onApply();

private:
    void setupUI();
    void updateImageListLabel();

    QPointer<ImageViewer>  m_viewer;
    QVector<PMImageRef>    m_images;

    // -- UI widgets --
    QLabel*         m_imageListLabel = nullptr;
    QPlainTextEdit* m_exprEdit       = nullptr;
    QCheckBox*      m_checkRescale   = nullptr;
    QPushButton*    m_btnApply       = nullptr;
    QPushButton*    m_btnCancel      = nullptr;
    QLabel*         m_statusLabel    = nullptr;
};