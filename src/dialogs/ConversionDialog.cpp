#include "ConversionDialog.h"
#include "../ImageBuffer.h"
#include "../io/FitsWrapper.h"
#include "../io/TiffIO.h"
#include "core/ResourceManager.h"

#include <QApplication>
#include <QAtomicInt>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <mutex>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

// ============================================================================
// Construction
// ============================================================================

ConversionDialog::ConversionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Convert RAW Files to FITS"));
    setMinimumSize(600, 500);
    setupUI();
}

// ============================================================================
// UI construction
// ============================================================================

void ConversionDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Informational header
    QLabel* infoLabel = new QLabel(
        tr("Convert RAW camera files (CR2, NEF, ARW, DNG, etc.) to FITS format "
           "for astrophotography processing."),
        this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #aaa; padding: 5px;");
    mainLayout->addWidget(infoLabel);

    // --- Input file list ---
    QGroupBox*   filesGroup  = new QGroupBox(tr("Input Files"), this);
    QVBoxLayout* filesLayout = new QVBoxLayout(filesGroup);

    m_fileList = new QListWidget(this);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    filesLayout->addWidget(m_fileList);

    QHBoxLayout* fileButtonsLayout = new QHBoxLayout();
    m_addBtn    = new QPushButton(tr("Add Files..."), this);
    m_removeBtn = new QPushButton(tr("Remove"),       this);
    m_clearBtn  = new QPushButton(tr("Clear All"),    this);

    fileButtonsLayout->addWidget(m_addBtn);
    fileButtonsLayout->addWidget(m_removeBtn);
    fileButtonsLayout->addWidget(m_clearBtn);
    fileButtonsLayout->addStretch();
    filesLayout->addLayout(fileButtonsLayout);
    mainLayout->addWidget(filesGroup);

    // --- Output settings ---
    QGroupBox*   outputGroup  = new QGroupBox(tr("Output Settings"), this);
    QVBoxLayout* outputLayout = new QVBoxLayout(outputGroup);

    // Output directory row
    QHBoxLayout* dirLayout = new QHBoxLayout();
    dirLayout->addWidget(new QLabel(tr("Output Directory:"), this));

    m_outputDir = new QLineEdit(this);
    m_outputDir->setPlaceholderText(tr("Same as input files"));
    dirLayout->addWidget(m_outputDir);

    m_browseBtn = new QPushButton(tr("Browse..."), this);
    dirLayout->addWidget(m_browseBtn);
    outputLayout->addLayout(dirLayout);

    // Format and bit-depth options row
    QHBoxLayout* optionsLayout = new QHBoxLayout();

    optionsLayout->addWidget(new QLabel(tr("Output Format:"), this));
    m_outputFormat = new QComboBox(this);
    m_outputFormat->addItems({ "FITS", "XISF", "TIFF" });
    optionsLayout->addWidget(m_outputFormat);

    optionsLayout->addWidget(new QLabel(tr("Bit Depth:"), this));
    m_bitDepth = new QComboBox(this);
    m_bitDepth->addItems({ "16-bit", "32-bit float" });
    m_bitDepth->setCurrentIndex(1); // Default to 32-bit float for astrophotography
    optionsLayout->addWidget(m_bitDepth);

    optionsLayout->addStretch();
    outputLayout->addLayout(optionsLayout);

    // Debayer option
    m_debayerCheck = new QCheckBox(tr("Apply debayering (for color cameras)"), this);
    m_debayerCheck->setChecked(true);
    outputLayout->addWidget(m_debayerCheck);

    mainLayout->addWidget(outputGroup);

    // --- Progress ---
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(true);
    m_progress->setValue(0);
    mainLayout->addWidget(m_progress);

    m_statusLabel = new QLabel(tr("Ready"), this);
    m_statusLabel->setStyleSheet("color: #888;");
    mainLayout->addWidget(m_statusLabel);

    // --- Action buttons ---
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_closeBtn   = new QPushButton(tr("Close"),   this);
    m_convertBtn = new QPushButton(tr("Convert"), this);
    m_convertBtn->setDefault(true);

    buttonLayout->addWidget(m_closeBtn);
    buttonLayout->addWidget(m_convertBtn);
    mainLayout->addLayout(buttonLayout);

    // --- Signal/slot connections ---
    connect(m_addBtn,    &QPushButton::clicked, this, &ConversionDialog::onAddFiles);
    connect(m_removeBtn, &QPushButton::clicked, this, &ConversionDialog::onRemoveFiles);
    connect(m_clearBtn,  &QPushButton::clicked, this, &ConversionDialog::onClearList);
    connect(m_browseBtn, &QPushButton::clicked, this, &ConversionDialog::onBrowseOutput);
    connect(m_convertBtn,&QPushButton::clicked, this, &ConversionDialog::onConvert);
    connect(m_closeBtn,  &QPushButton::clicked, this, &QDialog::reject);

    connect(m_fileList, &QListWidget::itemSelectionChanged,
            this, &ConversionDialog::updateStatus);
}

