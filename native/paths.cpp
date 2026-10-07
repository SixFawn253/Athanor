#include "platform.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>

QString Platform::name()
{
#ifdef Q_OS_WIN
    return "Windows";
#elif defined(Q_OS_MACOS)
    return "macOS";
#else
    return "Linux";
#endif
}
QString Platform::settingsDirectory()
{
    const auto override = qEnvironmentVariable("ATHANOR_SETTINGS_DIR");
    if (!override.isEmpty())
        return override;
#ifdef Q_OS_WIN
    // Preserve existing unpacked Windows settings and portable launcher overrides.
    return QCoreApplication::applicationDirPath();
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif
}
QString Platform::scratchPattern(const QString &prefix)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(prefix + "-XXXXXX");
}
QString Platform::assetPath(const QString &name)
{
    return ":/assets/" + name;
}
QString Platform::toolPath(const QString &name)
{
    QString executable = name;
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    QStringList folders;
    const auto override = qEnvironmentVariable("ATHANOR_TOOLS_DIR");
    if (!override.isEmpty())
        folders << override;
    folders << QCoreApplication::applicationDirPath() + "/tools" << QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    folders << QCoreApplication::applicationDirPath() + "/../Resources/tools";
#elif defined(Q_OS_LINUX)
    folders << QCoreApplication::applicationDirPath() + "/../lib/athanor/tools";
#endif
    for (const auto &folder : folders)
    {
        const auto path = QDir(folder).filePath(executable);
        if (QFileInfo(path).isFile() && QFileInfo(path).isExecutable())
            return path;
    }
#ifndef Q_OS_WIN
    // Distro builds use their package manager's codecs; portable builds bundle tools.
    if (override.isEmpty())
        return QStandardPaths::findExecutable(executable);
#endif
    return {};
}
QString Platform::installRoot()
{
#ifdef Q_OS_MACOS
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.dirName() != "MacOS" || !dir.cdUp() || dir.dirName() != "Contents" || !dir.cdUp() ||
        !dir.dirName().endsWith(".app"))
        return {};
    return dir.absolutePath();
#elif defined(Q_OS_LINUX)
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.dirName() != "bin" || !dir.cdUp())
        return {};
    if (dir.dirName() == "usr")
        dir.cdUp();
    if (!QFileInfo::exists(dir.filePath(".athanor-portable")))
        return {};
    return dir.absolutePath();
#else
    return qEnvironmentVariable("ATHANOR_LAUNCHER_PATH");
#endif
}
QString Platform::releaseAsset(const QString &version)
{
    QString arch = QSysInfo::buildCpuArchitecture();
    if (arch == "x86_64")
        arch = "x64";
    else if (arch == "aarch64")
        arch = "arm64";
#ifdef Q_OS_WIN
    return "Athanor-Alpha-" + version + "-Windows-" + arch + ".exe";
#elif defined(Q_OS_MACOS)
    return "Athanor-Alpha-" + version + "-macOS-" + arch + ".zip";
#else
    return "Athanor-Alpha-" + version + "-Linux-" + arch + ".tar.gz";
#endif
}
