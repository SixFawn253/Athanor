#include "platform.h"
#include "desktopintegration.h"
#include "conversion.h"
#include "hardwareload.h"
#include "scheduling.h"
#include <QThread>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QJsonArray>
#include <QTemporaryDir>

int runPlatformTest()
{
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    QStringList errors;
    auto check = [&](bool ok, const QString &message) { if (!ok) errors << message; };
    auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    HardwareMonitor monitor;
    auto load = monitor.sample();
    check(load.totalMemory > 0 && load.availableMemory <= load.totalMemory, "Hardware memory sampling failed");
    QThread::msleep(30);
    load = monitor.sample();
    check(load.cpu >= 0 && load.cpu <= 1, "Hardware CPU sampling failed");
    check(!load.gpuEncoding && load.gpu == -1, "CPU-only work sampled GPU load");
    check(AdaptiveWorkers::ceiling(1, 0) == 1 && AdaptiveWorkers::ceiling(64, 0) == 8 &&
          AdaptiveWorkers::ceiling(8, 4) == 2, "CPU thread budget did not constrain parallelism");
    HardwareLoad idle;
    idle.cpu = .2;
    idle.totalMemory = 16ULL * 1024 * 1024 * 1024;
    idle.availableMemory = 8ULL * 1024 * 1024 * 1024;
    int samples = 0, workers = 2;
    workers = AdaptiveWorkers::adjust(workers, 8, idle, samples);
    workers = AdaptiveWorkers::adjust(workers, 8, idle, samples);
    check(workers == 2, "Adaptive scheduling increased without sustained headroom");
    workers = AdaptiveWorkers::adjust(workers, 8, idle, samples);
    check(workers == 3, "Adaptive scheduling did not expand under sustained headroom");
    idle.cpu = .98;
    check(AdaptiveWorkers::adjust(3, 8, idle, samples) == 2, "CPU saturation did not reduce launches");
    idle.cpu = .2;
    idle.availableMemory = 100 * 1024 * 1024;
    check(AdaptiveWorkers::adjust(3, 8, idle, samples) == 2, "Memory pressure did not reduce launches");
    idle.availableMemory = 8ULL * 1024 * 1024 * 1024;
    idle.gpu = .99;
    idle.gpuEncoding = false;
    check(AdaptiveWorkers::adjust(3, 8, idle, samples) == 3, "Inactive GPU load affected CPU-only jobs");
    idle.gpuEncoding = true;
    check(AdaptiveWorkers::adjust(3, 8, idle, samples) == 2, "Active GPU saturation did not reduce launches");
    idle.cpu = -1; idle.gpu = -1; samples = 0;
    for (int i = 0; i < 5; ++i)
        workers = AdaptiveWorkers::adjust(workers, 8, idle, samples);
    check(workers == 3, "Unknown hardware telemetry caused uncontrolled growth");
    check(Scheduling::shortestJob({}) == -1 && Scheduling::shortestJob({{0, 50}, {1, 5}, {2, 20}}) == 1 &&
          Scheduling::shortestJob({{2, 5}, {1, 5}}) == 1, "Shortest-job-first selection or stable ties failed");
    Options estimates;
    check(Scheduling::work("video", 1000, 1920, 1080, 10, estimates) <
          Scheduling::work("video", 1000, 1920, 1080, 100, estimates), "Media duration was ignored in SJF estimates");
    const auto root = QFileInfo(temporary.path()).canonicalFilePath();
    qputenv("ATHANOR_SETTINGS_DIR", (root + "/settings").toUtf8());
    check(Platform::settingsDirectory() == root + "/settings", "Settings override lost");
    check(QFileInfo::exists(Platform::assetPath("check.svg")), "Embedded icons missing");
    check(write(root + "/source", "new") && write(root + "/destination", "old"), "Cannot create publication fixtures");
    check(!Platform::publish(root + "/source", root + "/destination"), "Publication overwrote an existing file");
    check(QFileInfo::exists(root + "/source"), "Failed publication consumed source");
    check(Platform::publish(root + "/source", root + "/new-file"), "File publication failed");
    check(!QFileInfo::exists(root + "/source"), "Publication left staged source");
    QDir().mkpath(root + "/pages");
    write(root + "/pages/page-0001.jpg", "page");
    check(Platform::publish(root + "/pages", root + "/export"), "Directory publication failed");
    check(QFileInfo::exists(root + "/export/page-0001.jpg"), "Published PDF pages missing");
    QDir().mkpath(root + "/other-pages");
    check(!Platform::publish(root + "/other-pages", root + "/export"), "Directory collision overwrote results");
    const auto asset = Platform::releaseAsset("9.9");
    check(asset.contains(Platform::name()) && asset.startsWith("Athanor-Alpha-9.9-"), "Wrong platform release name");
#ifndef Q_OS_WIN
    qputenv("ATHANOR_INTEGRATION_DIR", (root + "/actions").toUtf8());
    QString error;
    check(DesktopIntegration::configure(true, "/bin/echo", &error), "Action installation failed: " + error);
    QDirIterator actions(root + "/actions", QDir::Files, QDirIterator::Subdirectories);
    int count = 0;
    while (actions.hasNext())
    {
        const auto path = actions.next();
        count++;
#ifdef Q_OS_MACOS
        QProcess lint;
        lint.start("/usr/bin/plutil", {"-lint", path});
        check(lint.waitForFinished() && lint.exitCode() == 0, "Invalid Finder workflow: " + path);
#else
        if (!path.endsWith(".desktop"))
        {
            QProcess lint;
            lint.start("/bin/bash", {"-n", path});
            check(lint.waitForFinished() && lint.exitCode() == 0, "Invalid file manager script: " + path);
        }
#endif
    }
    check(count >= 21, "Missing file manager conversion actions");
#ifdef Q_OS_MACOS
    const auto file = root + "/foto à ' test.png";
    write(file, "test");
    QProcess automator;
    automator.start("/usr/bin/automator", {"-i", file, root + "/actions/Athanor - Images - Convert to PNG.workflow"});
    const bool done = automator.waitForFinished(30000);
    const auto stdoutBytes = automator.readAllStandardOutput();
    const auto stderrBytes = automator.readAllStandardError();
    check(done && automator.exitCode() == 0 && QString::fromUtf8(stdoutBytes).normalized(QString::NormalizationForm_C).contains(file.normalized(QString::NormalizationForm_C)),
          "Finder workflow did not pass selected path intact: " + QString::fromUtf8(stdoutBytes + stderrBytes));
    const auto gifWorkflow = root + "/actions/Athanor - GIF - Convert to WebM.workflow";
    QFile gifInfo(gifWorkflow + "/Contents/Info.plist");
    check(gifInfo.open(QIODevice::ReadOnly) && gifInfo.readAll().contains("com.compuserve.gif"),
          "Finder video targets are not registered for the GIF image UTI");
    const auto gif = root + "/animation.gif";
    write(gif, "test");
    automator.start("/usr/bin/automator", {"-i", gif, gifWorkflow});
    check(automator.waitForFinished(30000) && automator.exitCode() == 0 &&
          QString::fromUtf8(automator.readAllStandardOutput()).contains("--target webm -- " + gif),
          "Finder GIF action did not select the existing video conversion target");
#endif
    check(DesktopIntegration::configure(false, "/bin/echo", &error), "Action removal failed: " + error);
    QDirIterator remaining(root + "/actions", QDir::Files, QDirIterator::Subdirectories);
    check(!remaining.hasNext(), "Disabling integration left generated actions");
#endif
    QJsonArray list;
    for (const auto &error : errors)
        list.append(error);
    Conversion::writeLine({{"ok", errors.isEmpty()}, {"errors", list}});
    return errors.isEmpty() ? 0 : 1;
}
