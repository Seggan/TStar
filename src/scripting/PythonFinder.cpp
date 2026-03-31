#include "PythonFinder.h"

#include <QCoreApplication>
#include <QFile>

QString findPythonExecutable() {
#if defined(Q_OS_MAC)
    QString pythonExe = QCoreApplication::applicationDirPath() + "/../Resources/python_venv/bin/python3";
    if (!QFile::exists(pythonExe))
        pythonExe = QCoreApplication::applicationDirPath() + "/../../deps/python_venv/bin/python3";
#elif defined(Q_OS_LINUX)
    QString pythonExe = QCoreApplication::applicationDirPath() + "/python_venv/bin/python3";
    if (!QFile::exists(pythonExe))
        pythonExe = QCoreApplication::applicationDirPath() + "/../deps/python_venv/bin/python3";
#elif defined(Q_OS_WIN)
    QString pythonExe = QCoreApplication::applicationDirPath() + "/python/python.exe";
    if (!QFile::exists(pythonExe))
        pythonExe = QCoreApplication::applicationDirPath() + "/../deps/python/python.exe";
#else
    #error "Unsupported platform"
#endif
    return QFile::exists(pythonExe) ? pythonExe : QString();
}