// ============================================================================
// File list management
// ============================================================================

void ConversionDialog::onAddFiles()
{
    QSettings settings("TStar", "TStar");
    const QString initialDir =
        settings.value("Conversion/InputFolder", QDir::currentPath()).toString();

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Select RAW Files"),
        initialDir,
        tr("RAW Files (*.cr2 *.CR2 *.nef *.NEF *.arw *.ARW *.dng *.DNG "
           "*.orf *.ORF *.rw2 *.RW2 *.raf *.RAF *.pef *.PEF);;"
           "TIFF Files (*.tif *.tiff *.TIF *.TIFF);;"
           "All Files (*)"));

    if (!files.isEmpty())
        settings.setValue("Conversion/InputFolder",
                          QFileInfo(files.first()).absolutePath());

    for (const QString& file : files)
    {
        // Skip duplicates already present in the list.
        bool exists = false;
        for (int i = 0; i < m_fileList->count(); ++i)
        {
            if (m_fileList->item(i)->data(Qt::UserRole).toString() == file)
            {
                exists = true;
                break;
            }
        }

        if (!exists)
        {
            QListWidgetItem* item = new QListWidgetItem(QFileInfo(file).fileName());
            item->setData(Qt::UserRole, file);
            m_fileList->addItem(item);
        }
    }

    updateStatus();
}

void ConversionDialog::onRemoveFiles()
{
    qDeleteAll(m_fileList->selectedItems());
    updateStatus();
}

void ConversionDialog::onClearList()
{
    m_fileList->clear();
    updateStatus();
}

void ConversionDialog::onBrowseOutput()
{
    QSettings settings("TStar", "TStar");
    const QString initialDir = settings.value(
        "Conversion/OutputFolder",
        m_outputDir->text().isEmpty() ? QDir::currentPath() : m_outputDir->text()
    ).toString();

    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Output Directory"), initialDir);

    if (!dir.isEmpty())
    {
        m_outputDir->setText(dir);
        settings.setValue("Conversion/OutputFolder", dir);
    }
}

void ConversionDialog::updateStatus()
{
    const int count = m_fileList->count();
    m_statusLabel->setText(tr("%1 file(s) ready for conversion").arg(count));
    m_convertBtn->setEnabled(count > 0);
}

// ============================================================================
// Conversion
// ============================================================================

