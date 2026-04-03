#ifndef CONVERSION_DIALOG_H
#define CONVERSION_DIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>

/**
 * @brief Dialog for batch-converting RAW camera files to FITS/XISF/TIFF.
 *
 * Supports CR2, NEF, ARW, DNG, and other LibRaw-compatible formats.
 * Conversion runs asynchronously via QtConcurrent to keep the UI responsive.
 */
class ConversionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConversionDialog(QWidget* parent = nullptr);

private slots:
    void onAddFiles();
    void onRemoveFiles();
    void onClearList();
    void onBrowseOutput();
    void onConvert();
    void updateStatus();

private:
    void setupUI();

    // Input file list
    QListWidget* m_fileList;

    // Output settings
    QLineEdit* m_outputDir;
    QPushButton* m_browseBtn;
    QComboBox*   m_outputFormat;
    QComboBox*   m_bitDepth;
    QCheckBox*   m_debayerCheck;

    // Progress reporting
    QProgressBar* m_progress;
    QLabel*       m_statusLabel;

    // Action buttons
    QPushButton* m_addBtn;
    QPushButton* m_removeBtn;
    QPushButton* m_clearBtn;
    QPushButton* m_convertBtn;
    QPushButton* m_closeBtn;
};

#endif // CONVERSION_DIALOG_H