#include "unixupdate.h"
#include "platform.h"
#include "scratch.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QThread>
#include <stdexcept>
#include <climits>
#ifndef Q_OS_WIN
#include <cerrno>
#include <signal.h>
#endif

bool UnixUpdate::canInstall()
{
    const auto root = Platform::installRoot();
    return !root.isEmpty() && !QFileInfo(root).isSymLink() && QFileInfo(QFileInfo(root).absolutePath()).isWritable();
}
int UnixUpdate::apply(const QStringList &args)
{
#ifdef Q_OS_WIN
    Q_UNUSED(args);
    return 1;
#else
    QString backup, target;
    bool replaced = false;
    auto fail = [](const QString &message) { throw std::runtime_error(message.toUtf8().constData()); };
    try
    {
        // The helper is the currently installed binary, not an executable from
        // the unverified download. It must only update its own installation.
        if (args.size() != 6 || !canInstall())
            fail("Invalid or unwritable portable installation.");
        target = QFileInfo(Platform::installRoot()).canonicalFilePath();
        if (target.isEmpty() || target != QFileInfo(args[3]).canonicalFilePath())
            fail("Update target does not match this application.");
        QFile archive(args[2]);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!archive.open(QIODevice::ReadOnly) || !hash.addData(&archive) ||
            hash.result().toHex() != args[5].toLatin1())
            fail("The downloaded update could not be verified.");
        archive.close();
        bool validPid = false;
        const auto pid = args[4].toLongLong(&validPid);
        if (!validPid || pid <= 0 || pid > INT_MAX || pid == QCoreApplication::applicationPid())
            fail("Invalid update process.");
        Scratch staging(QFileInfo(target).absolutePath() + "/.athanor-update-XXXXXX");
        if (!staging.isValid())
            fail("Cannot create update workspace.");
#ifdef Q_OS_MACOS
        const QString bundle = "Athanor.app";
        const QString relativeExecutable = "Contents/MacOS/Athanor";
        const QString listingTool = "/usr/bin/unzip";
        const QStringList listingArgs{"-Z", "-1", archive.fileName()};
        const QString extractTool = "/usr/bin/ditto";
        const QStringList extractArgs{"-x", "-k", archive.fileName(), staging.path()};
#else
        const QString bundle = "Athanor";
        const QString relativeExecutable = "usr/bin/Athanor";
        const QString listingTool = "/bin/tar";
        const QStringList listingArgs{"-tzf", archive.fileName()};
        const QString extractTool = "/bin/tar";
        const QStringList extractArgs{"-xzf", archive.fileName(), "-C", staging.path(), "--no-same-owner", "--no-same-permissions"};
#endif
        auto run = [&](const QString &program, const QStringList &arguments) {
            QProcess process;
            process.start(program, arguments);
            if (!process.waitForStarted() || !process.waitForFinished(60000) || process.exitCode() ||
                process.exitStatus() != QProcess::NormalExit)
                fail("Could not unpack or start the update: " + QString::fromUtf8(process.readAllStandardError()));
            return process.readAllStandardOutput();
        };
        const auto listing = run(listingTool, listingArgs);
        if (listing.isEmpty())
            fail("The update archive is empty.");
        for (auto name : QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts))
        {
            while (name.startsWith("./"))
                name.remove(0, 2);
            if (name.contains('\\') || name.split('/').contains("..") ||
                (name != bundle && !name.startsWith(bundle + '/')))
                fail("The update archive contains an unexpected path.");
        }
        run(extractTool, extractArgs);
        const auto replacement = staging.path() + '/' + bundle;
        const auto executable = replacement + '/' + relativeExecutable;
        QDirIterator entries(replacement, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
        while (entries.hasNext())
        {
            const auto path = entries.next();
            const auto canonical = QFileInfo(path).canonicalFilePath();
            if (canonical.isEmpty() || !canonical.startsWith(replacement + '/'))
                fail("The update contains an external or broken link.");
        }
        if (!QFileInfo(executable).isExecutable())
            fail("The update is missing its application executable.");
        // Launch the new runtime before touching the installation, as Windows
        // does. No conversions, settings or file manager actions run in --cli.
        run(executable, {"--cli"});
        QElapsedTimer timer;
        timer.start();
        while (::kill(pid_t(pid), 0) == 0 || errno == EPERM)
        {
            if (timer.elapsed() >= 120000)
                fail("Close all Athanor windows and try the update again.");
            QThread::msleep(50);
        }
        backup = target + ".athanor-backup-" + QFileInfo(staging.path()).fileName();
        if (QFileInfo::exists(backup) || !QDir().rename(target, backup))
            fail("Cannot save an application backup.");
        if (!QDir().rename(replacement, target))
            fail("Cannot publish the new application.");
        replaced = true;
        if (!QProcess::startDetached(target + '/' + relativeExecutable,
                                     qEnvironmentVariableIsSet("ATHANOR_TEST") ? QStringList{"--cli"} : QStringList{},
                                     QFileInfo(target).absolutePath()))
            fail("Could not restart Athanor.");
        QDir(backup).removeRecursively();
        QFile::remove(archive.fileName());
        return 0;
    }
    catch (const std::exception &e)
    {
        if (!backup.isEmpty() && QFileInfo::exists(backup))
        {
            if (replaced)
                QDir(target).removeRecursively();
            QDir().rename(backup, target);
        }
        QSaveFile error(Platform::settingsDirectory() + "/update-error.txt");
        if (error.open(QIODevice::WriteOnly))
        {
            error.write(e.what());
            error.commit();
        }
        Platform::print(QByteArray(e.what()) + '\n');
        return 1;
    }
#endif
}