void ConversionDialog::onConvert()
{
    const int count = m_fileList->count();
    if (count == 0)
    {
        QMessageBox::warning(this, tr("No Files"), tr("Please add files to convert."));
        return;
    }

    const QString outDir      = m_outputDir->text();
    const int     bitDepthVal = (m_bitDepth->currentIndex() == 0) ? 16 : 32;

    m_progress->setMaximum(count);
    m_progress->setValue(0);
    m_convertBtn->setEnabled(false);
    m_statusLabel->setText(tr("Preparing conversion..."));

    // Disable input controls during batch processing.
    m_addBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);
    m_clearBtn->setEnabled(false);

    // Describes a single file conversion job.
    struct ConvertJob
    {
        QString filePath;
        QString outPath;
        int     listIndex;
    };

    // Shared context for tracking cross-thread progress safely.
    struct Context
    {
        QList<ConvertJob> jobs;
        QAtomicInt        processed{ 0 };
        QAtomicInt        successes{ 0 };
        qint64            startTime;
    };

    auto ctx       = std::make_shared<Context>();
    ctx->startTime = QDateTime::currentMSecsSinceEpoch();

    for (int i = 0; i < count; ++i)
    {
        const QString filePath  = m_fileList->item(i)->data(Qt::UserRole).toString();
        const QString targetDir = outDir.isEmpty()
            ? QFileInfo(filePath).absolutePath()
            : outDir;

        const QString outPath = QDir(targetDir).absoluteFilePath(
            QFileInfo(filePath).completeBaseName() + ".fit");

        m_fileList->item(i)->setForeground(Qt::white);
        ctx->jobs.append({ filePath, outPath, i });
    }

    const int maxThreads = ResourceManager::instance().maxThreads();
    QThreadPool::globalInstance()->setMaxThreadCount(maxThreads);

    auto* watcher = new QFutureWatcher<void>(this);

    // Re-enable controls and report results when all jobs complete.
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, count, ctx]()
    {
        m_convertBtn->setEnabled(true);
        m_addBtn->setEnabled(true);
        m_removeBtn->setEnabled(true);
        m_clearBtn->setEnabled(true);

        const qint64 elapsed      = QDateTime::currentMSecsSinceEpoch() - ctx->startTime;
        const int    successCount = ctx->successes.loadRelaxed();

        m_statusLabel->setText(
            tr("Converted %1 files in %2 ms").arg(successCount).arg(elapsed));

        if (successCount == count)
        {
            QMessageBox::information(
                this, tr("Conversion Complete"),
                tr("Successfully converted %1 files in %2 s.")
                    .arg(successCount)
                    .arg(elapsed / 1000.0, 0, 'f', 1));
        }
        else
        {
            QMessageBox::warning(
                this, tr("Conversion Complete"),
                tr("Converted %1 of %2 files. Some files failed.")
                    .arg(successCount).arg(count));
        }

        watcher->deleteLater();
    });

    // Each job runs on a thread-pool thread; thread_local buffers avoid
    // repeated allocation and are safe because each thread owns its own copy.
    QFuture<void> future = QtConcurrent::map(
        ctx->jobs,
        [this, ctx, count, bitDepthVal](const ConvertJob& job)
    {
        bool success = false;

        static thread_local ImageBuffer threadBuffer;
        threadBuffer.resize(0, 0, 0);
        threadBuffer.setMetadata(ImageBuffer::Metadata());

        bool        loaded = false;
        const QString ext  = QFileInfo(job.filePath).suffix().toLower();

        if (ext == "fit" || ext == "fits" || ext == "fts")
        {
            loaded = Stacking::FitsIO::read(job.filePath, threadBuffer);
        }
        else if (ext == "tif" || ext == "tiff")
        {
            loaded = Stacking::TiffIO::read(job.filePath, threadBuffer);
        }
        else
        {
#ifdef HAVE_LIBRAW
            libraw_data_t* lr = libraw_init(0);
            if (lr)
            {
                if (libraw_open_file(lr, job.filePath.toLocal8Bit().constData()) == LIBRAW_SUCCESS &&
                    libraw_unpack(lr) == LIBRAW_SUCCESS)
                {
                    const int w    = lr->rawdata.sizes.raw_width;
                    const int left = lr->rawdata.sizes.left_margin;
                    const int top  = lr->rawdata.sizes.top_margin;
                    const int vw   = lr->rawdata.sizes.width;
                    const int vh   = lr->rawdata.sizes.height;

                    threadBuffer.resize(vw, vh, 1);
                    float*          dst = threadBuffer.data().data();
                    unsigned short* src = (unsigned short*)lr->rawdata.raw_alloc;
                    if (!src) src = lr->rawdata.raw_image;

                    if (src)
                    {
                        const float black   = (float)lr->color.black;
                        const float maximum = (float)lr->color.maximum;
                        float       range   = maximum - black;
                        if (range <= 0.0f) range = 65535.0f;

                        // Normalize white-balance multipliers relative to green.
                        float mul[4];
                        for (int k = 0; k < 4; ++k) mul[k] = lr->color.cam_mul[k];
                        float gNorm = mul[1];
                        if (gNorm <= 0.0f && mul[3] > 0.0f) gNorm = mul[3];
                        if (gNorm <= 0.0f) gNorm = 1.0f;
                        for (int k = 0; k < 4; ++k) mul[k] /= gNorm;

                        const unsigned int f = lr->idata.filters;

                        for (int y = 0; y < vh; ++y)
                        {
                            const int row = y + top;
                            for (int x = 0; x < vw; ++x)
                            {
                                const int   col        = x + left;
                                const int   patternIdx =
                                    (f >> ((((row) << 1) & 14) + (col & 1)) * 2) & 3;
                                const float val =
                                    (float)src[row * w + col];
                                dst[y * vw + x] =
                                    std::max(0.0f, (val - black) * mul[patternIdx] / range);
                            }
                        }

                        loaded = true;

                        // Resolve the Bayer pattern string from the filter mask.
                        QString bayerPat = "RGGB";
                        if      (f == 0x94949494) bayerPat = "RGGB";
                        else if (f == 0x16161616) bayerPat = "BGGR";
                        else if (f == 0x61616161) bayerPat = "GRBG";
                        else if (f == 0x49494949) bayerPat = "GBRG";

                        // Adjust pattern for sensor margin offsets.
                        if (left % 2 != 0)
                        {
                            if      (bayerPat == "RGGB") bayerPat = "GRBG";
                            else if (bayerPat == "BGGR") bayerPat = "GBRG";
                            else if (bayerPat == "GRBG") bayerPat = "RGGB";
                            else if (bayerPat == "GBRG") bayerPat = "BGGR";
                        }
                        if (top % 2 != 0)
                        {
                            if      (bayerPat == "RGGB") bayerPat = "GBRG";
                            else if (bayerPat == "BGGR") bayerPat = "GRBG";
                            else if (bayerPat == "GRBG") bayerPat = "BGGR";
                            else if (bayerPat == "GBRG") bayerPat = "RGGB";
                        }

                        threadBuffer.metadata().isMono        = true;
                        threadBuffer.metadata().bayerPattern  = bayerPat;
                        threadBuffer.metadata().xisfProperties["BayerPattern"] = bayerPat;
                        threadBuffer.metadata().exposure      = lr->other.shutter;
                        threadBuffer.metadata().focalLength   = lr->other.focal_len;

                        if (lr->other.timestamp > 0)
                        {
                            QDateTime dt = QDateTime::fromSecsSinceEpoch(
                                static_cast<qint64>(lr->other.timestamp),
                                QTimeZone::utc());
                            threadBuffer.metadata().dateObs = dt.toString(Qt::ISODateWithMs);
                            threadBuffer.metadata().rawHeaders.push_back(
                                { "DATE-OBS", threadBuffer.metadata().dateObs,
                                  "Observation date" });
                        }
                        if (lr->other.iso_speed > 0.0f)
                            threadBuffer.metadata().rawHeaders.push_back(
                                { "ISOSPEED",
                                  QString::number(static_cast<int>(lr->other.iso_speed)),
                                  "ISO speed" });
                        if (lr->other.shutter > 0.0f)
                            threadBuffer.metadata().rawHeaders.push_back(
                                { "EXPTIME",
                                  QString::number(lr->other.shutter, 'f', 6),
                                  "Exposure time [s]" });
                        if (lr->other.aperture > 0.0f)
                            threadBuffer.metadata().rawHeaders.push_back(
                                { "APERTURE",
                                  QString::number(lr->other.aperture, 'f', 1),
                                  "Aperture (f-number)" });
                        if (lr->other.focal_len > 0.0f)
                            threadBuffer.metadata().rawHeaders.push_back(
                                { "FOCALLEN",
                                  QString::number(static_cast<int>(lr->other.focal_len)),
                                  "Focal length [mm]" });
                    }
                }
                libraw_close(lr);
            }
#endif
        }

        if (loaded)
        {
            if (Stacking::FitsIO::write(job.outPath, threadBuffer, bitDepthVal))
            {
                success = true;
                ctx->successes.fetchAndAddRelaxed(1);
            }
        }

        const int p = ctx->processed.fetchAndAddRelaxed(1) + 1;

        // Marshal UI updates back to the main thread.
        QMetaObject::invokeMethod(this, [this, p, count, job, success]()
        {
            m_progress->setValue(p);
            m_statusLabel->setText(
                tr("Converting %1 of %2...").arg(p).arg(count));

            if (QListWidgetItem* item = m_fileList->item(job.listIndex))
                item->setForeground(success ? Qt::green : Qt::red);
        },
        Qt::QueuedConnection);
    });

    watcher->setFuture(future);
}