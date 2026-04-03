// =============================================================================
// main.cpp
//
// Application entry point for TStar. Handles:
//   - Safe graphics fallback (software rendering for unstable GPU drivers)
//   - Self-healing startup failure detection
//   - Global exception handler installation
//   - Logging and resource manager initialization
//   - Splash screen with staged progress updates
//   - Translation loading (system locale or user preference)
//   - Dark theme (Fusion style) and global stylesheet
//   - Main window construction and display
//   - Workspace project restoration from command-line arguments
// =============================================================================

#include "core/TStarApplication.h"
#include "core/GlobalExceptionHandler.h"
#include "core/Logger.h"
#include "core/ResourceManager.h"
#include "core/TaskManager.h"
#include "core/Version.h"
#include "MainWindow.h"
#include "widgets/SplashScreen.h"

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QTimer>
#include <QThread>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QIcon>
#include <QTranslator>
#include <QLocale>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QSettings>

// =============================================================================
// main
// =============================================================================

int main(int argc, char* argv[])
{
    // -------------------------------------------------------------------------
    // Safe graphics fallback
    //
    // If the previous launch crashed during initialization, or if the user
    // explicitly requests it, force software rendering to bypass GPU issues.
    // -------------------------------------------------------------------------

    bool safeGraphics = false;

    for (int i = 0; i < argc; ++i) {
        if (QString(argv[i]) == "--safe-graphics")
            safeGraphics = true;
    }
    if (qEnvironmentVariableIsSet("TSTAR_SAFE_GRAPHICS"))
        safeGraphics = true;

    // Self-healing: detect if the previous launch failed to complete startup
    QSettings startupSettings("TStar", "StartupCheck");
    const bool lastLaunchSuccessful =
        startupSettings.value("last_launch_successful", true).toBool();

    if (!lastLaunchSuccessful) {
        safeGraphics = true;
        qDebug() << "TStar: Detected previous startup failure. "
                    "Engaging Safe Graphics Mode (Software OpenGL).";
    }

    // Mark startup as incomplete; MainWindow sets this to true on success
    startupSettings.setValue("last_launch_successful", false);
    startupSettings.sync();

    if (safeGraphics) {
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
        qputenv("QT_QUICK_BACKEND", "software");
        qputenv("QSG_RHI_BACKEND",  "software");
    }

    // -------------------------------------------------------------------------
    // Exception handling
    // -------------------------------------------------------------------------

    GlobalExceptionHandler::init();

    // -------------------------------------------------------------------------
    // Application object and core subsystem initialization
    // -------------------------------------------------------------------------

    QCoreApplication::setOrganizationName("TStar");
    QCoreApplication::setOrganizationDomain("tstar.app");
    QCoreApplication::setApplicationName("TStar");

    TStarApplication app(argc, argv);

    Logger::init();
    ResourceManager::instance().init();
    Threading::TaskManager::instance().init();

    Logger::info("TStar starting up...", "Main");
    qDebug() << "TStar startup - Debug Log Initialized";

    // -------------------------------------------------------------------------
    // Locate the application logo for the splash screen and window icon
    // -------------------------------------------------------------------------

    const QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);

    const QStringList searchPaths = {
        dir.filePath("images/Logo.png"),
        QDir::cleanPath(dir.absoluteFilePath("../Resources/images/Logo.png")),
        dir.filePath("Logo.png"),
        QDir::cleanPath(dir.absoluteFilePath("../src/images/Logo.png")),
        QDir::cleanPath(dir.absoluteFilePath("../../src/images/Logo.png")),
    };

    QString logoPath;
    for (const QString& path : searchPaths) {
        if (QFile::exists(path)) {
            logoPath = path;
            break;
        }
    }

    // -------------------------------------------------------------------------
    // Splash screen
    // -------------------------------------------------------------------------

    SplashScreen* splash = new SplashScreen(logoPath);

    // -------------------------------------------------------------------------
    // Load translations
    // -------------------------------------------------------------------------

    QSettings settings;
    const QString lang = settings.value("general/language", "System").toString();

    QTranslator* translator = new QTranslator(&app);
    bool translationLoaded  = false;

    if (lang == "System") {
        // Attempt to load the best matching system language
        const QStringList uiLanguages = QLocale::system().uiLanguages();
        for (const QString& locale : uiLanguages) {
            const QString langCode = QLocale(locale).name().left(2);

            if (translator->load(":/translations/tstar_" + langCode)) {
                translationLoaded = true;
                break;
            }
            if (translator->load("tstar_" + langCode,
                    QCoreApplication::applicationDirPath() + "/translations") ||
                translator->load("tstar_" + langCode,
                    QCoreApplication::applicationDirPath() + "/../Resources/translations"))
            {
                translationLoaded = true;
                break;
            }
        }
    } else if (lang != "en") {
        // Explicit language selection
        if (translator->load(":/translations/tstar_" + lang)) {
            translationLoaded = true;
        } else if (
            translator->load("tstar_" + lang,
                QCoreApplication::applicationDirPath() + "/translations") ||
            translator->load("tstar_" + lang,
                QCoreApplication::applicationDirPath() + "/../Resources/translations"))
        {
            translationLoaded = true;
        }
    }

    if (translationLoaded) {
        app.installTranslator(translator);
    }

    // -------------------------------------------------------------------------
    // Show splash screen with staged progress
    // -------------------------------------------------------------------------

    // Brief pause to let the OS and app settle before showing the splash
    QThread::msleep(200);

    splash->show();
    splash->startFadeIn();

    // Process events so the fade-in animation is visible
    for (int i = 0; i < 50; ++i) {
        app.processEvents();
        QThread::msleep(10);
    }

    // Helper: process events for a given duration (non-blocking sleep)
    auto waitStep = [&](int ms) {
        for (int i = 0; i < ms / 10; ++i) {
            app.processEvents();
            QThread::msleep(10);
        }
    };

    // Staged progress updates
    auto splashStep = [&](const char* message, int progress, int waitMs = 100) {
        splash->setMessage(QCoreApplication::translate("main", message));
        splash->setProgress(progress);
        waitStep(waitMs);
    };

    splashStep("Initializing Core Systems...",              10);
    splashStep("Loading Configuration...",                  15);
    splashStep("Initializing Memory Manager...",            20);
    splashStep("Loading Image Processing Algorithms...",    30);
    splashStep("Initializing OpenCV Backend...",            35);
    splashStep("Loading Color Management...",               40);
    splashStep("Setting up Dark Theme...",                  50);

    // -------------------------------------------------------------------------
    // Dark theme (Fusion)
    // -------------------------------------------------------------------------

    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QPalette p = qApp->palette();
    p.setColor(QPalette::Window,          QColor(53, 53, 53));
    p.setColor(QPalette::WindowText,      Qt::white);
    p.setColor(QPalette::Base,            QColor(25, 25, 25));
    p.setColor(QPalette::AlternateBase,   QColor(53, 53, 53));
    p.setColor(QPalette::ToolTipBase,     Qt::white);
    p.setColor(QPalette::ToolTipText,     Qt::white);
    p.setColor(QPalette::Text,            Qt::white);
    p.setColor(QPalette::Button,          QColor(53, 53, 53));
    p.setColor(QPalette::ButtonText,      Qt::white);
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Link,            QColor(42, 130, 218));
    p.setColor(QPalette::Highlight,       QColor(42, 130, 218));
    p.setColor(QPalette::HighlightedText, Qt::black);
    qApp->setPalette(p);

    waitStep(100);

    splashStep("Configuring UI Colors...",     55);
    splashStep("Loading Stylesheets...",       60);

    // -------------------------------------------------------------------------
    // Global stylesheet (tooltips, scrollbars, combo boxes)
    // -------------------------------------------------------------------------

    qApp->setStyleSheet(
        // Tooltips
        "QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }"

        // Vertical scrollbar
        "QScrollBar:vertical {"
        "  border: 0px; background: #2b2b2b; width: 10px;"
        "  margin: 0; border-radius: 5px; }"
        "QScrollBar::handle:vertical {"
        "  background: #555; min-height: 20px; border-radius: 5px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"

        // Horizontal scrollbar
        "QScrollBar:horizontal {"
        "  border: 0px; background: #2b2b2b; height: 10px;"
        "  margin: 0; border-radius: 5px; }"
        "QScrollBar::handle:horizontal {"
        "  background: #555; min-width: 20px; border-radius: 5px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"

        // Combo boxes
        "QComboBox {"
        "  color: white; background-color: #353535;"
        "  border: 1px solid #555; padding: 2px; }"
        "QComboBox:hover { border: 1px solid #2a82da; }"
        "QComboBox QAbstractItemView {"
        "  background-color: #2a2a2a; color: white;"
        "  selection-background-color: #4a7ba7;"
        "  selection-color: white; outline: none; }"
        "QComboBox QAbstractItemView::item {"
        "  padding: 5px; margin: 0px; color: white; }"
        "QComboBox QAbstractItemView::item:hover {"
        "  background-color: #4a7ba7; color: white; }"
        "QComboBox QAbstractItemView::item:selected {"
        "  background-color: #4a7ba7; color: white; }"
    );

    splashStep("Applying Custom Widgets...",        65);
    splashStep("Loading Icons & Resources...",      70);
    splashStep("Initializing Tool Dialogs...",      75);
    splashStep("Constructing Main Window...",       80);

    // -------------------------------------------------------------------------
    // Main window construction
    // -------------------------------------------------------------------------

    MainWindow* window = new MainWindow();
    window->setWindowTitle("TStar v" + QString(TStar::getVersion()));
    window->resize(1280, 800);

    app.setWindowIcon(QIcon(logoPath));

    splashStep("Configuring Workspace...",  90);
    splashStep("Finalizing Setup...",       95);

    splash->setMessage(QCoreApplication::translate("main", "Ready!"));
    splash->setProgress(100);
    waitStep(150);

    splash->startFadeOut();
    window->showMaximized();

    // -------------------------------------------------------------------------
    // Restore workspace project if passed as a command-line argument
    // -------------------------------------------------------------------------

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.endsWith(".tstarproj", Qt::CaseInsensitive)) {
            QFileInfo fi(arg);
            if (fi.exists() && fi.isFile()) {
                window->loadWorkspaceProjectAtStartup(fi.absoluteFilePath());
            }
            break;
        }
    }

    // -------------------------------------------------------------------------
    // Event loop
    // -------------------------------------------------------------------------

    Logger::info("Main window displayed", "Main");
    Logger::info("Application startup complete", "Main");

    int result = 0;
    try {
        result = app.exec();
    } catch (const std::exception& e) {
        GlobalExceptionHandler::handle(e);
        result = -1;
    } catch (...) {
        GlobalExceptionHandler::handle(QString("Fatal error in main loop."));
        result = -1;
    }

    Logger::info(QString("Application exiting with code %1").arg(result), "Main");
    Logger::shutdown();

    return result;
}