#include "platform.h"
#include "conversion.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QSettings>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#else
#include <signal.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif
void Platform::setupProcess(ChildProcess *process, bool workerGroup)
{
    process->setGroup(workerGroup);
}
void Platform::cancelProcess(ChildProcess *process)
{
    process->kill();
}
bool Platform::publish(const QString &from, const QString &to)
{
#ifdef Q_OS_WIN
    return MoveFileExW(reinterpret_cast<LPCWSTR>(from.utf16()), reinterpret_cast<LPCWSTR>(to.utf16()), 0) != 0;
#elif defined(Q_OS_LINUX)
    return syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(from).constData(), AT_FDCWD,
                   QFile::encodeName(to).constData(), RENAME_NOREPLACE) == 0;
#else
    if (link(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0)
    {
        unlink(QFile::encodeName(from).constData());
        return true;
    }
    return false;
#endif
}
bool Platform::systemDark()
{
#ifdef Q_OS_WIN
    QSettings theme("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    QSettings::NativeFormat);
    return theme.value("AppsUseLightTheme", 1).toInt() == 0;
#else
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#endif
}
bool Platform::contextMenuEnabled(const QString &executable)
{
#ifdef Q_OS_WIN
    QSettings registry(
        "HKEY_CURRENT_USER\\Software\\Classes\\SystemFileAssociations\\.webp\\shell\\Athanor\\shell\\avif\\command",
        QSettings::NativeFormat);
    return registry.value(".").toString().contains(QDir::toNativeSeparators(executable), Qt::CaseInsensitive);
#else
    Q_UNUSED(executable);
    return false;
#endif
}
bool Platform::contextMenu(bool enabled, const QString &executable, QString *error)
{
#ifdef Q_OS_WIN
    const QString prefix = "HKEY_CURRENT_USER\\Software\\Classes\\SystemFileAssociations\\.";
    const QStringList images = Conversion::imageExtensions(), videos = Conversion::videoExtensions(),
                      audios = Conversion::audioExtensions();
    auto install = [&](const QString &extension, const QStringList &targets, const QStringList &labels) {
        QSettings root(prefix + extension + "\\shell\\Athanor", QSettings::NativeFormat);
        root.remove("");
        if (!enabled)
        {
            root.sync();
            return root.status() == QSettings::NoError;
        }
        root.setValue("MUIVerb", "Convert using Athanor");
        root.setValue("SubCommands", "");
        root.setValue("Icon", QDir::toNativeSeparators(executable));
        for (int i = 0; i < targets.size(); i++)
        {
            QString key = "shell/" + targets[i];
            root.setValue(key + "/MUIVerb", labels[i]);
            root.setValue(key + "/MultiSelectModel", "Player");
            root.setValue(key + "/command/.",
                          '"' + QDir::toNativeSeparators(executable) + "\" --quick --target " + targets[i] + " \"%1\"");
        }
        root.sync();
        return root.status() == QSettings::NoError;
    };
    bool ok = true;
    for (auto e : images)
        ok = install(e, {"avif", "webp", "jpg", "png", "ico", "images-pdf"},
                     {"Convert to AVIF", "Convert to WebP", "Convert to JPG", "Convert to PNG", "Convert to ICO",
                      "Create PDF"}) &&
             ok;
    for (auto e : videos)
        ok = install(e, {"webm", "mkv", "av1", "mp4", "gif", "opus", "mp3", "wav"},
                     {"Convert to WebM", "Convert to MKV", "Convert to .av1 (video only)", "Convert to MP4",
                      "Convert to GIF", "Extract Opus audio", "Extract MP3 audio", "Extract WAV audio"}) &&
             ok;
    for (auto e : audios)
        ok = install(e, {"opus", "mp3", "wav"}, {"Convert to Opus", "Convert to MP3", "Convert to WAV"}) && ok;
    ok = install("pdf", {"pdf", "pdf-jpg"}, {"Compress PDF", "Export JPG pages"}) && ok;
    if (!ok && error)
        *error = "Windows could not save the context menu settings";
    return ok;
#else
    Q_UNUSED(enabled);
    Q_UNUSED(executable);
    if (error)
        *error = "File manager integration is configured by the Linux package";
    return true;
#endif
}
void Platform::notify(const QString &title, const QString &message)
{
    static QSystemTrayIcon *tray = nullptr;
    if (!tray)
    {
        tray = new QSystemTrayIcon(QIcon(QCoreApplication::applicationDirPath() + "/assets/athanor.ico"), qApp);
        tray->setToolTip("Athanor");
        tray->show();
    }
    tray->showMessage(title, message, QSystemTrayIcon::Information, 5000);
}
void Platform::print(const QByteArray &bytes)
{
#ifdef Q_OS_WIN
    DWORD written = 0;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE && output)
        WriteFile(output, bytes.constData(), DWORD(bytes.size()), &written, nullptr);
#else
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    fflush(stdout);
#endif
}

bool Platform::animationsEnabled()
{
#ifdef Q_OS_WIN
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    return enabled;
#else
    QSettings desktop("org.gnome.desktop.interface", QSettings::NativeFormat);
    return desktop.value("enable-animations", true).toBool();
#endif
}
