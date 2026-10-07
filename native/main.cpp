#include <cmath>
#include "controller.h"
#include "updater.h"
#include "conversion.h"
#include "platform.h"
#include "ui_test.h"
#include "unixupdate.h"
#include <QFileOpenEvent>
#include <functional>
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPainter>
#include <QSvgRenderer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>

class MediaProvider : public QQuickImageProvider
{
  public:
    MediaProvider() : QQuickImageProvider(Image)
    {
    }
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override
    {
        QString path =
            QString::fromUtf8(QByteArray::fromBase64(id.section('?', 0, 0).toLatin1(), QByteArray::Base64UrlEncoding));
        QImage image;
        try
        {
            if (QFileInfo(path).suffix().compare("avif", Qt::CaseInsensitive) == 0 || Conversion::kind(path) == "video")
                image = Conversion::readImage(path);
            else
            {
                QImageReader reader(path);
                reader.setAutoTransform(true);
                reader.setAllocationLimit(0);
                auto sourceSize = reader.size();
                if (requested.isValid() && sourceSize.isValid())
                    reader.setScaledSize(sourceSize.scaled(requested, Qt::KeepAspectRatio));
                image = reader.read();
                if (image.isNull())
                    image = Conversion::readImage(path);
            }
            if (requested.isValid())
                image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        catch (const std::exception &)
        {
            return {};
        }
        if (size)
            *size = image.size();
        return image;
    }
};
class IconProvider : public QQuickImageProvider
{
  public:
    IconProvider() : QQuickImageProvider(Image)
    {
    }
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override
    {
        QString name = id.section('/', 0, 0), color = id.section('/', 1, 1);
        QString svg = Platform::assetPath(name + ".svg");
        QImage image;
        if (QFileInfo::exists(svg))
        {
            QSvgRenderer renderer(svg);
            image = QImage(requested.isValid() ? requested : QSize(48, 48), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            renderer.render(&painter);
        }
        else
            image.load(Platform::assetPath(name + ".png"));
        if (image.isNull())
            return {};
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (!color.isEmpty())
        {
            QPainter painter(&image);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(image.rect(), QColor('#' + color));
        }
        if (requested.isValid())
            image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (size)
            *size = image.size();
        return image;
    }
};
class Application : public QApplication
{
  public:
    using QApplication::QApplication;
    QStringList opened;
    std::function<void(const QStringList &)> filesOpened;
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen)
        {
            const auto path = static_cast<QFileOpenEvent *>(event)->file();
            if (filesOpened)
                filesOpened({path});
            else
                opened << path;
            return true;
        }
        return QApplication::event(event);
    }
};
int main(int argc, char **argv)
{
#ifndef Q_OS_WIN
    if (argc > 1 && QByteArray(argv[1]) == "--apply-update")
    {
        QCoreApplication application(argc, argv);
        application.setApplicationName("Athanor");
        return UnixUpdate::apply(application.arguments());
    }
#endif
#ifdef Q_OS_LINUX
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
        !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        for (int i = 1; i < argc; i++)
            if (QByteArray(argv[i]) == "--cli" || QByteArray(argv[i]) == "--worker")
                qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    Application application(argc, argv);
    application.setApplicationName("Athanor");
    application.setApplicationVersion("1.4.0");
    application.setQuitOnLastWindowClosed(true);
    QImageReader::setAllocationLimit(0);
    QQuickStyle::setStyle("Basic");
    const QStringList args = application.arguments();
    if (args.contains("--worker"))
    {
        int i = args.indexOf("--worker");
        return i + 1 < args.size() ? Conversion::worker(args[i + 1]) : 1;
    }
    QCommandLineParser parser;
    parser.setApplicationDescription("Athanor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("cli", "Convert from the command line", "", ""));
    parser.addOption(QCommandLineOption("quick", "Compact conversion window", "", ""));
    parser.addOption(QCommandLineOption("target", "Conversion target", "format", "auto"));
    parser.addOption(QCommandLineOption("image", "Image output", "format", "avif"));
    parser.addOption(QCommandLineOption("video", "Video output", "format", "webm"));
    parser.addOption(QCommandLineOption("audio", "Audio output", "format", "opus"));
    parser.addOption(QCommandLineOption("audio-bitrate", "Audio bitrate in kb/s", "kb/s", "96"));
    parser.addOption(QCommandLineOption("pdf-output", "PDF output: pdf or jpg pages", "format", "pdf"));
    parser.addOption(QCommandLineOption("pdf-dpi", "PDF page export resolution", "DPI", "150"));
    parser.addOption(QCommandLineOption("pdf", "PDF compression", "profile", "balanced"));
    parser.addOption(QCommandLineOption("lossless", "Lossless image/video encoding", "", ""));
    parser.addOption(QCommandLineOption("quality", "Image quality", "1-100", "75"));
    parser.addOption(QCommandLineOption("crf", "Video CRF", "0-63", "32"));
    parser.addOption(QCommandLineOption("speed", "Encoding speed", "preset", "Smallest"));
    parser.addOption(QCommandLineOption("acceleration", "Video acceleration", "mode", "Auto"));
    parser.addOption(QCommandLineOption("threads", "Encoder threads", "count", "0"));
    parser.addOption(QCommandLineOption("target-size", "Maximum output size in MB", "MB"));
    parser.addOption(QCommandLineOption("output", "Output folder", "path", ""));
    parser.addOption(QCommandLineOption("delete-originals", "Delete originals after validated conversion", "", ""));
    parser.addOption(QCommandLineOption("include-subfolders", "Include subfolders", "", ""));
    parser.addPositionalArgument("files", "Input files", "[files...]");
    QCommandLineOption qmlCheck("qml-check");
    qmlCheck.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(qmlCheck);
    QCommandLineOption platformTest("platform-test");
    platformTest.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(platformTest);
    QCommandLineOption testOption("self-test", QString(), "folder");
    testOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(testOption);
    QCommandLineOption controllerTest("controller-test", QString(), "folder");
    controllerTest.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(controllerTest);
    QCommandLineOption queueTest("queue-test", QString(), "fixtures");
    queueTest.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(queueTest);
    QCommandLineOption formatQueueTest("format-queue-test", QString(), "fixtures");
    formatQueueTest.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(formatQueueTest);
    QCommandLineOption schedulingTest("scheduling-test", QString(), "fixtures");
    schedulingTest.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(schedulingTest);
    QCommandLineOption updateTest("update-test", QString(), "folder");
    updateTest.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(updateTest);
    parser.process(application);
    if (parser.isSet("platform-test"))
    {
        extern int runPlatformTest();
        return runPlatformTest();
    }
    if (parser.isSet("cli"))
    {
        Options opts;
        opts.image = parser.value("image");
        opts.video = parser.value("video");
        opts.audio = parser.value("audio");
        opts.audioBitrate = qBound(6, parser.value("audio-bitrate").toInt(), 320);
        opts.pdfOutput = parser.value("pdf-output");
        opts.pdfDpi = qBound(36, parser.value("pdf-dpi").toInt(), 600);
        opts.pdf = parser.value("pdf");
        opts.quality = qBound(1, parser.value("quality").toInt(), 100);
        opts.crf = qBound(0, parser.value("crf").toInt(), 63);
        opts.speed = parser.value("speed");
        opts.acceleration = parser.value("acceleration");
        opts.threads = parser.value("threads").toInt();
        opts.output = parser.value("output");
        opts.deleteOriginal = parser.isSet("delete-originals");
        opts.imageLossless = opts.videoLossless = parser.isSet("lossless");
        if (parser.isSet("lossless"))
            opts.pdf = "lossless";
        if (parser.isSet("target-size"))
        {
            bool valid = false;
            double mb = parser.value("target-size").toDouble(&valid);
            if (!valid || !std::isfinite(mb) || mb <= 0 || mb > 1000000000)
            {
                Conversion::writeLine({{"ok", false}, {"error", "Enter a positive target size in MB"}});
                return 1;
            }
            opts.sizeMode = true;
            opts.targetBytes = qint64(std::round(mb * 1000000));
        }
        QString target = parser.value("target");
        if (QStringList{"avif", "webp", "heic", "heif", "jpg", "png", "ico"}.contains(target))
            opts.image = target;
        if (QStringList{"webm", "mkv", "av1", "mp4", "gif", "opus", "mp3", "wav"}.contains(target))
            opts.video = target;
        if (QStringList{"opus", "mp3", "wav"}.contains(target))
            opts.audio = target;
        QJsonArray pdfSources;
        if (opts.image == "pdf")
            for (const auto &path : parser.positionalArguments())
                if (Conversion::kind(path) == "image")
                    pdfSources.append(QFileInfo(path).absoluteFilePath());
        bool pdfDone = false;
        int errors = 0;
        for (const auto &input : parser.positionalArguments())
        {
            if (opts.image == "pdf" && Conversion::kind(input) == "image")
            {
                if (pdfDone)
                    continue;
                pdfDone = true;
            }
            try
            {
                auto result = Conversion::run(
                    {{"source", QFileInfo(input).absoluteFilePath()},
                     {"sources", pdfSources},
                     {"options", opts.json()}},
                    [](int p, const QString &s) { Conversion::writeLine({{"percent", p}, {"stage", s}}); });
                Conversion::writeLine(result);
            }
            catch (const std::exception &e)
            {
                Conversion::writeLine({{"ok", false}, {"source", input}, {"error", QString::fromUtf8(e.what())}});
                errors++;
            }
        }
        return errors ? 1 : 0;
    }
    if (parser.isSet("qml-check") || parser.isSet("self-test") || parser.isSet("controller-test") || parser.isSet("queue-test") ||
        parser.isSet("format-queue-test") || parser.isSet("update-test"))
        qputenv("ATHANOR_TEST", "1");
    bool quick = parser.isSet("quick") || parser.value("target") != "auto";
    Controller controller(quick);
    controller.setTarget(parser.value("target"));
    controller.addPaths(parser.positionalArguments() + application.opened, parser.isSet("include-subfolders"));
    application.filesOpened = [&controller](const QStringList &paths) { controller.addPaths(paths); };
    if (parser.isSet("update-test"))
    {
        qputenv("ATHANOR_TEST", "1");
        Updater updater(&controller);
        bool downloading = false;
        QObject::connect(&updater, &Updater::changed, &application, [&] {
            if (updater.working())
                return;
            if (!downloading && qEnvironmentVariableIsSet("ATHANOR_TEST_UPDATE_INSTALL") && updater.canInstall())
            {
                downloading = true;
                updater.install();
                return;
            }
            Conversion::writeLine({{"status", updater.status()},
                                   {"version", updater.availableVersion()},
                                   {"can_install", updater.canInstall()},
                                   {"downloaded", downloading}});
            application.quit();
        });
        QTimer::singleShot(0, &updater, &Updater::check);
        QTimer::singleShot(65000, &application, [] { QCoreApplication::exit(3); });
        return application.exec();
    }
    if (parser.isSet("scheduling-test"))
    {
        runSchedulingTest(&controller, parser.value("scheduling-test"));
        return application.exec();
    }
    if (parser.isSet("format-queue-test"))
    {
        runFormatQueueTest(&controller, parser.value("format-queue-test"));
        return application.exec();
    }
    if (parser.isSet("queue-test"))
    {
        runQueueReuseTest(&controller, parser.value("queue-test"));
        return application.exec();
    }
    if (parser.isSet("controller-test"))
    {
        runControllerTest(&controller, parser.value("controller-test"));
        return application.exec();
    }
    Updater updater(&controller);
    QQmlApplicationEngine engine;
    engine.addImageProvider("media", new MediaProvider);
    engine.addImageProvider("icons", new IconProvider);
    engine.rootContext()->setContextProperty("backend", &controller);
    engine.rootContext()->setContextProperty("updater", &updater);
    engine.rootContext()->setContextProperty("nativePlatform", Platform::name());
    application.setWindowIcon(QIcon(Platform::assetPath("athanor.ico")));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &application, [] { QCoreApplication::exit(2); },
        Qt::QueuedConnection);
    if (parser.isSet("self-test"))
        QObject::connect(&engine, &QQmlEngine::warnings, &application, [](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                Conversion::writeLine({{"ui_warning", error.toString()}});
        });
    engine.loadFromModule("Athanor", quick ? "Quick" : "Main");
    if (parser.isSet("qml-check"))
        return engine.rootObjects().isEmpty() ? 2 : 0;
    if (parser.isSet("self-test") && qEnvironmentVariableIsSet("ATHANOR_TEST_UPDATE_API"))
        QTimer::singleShot(0, &updater, &Updater::check);
    if (parser.isSet("self-test"))
        runUiTest(&engine, &controller, parser.value("self-test"), quick);
    return application.exec();
}
