#include "controller.h"
#include "platform.h"
#include "scheduling.h"
#include <QCoreApplication>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QDir>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDesktopServices>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QImageReader>
#include <QRegularExpression>
#include <QThread>
#include <cmath>

static QString sizeText(qint64 bytes)
{
    if (bytes < 0)
        return QString::fromUtf8("−") + sizeText(-bytes);
    if (bytes < 1000)
        return QString::number(bytes) + " B";
    double value = bytes / 1000.0;
    QStringList units = {"KB", "MB", "GB", "TB"};
    int unit = 0;
    while (value >= 1000 && unit < units.size() - 1)
    {
        value /= 1000;
        unit++;
    }
    return QString::number(value, 'f', value < 10 ? 2 : 1) + ' ' + units[unit];
}
QHash<int, QByteArray> QueueModel::roleNames() const
{
    return {{Source, "sourcePath"},
            {Name, "fileName"},
            {Category, "category"},
            {Status, "fileStatus"},
            {ProgressRole, "fileProgress"},
            {Output, "outputPath"},
            {Warning, "warning"},
            {Width, "imageWidth"},
            {Height, "imageHeight"},
            {FileSize, "fileSize"},
            {CompressedSize, "compressedSize"},
            {Smaller, "percentSmaller"},
            {Gained, "gainedSpace"},
            {FolderGroup, "folderGroup"},
            {FolderSection, "folderSection"}};
}
QVariant QueueModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.row() < 0 || i.row() >= items.size())
        return {};
    const auto &r = items[i.row()];
    switch (role)
    {
    case FolderGroup:
        return r.groupRoot;
    case FolderSection:
        return r.groupRoot.isEmpty()
                   ? QString()
                   : QString::fromUtf8(
                         QJsonDocument(
                             QJsonArray{r.groupRoot,
                                        QDir(r.groupRoot).relativeFilePath(QFileInfo(r.source).absolutePath())})
                             .toJson(QJsonDocument::Compact));
    case Source:
        return r.source;
    case Name:
        return r.groupRoot.isEmpty() ? QFileInfo(r.source).fileName() : QDir(r.groupRoot).relativeFilePath(r.source);
    case Category:
        return r.category;
    case Status:
        return r.status;
    case ProgressRole:
        return r.progress;
    case Output:
        return r.output;
    case Warning:
        return r.warning;
    case Width:
        return r.width;
    case Height:
        return r.height;
    case FileSize:
        return sizeText(r.before);
    case CompressedSize:
        return r.after < 0 ? QString::fromUtf8("—") : sizeText(r.after);
    case Smaller:
        return r.after < 0 || r.before <= 0 ? QString::fromUtf8("—")
                                            : QString::number(100.0 * (r.before - r.after) / r.before, 'f', 1) + "%";
    case Gained:
        return r.after < 0 ? QString::fromUtf8("—") : sizeText(r.before - r.after);
    default:
        return {};
    }
}
void QueueModel::add(const QVector<QueueItem> &rows)
{
    if (rows.isEmpty())
        return;
    int first = items.size();
    beginInsertRows({}, first, first + rows.size() - 1);
    items += rows;
    endInsertRows();
}
void QueueModel::update(int row)
{
    if (row >= 0 && row < items.size())
        emit dataChanged(index(row), index(row));
}
void QueueModel::remove(int row)
{
    if (row < 0 || row >= items.size())
        return;
    beginRemoveRows({}, row, row);
    items.removeAt(row);
    endRemoveRows();
}
void QueueModel::clear()
{
    beginResetModel();
    items.clear();
    endResetModel();
}
Controller::Controller(bool quick, QObject *parent) : QObject(parent), compact(quick)
{
    revealDeadline.setSingleShot(true);
    connect(&revealDeadline, &QTimer::timeout, this, &Controller::transitionExpired);
    QString settingsDirectory = Platform::settingsDirectory();
    QDir().mkpath(settingsDirectory);
    settingsPath = settingsDirectory + "/settings.json";
    QFile file(settingsPath);
    if (file.open(QIODevice::ReadOnly))
    {
        auto settings = QJsonDocument::fromJson(file.readAll()).object();
        opts = Options::fromJson(settings);
        if (!settings.contains("convert_only") && (opts.imageLossless || opts.videoLossless))
            opts.convertOnly = !opts.sizeMode;
        opts.imageLossless = false;
        opts.videoLossless = false;
        opts.deleteOriginal = false;
        opts.output.clear();
        theme = settings.value("appearance").toString("System");
        contexts = settings.value("context_menu").toBool(true);
        motionPreference = settings.value("motion").toString("System");
    }
    systemTheme = Platform::systemDark();
    systemMotion = Platform::animationsEnabled();
    systemTimer.setInterval(1000);
    connect(&systemTimer, &QTimer::timeout, this, [this] {
        bool value = Platform::systemDark();
        bool motion = Platform::animationsEnabled();
        if (motion != systemMotion)
        {
            systemMotion = motion;
            if (motionPreference == "System")
                emit changed();
        }
        if (value != systemTheme)
        {
            systemTheme = value;
            if (theme == "System")
                emit themeChanged();
        }
    });
    systemTimer.start();
    batchTimer.setInterval(2000);
    connect(&batchTimer, &QTimer::timeout, this, &Controller::updateBatchLoad);
    if (!qEnvironmentVariableIsSet("ATHANOR_TEST"))
        QTimer::singleShot(400, this, [this] {
            QString error;
            if (!Platform::contextMenu(
                    contexts, qEnvironmentVariable("ATHANOR_LAUNCHER_PATH", QCoreApplication::applicationFilePath()),
                    &error))
            {
                message = error;
                emit changed();
            }
        });
}
static QString conversionKey(const QueueItem &item, const Options &options)
{
    QJsonObject key;
    bool image = item.category == "image", video = item.category == "video";
    key["convert_only"] = options.convertOnly;
    key["format"] = image                      ? options.image
                    : video                    ? options.video
                    : item.category == "audio" ? options.audio
                                               : options.pdfOutput;
    key["audio_bitrate"] =
        item.category == "audio" || (video && QStringList{"opus", "mp3", "wav"}.contains(options.video))
            ? options.audioBitrate
            : 0;
    key["pdf_dpi"] = item.category == "pdf" && options.pdfOutput == "jpg" ? options.pdfDpi : 0;
    key["lossless"] = !options.sizeMode && (image   ? options.imageLossless
                                            : video ? options.videoLossless
                                                    : options.pdf == "lossless");
    key["quality"] = image ? options.quality : video ? options.crf : 0;
    key["pdf"] = item.category == "pdf" ? options.pdf : QString();
    key["speed"] = options.speed;
    key["size_mode"] = options.sizeMode;
    key["target_bytes"] = options.sizeMode ? options.targetBytes : 0;
    return QString::fromUtf8(QJsonDocument(key).toJson(QJsonDocument::Compact));
}
bool Controller::needsConversion(const QueueItem &row) const
{
    return row.status != "Done" || row.completedKey != conversionKey(row, opts);
}
bool Controller::canConvert() const
{
    return !busy() && std::any_of(model.items.cbegin(), model.items.cend(),
                                  [this](const QueueItem &row) { return needsConversion(row); });
}
static QString rowInput(const QueueItem &row)
{
    return QFileInfo::exists(row.source) ? row.source : QFileInfo::exists(row.output) ? row.output : row.source;
}
bool Controller::hasImage() const
{
    return std::any_of(model.items.cbegin(), model.items.cend(),
                       [](const QueueItem &i) { return i.category == "image" && QFileInfo::exists(rowInput(i)); });
}
bool Controller::hasVideo() const
{
    return std::any_of(model.items.cbegin(), model.items.cend(),
                       [](const QueueItem &i) { return i.category == "video" && QFileInfo::exists(rowInput(i)); });
}
bool Controller::oversizedWebp() const
{
    return opts.image == "webp" && std::any_of(model.items.cbegin(), model.items.cend(), [](const QueueItem &i) {
               return i.category == "image" && (i.width > 16383 || i.height > 16383);
           });
}
bool Controller::dark() const
{
    return theme == "Dark" || (theme == "System" && systemTheme);
}
void Controller::setAppearance(const QString &value)
{
    if (value != "System" && value != "Dark" && value != "Light")
        return;
    theme = value;
    save();
    emit themeChanged();
}
void Controller::setContextMenus(bool enabled)
{
    QString error;
    if (qEnvironmentVariableIsSet("ATHANOR_TEST") ||
        Platform::contextMenu(
            enabled, qEnvironmentVariable("ATHANOR_LAUNCHER_PATH", QCoreApplication::applicationFilePath()), &error))
    {
        contexts = enabled;
        save();
    }
    else
        message = error;
    emit changed();
}
void Controller::save()
{
    auto settings = opts.json();
    settings.remove("delete");
    settings.remove("output");
    settings["appearance"] = theme;
    settings["context_menu"] = contexts;
    settings["motion"] = motionPreference;
    QSaveFile file(settingsPath);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(settings).toJson());
        file.commit();
    }
}
void Controller::setOption(const QString &key, const QVariant &value)
{
    auto j = opts.json();
    if (key == "mode")
    {
        j["size_mode"] = value.toString() == "target";
        j["convert_only"] = value.toString() == "convert";
        if (value.toString() == "quality")
        {
            j["image_lossless"] = false;
            j["video_lossless"] = false;
        }
    }
    else
        j[key] = QJsonValue::fromVariant(value);
    opts = Options::fromJson(j);
    if (key != "output")
        save();
    emit changed();
}
void Controller::setTarget(const QString &target)
{
    if (QStringList{"avif", "webp", "heic", "heif", "jpg", "png", "ico"}.contains(target))
        opts.image = target;
    else if (QStringList{"webm", "mkv", "av1", "mp4", "gif"}.contains(target))
        opts.video = target;
    else if (QStringList{"opus", "mp3", "wav"}.contains(target))
    {
        opts.audio = target;
        opts.video = target;
    }
    else if (target == "images-pdf")
        opts.image = "pdf";
    else if (target == "pdf-jpg")
        opts.pdfOutput = "jpg";
    emit changed();
}
void Controller::addFiles()
{
    addPaths(QFileDialog::getOpenFileNames(nullptr, "Select images, audio, videos or PDFs", {},
                                           Conversion::supportedFilter()));
}
void Controller::addFolder(bool recursive)
{
    auto folder = QFileDialog::getExistingDirectory(nullptr, "Select folder");
    if (!folder.isEmpty())
        addPaths({folder}, recursive);
}
void Controller::addUrls(const QList<QUrl> &urls, bool recursive)
{
    QStringList paths;
    for (const auto &url : urls)
        if (url.isLocalFile())
            paths << url.toLocalFile();
    addPaths(paths, recursive);
}
QString Controller::version() const
{
    return QCoreApplication::applicationVersion();
}
int Controller::groupCount(const QString &root) const
{
    return int(std::count_if(model.items.cbegin(), model.items.cend(),
                             [&](const QueueItem &item) { return item.groupRoot == root; }));
}
QStringList Controller::queueCategories() const
{
    QStringList result;
    for (const auto &item : model.items)
        if (!result.contains(item.category))
            result.append(item.category);
    return result;
}
bool Controller::hasFolders() const
{
    return std::any_of(model.items.cbegin(), model.items.cend(),
                       [](const QueueItem &item) { return !item.groupRoot.isEmpty(); });
}
QJsonObject Controller::folderSectionInfo(const QString &key) const
{
    const auto parts = QJsonDocument::fromJson(key.toUtf8()).array();
    if (parts.size() != 2)
        return {};
    const QString root = parts[0].toString(), relative = parts[1].toString();
    QString label = QFileInfo(root).fileName();
    if (label.isEmpty())
        label = QDir::toNativeSeparators(root);
    if (relative != ".")
        label += " / " + relative;
    int files = 0;
    for (const auto &item : model.items)
        if (item.groupRoot == root && QDir(root).relativeFilePath(QFileInfo(item.source).absolutePath()) == relative)
            ++files;
    return {{"label", label},
            {"path", QDir::cleanPath(QDir(root).filePath(relative))},
            {"count", files},
            {"depth", relative == "." ? 0 : relative.count('/') + 1}};
}
void Controller::addPaths(const QStringList &paths, bool recursive)
{
    if (paths.isEmpty())
        return;
    int generation = queueGeneration;
    auto *watcher = new QFutureWatcher<QVector<QueueItem>>(this);
    connect(watcher, &QFutureWatcher<QVector<QueueItem>>::finished, this, [this, watcher, generation] {
        if (generation != queueGeneration)
        {
            watcher->deleteLater();
            return;
        }
        QVector<QueueItem> added;
        for (const auto &r : watcher->result())
        {
            QString key = QDir::cleanPath(r.source);
#ifdef Q_OS_WIN
            key = key.toLower();
#endif
            if (!seen.contains(key))
            {
                seen.insert(key);
                added.append(r);
            }
        }
        if (running)
            for (int i = 0; i < added.size(); ++i)
                batchRows.insert(count() + i);
        model.add(added);
        watcher->deleteLater();
        emit changed();
        if (!added.isEmpty())
            emit filesAdded();
        if (running && !cancelling)
            schedule();
    });
    watcher->setFuture(QtConcurrent::run([paths, recursive] {
        QVector<QueueItem> rows;
        QVector<QPair<QString, QString>> candidates;
        for (const auto &path : paths)
        {
            QFileInfo info(path);
            if (info.isDir())
            {
                QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot,
                                recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
                while (it.hasNext())
                    candidates.append({it.next(), QDir::cleanPath(info.absoluteFilePath())});
            }
            else
                candidates.append({info.absoluteFilePath(), QString()});
        }
        for (const auto &candidate : candidates)
        {
            const auto &path = candidate.first;
            QString category = Conversion::kind(path);
            if (category.isEmpty() || !QFileInfo(path).isFile())
                continue;
            QueueItem item;
            item.source = QFileInfo(path).absoluteFilePath();
            item.groupRoot = candidate.second;
            item.category = category;
            item.before = QFileInfo(path).size();
            if (category == "image")
            {
                QImageReader reader(path);
                auto size = reader.size();
                item.width = size.width();
                item.height = size.height();
            }
            if (category == "video" || category == "audio")
            {
                const auto ffprobe = Platform::toolPath("ffprobe");
                if (!ffprobe.isEmpty())
                {
                    ChildProcess probe;
                    Platform::setupProcess(&probe);
                    probe.start(ffprobe, {"-v", "error", "-show_entries",
                                          "format=duration:stream=width,height,duration", "-of", "json", path});
                    if (probe.waitForFinished(2000) && probe.exitCode() == 0)
                    {
                        const auto info = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
                        item.duration = info.value("format").toObject().value("duration").toString().toDouble();
                        for (const auto &value : info.value("streams").toArray())
                        {
                            const auto stream = value.toObject();
                            item.width = qMax(item.width, stream.value("width").toInt());
                            item.height = qMax(item.height, stream.value("height").toInt());
                            item.duration = qMax(item.duration, stream.value("duration").toString().toDouble());
                        }
                    }
                    else
                    {
                        probe.kill();
                        probe.waitForFinished(1000);
                    }
                }
            }
            rows.append(item);
        }
        return rows;
    }));
}
void Controller::remove(int row)
{
    if (busy() || row < 0 || row >= count())
        return;
    QString key = QDir::cleanPath(model.items[row].source);
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    seen.remove(key);
    model.remove(row);
    emit changed();
}
void Controller::clear()
{
    if (busy())
        return;
    queueGeneration++;
    model.clear();
    batchRows.clear();
    seen.clear();
    emit changed();
}
void Controller::chooseOutput()
{
    auto path = QFileDialog::getExistingDirectory(nullptr, "Save converted files to");
    if (!path.isEmpty())
    {
        opts.output = path;
        emit changed();
    }
}
void Controller::openOutput(int row)
{
    if (row >= 0 && row < count() && !model.items[row].output.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(model.items[row].output));
}
QString Controller::conversionWarning() const
{
    bool alpha = false, raster = false, limited = false, silent = false, combined = false;
    const QStringList alphaImages{"png",  "webp", "avif", "ico", "tif", "tiff", "heic",
                                  "heif", "svg",  "psd",  "tga", "exr", "apng"};
    for (const auto &row : model.items)
    {
        if (!needsConversion(row))
            continue;
        QString ext = QFileInfo(row.source).suffix().toLower();
        if (row.category == "image")
        {
            if ((opts.image == "jpg" || (opts.image == "pdf" && !opts.convertOnly)) && alphaImages.contains(ext))
                alpha = true;
            if (opts.image == "ico")
                limited = true;
            if (opts.image == "pdf")
                combined = true;
        }
        if (row.category == "video")
        {
            if ((opts.video == "av1" || opts.video == "mp4") &&
                QStringList{"gif", "webm", "mkv", "mov", "avi"}.contains(ext))
                alpha = true;
            if (opts.video == "gif" || opts.video == "av1")
                silent = true;
        }
        if (row.category == "pdf" && opts.pdfOutput == "jpg")
            raster = true;
    }
    QStringList warnings;
    if (alpha)
        warnings << "The selected output cannot preserve transparency. Transparent areas will become opaque (JPG uses "
                    "white).";
    if (raster)
        warnings
            << "PDF pages will become JPG images. Selectable text, links, forms and vector detail will become pixels.";
    if (limited)
        warnings << "ICO images will be fitted to a 256 × 256 icon canvas.";
    if (silent)
        warnings << "GIF and raw .av1 outputs contain video only. Audio will be omitted; GIF also reduces colors and "
                    "transparency to a palette.";
    if (combined)
        warnings << "Queued images will become pages of one PDF, in queue order.";
    return warnings.join("\n\n");
}
void Controller::start()
{
    if (property("updating").toBool() || !canConvert())
        return;
    closePreview();
    running = true;
    cancelling = false;
    successes = failures = cancelled = 0;
    message.clear();
    batchRows.clear();
    headroomSamples = 0;
    hardwareMonitor.reset();
    gpuProcesses.clear();
    nvencRows.clear();
    hardwareLoad = hardwareMonitor.sample();
    workerLimit = opts.autoWorkers
                      ? AdaptiveWorkers::initial(AdaptiveWorkers::ceiling(QThread::idealThreadCount(), opts.threads),
                                                 hardwareLoad)
                      : opts.batchWorkers;
    batchElapsed.start();
    batchTimer.start();
    pdfGroups.clear();
    pdfMembers.clear();
    if (opts.image == "pdf")
    {
        QMap<QString, QVector<int>> imageFolders;
        for (int i = 0; i < count(); i++)
            if (model.items[i].category == "image")
                imageFolders[model.items[i].groupRoot.isEmpty() ? QString()
                                                                : QFileInfo(model.items[i].source).absolutePath()]
                    << i;
        for (const auto &images : imageFolders)
        {
            pdfGroups[images.first()] = images;
            for (int i : images)
            {
                if (i != images.first())
                    pdfMembers.insert(i);
                model.items[i].completedKey.clear();
            }
        }
    }
    for (int i = 0; i < count(); i++)
        if (needsConversion(model.items[i]))
        {
            batchRows.insert(i);
            model.items[i].status = "Queued";
            model.items[i].progress = 0;
            model.items[i].after = -1;
            model.items[i].warning.clear();
            model.update(i);
        }
    emit changed();
    schedule();
}
void Controller::schedule()
{
    if (!running)
        return;
    int limit = opts.autoWorkers ? workerLimit : opts.batchWorkers;
    for (const auto &r : model.items)
        if (r.category == "image" && r.status != "Done" && r.status != "Error" && r.status != "Cancelled" &&
            qint64(r.width) * r.height > 64000000)
        {
            limit = 1;
            break;
        }
    workerLimit = limit;
    while (!cancelling && active < limit)
    {
        int row = nextJob();
        if (row < 0)
            break;
        launch(row);
    }
    if (active == 0 && (cancelling || nextJob() < 0))
        finishBatch();
}
double Controller::estimatedWork(int row) const
{
    const auto &item = model.items[row];
    return Scheduling::work(item.category, item.before, item.width, item.height, item.duration, opts);
}
int Controller::nextJob() const
{
    QVector<QPair<int, double>> candidates;
    for (int row = 0; row < count(); ++row)
        if (model.items[row].status == "Queued" && needsConversion(model.items[row]) && !pdfMembers.contains(row))
        {
            double work = estimatedWork(row);
            for (int member : pdfGroups.value(row))
                if (member != row)
                    work += estimatedWork(member);
            candidates.append({row, work});
        }
    return Scheduling::shortestJob(candidates);
}
void Controller::updateBatchLoad()
{
    if (!running || cancelling)
        return;
    if (opts.autoWorkers)
    {
        QSet<qint64> pids;
        for (const auto pid : gpuProcesses)
            pids.insert(pid);
        hardwareLoad = hardwareMonitor.sample(pids, !nvencRows.isEmpty());
        workerLimit =
            AdaptiveWorkers::adjust(workerLimit, AdaptiveWorkers::ceiling(QThread::idealThreadCount(), opts.threads),
                                    hardwareLoad, headroomSamples);
        schedule();
    }
    emit changed();
}
void Controller::launch(int row)
{
    auto *process = new ChildProcess(this);
    QString destinationRoot = opts.output.isEmpty() ? QFileInfo(model.items[row].source).absolutePath()
                                                    : QFileInfo(opts.output).absoluteFilePath();
    if (!opts.output.isEmpty() && !model.items[row].groupRoot.isEmpty())
    {
        const auto &item = model.items[row];
        const QString relative = QDir(item.groupRoot).relativeFilePath(QFileInfo(item.source).absolutePath());
        if (QDir::isAbsolutePath(relative) || relative == ".." || relative.startsWith("../"))
        {
            model.items[row].status = "Error";
            model.items[row].warning = "Source path is outside its queued folder";
            model.update(row);
            process->deleteLater();
            failures++;
            return;
        }
        QString rootName = QFileInfo(item.groupRoot).fileName();
        if (rootName.isEmpty())
            rootName = "Drive-" + item.groupRoot.left(1);
        for (const auto &other : model.items)
            if (!other.groupRoot.isEmpty() && other.groupRoot != item.groupRoot &&
                QFileInfo(other.groupRoot).fileName().compare(rootName, Qt::CaseInsensitive) == 0)
            {
                rootName += "-" + QString::number(qHash(item.groupRoot, 0), 16).right(8);
                break;
            }
        destinationRoot = QDir(destinationRoot).filePath(rootName);
        if (relative != ".")
            destinationRoot = QDir(destinationRoot).filePath(relative);
    }
    QDir().mkpath(destinationRoot);
    auto directory = std::make_shared<Scratch>(destinationRoot + "/.athanor-job-XXXXXX");
    QString job = directory->path() + "/job.json";
    QFile file(job);
    if (!directory->isValid() || !file.open(QIODevice::WriteOnly))
    {
        model.items[row].status = "Error";
        model.items[row].warning = "Cannot create conversion workspace";
        model.update(row);
        process->deleteLater();
        failures++;
        return;
    }
    QString input = rowInput(model.items[row]);
    Options jobOptions = opts;
    jobOptions.output = destinationRoot;
    if (opts.autoWorkers && opts.threads == 0)
        jobOptions.threads = qBound(1, QThread::idealThreadCount() / qMax(1, workerLimit), 8);
    if (input != model.items[row].source)
        jobOptions.deleteOriginal = false;
    QVector<int> group = pdfGroups.value(row, {row});
    QJsonArray sources;
    for (int index : group)
    {
        QString path = rowInput(model.items[index]);
        sources.append(path);
        if (group.size() > 1)
            model.items[index].before = QFileInfo(path).size();
        if (path != model.items[index].source)
            jobOptions.deleteOriginal = false;
    }
    QJsonObject spec{{"source", input},
                     {"original_category", model.items[row].category},
                     {"sources", sources},
                     {"options", jobOptions.json()},
                     {"scratch", directory->path()},
                     {"stem", QFileInfo(model.items[row].source).completeBaseName()}};
    file.write(QJsonDocument(spec).toJson());
    file.close();
    Platform::setupProcess(process, true);
    processes.append(process);
    active++;
    model.items[row].status = "Starting";
    model.update(row);
    emit jobStarted(row);
    emit changed();
    const QString requestedKey = conversionKey(model.items[row], opts);
    auto pending = std::make_shared<QByteArray>();
    auto final = std::make_shared<QJsonObject>();
    auto completed = std::make_shared<bool>(false);
    auto parse = [this, process, row, group, pending, final] {
        *pending += process->readAllStandardOutput();
        while (pending->contains('\n'))
        {
            int n = pending->indexOf('\n');
            auto obj = QJsonDocument::fromJson(pending->left(n)).object();
            pending->remove(0, n + 1);
            if (obj.contains("ok"))
                *final = obj;
            else if (obj.contains("gpu_active"))
            {
                if (obj.value("gpu_active").toBool())
                {
                    gpuProcesses[row] = obj.value("gpu_pid").toInteger();
                    if (obj.value("gpu_encoder").toString() == "av1_nvenc")
                        nvencRows.insert(row);
                }
                else
                {
                    gpuProcesses.remove(row);
                    nvencRows.remove(row);
                }
            }
            else if (obj.contains("percent"))
            {
                model.items[row].progress = obj.value("percent").toInt();
                model.items[row].status = obj.value("stage").toString();
                model.update(row);
                for (int index : group)
                    if (index != row)
                    {
                        model.items[index].progress = model.items[row].progress;
                        model.items[index].status = model.items[row].status;
                        model.update(index);
                    }
            }
        }
    };
    connect(process, &ChildProcess::readyReadStandardOutput, this, parse);
    auto done = [this, process, row, group, directory, final, completed, parse, requestedKey](int exitCode) {
        if (*completed)
            return;
        *completed = true;
        parse();
        if (exitCode == 0 && final->value("ok").toBool())
        {
            model.items[row].status = "Done";
            model.items[row].completedKey = requestedKey;
            model.items[row].progress = 100;
            if (group.size() == 1)
                model.items[row].before = final->value("before").toInteger();
            model.items[row].after = final->value("after").toInteger();
            model.items[row].output = final->value("output").toString();
            model.items[row].warning = final->value("warning").toString();
            successes++;
        }
        else if (cancelling)
        {
            model.items[row].status = "Cancelled";
            cancelled++;
        }
        else
        {
            model.items[row].status = "Error";
            model.items[row].warning =
                final->value("error").toString(QString::fromUtf8(process->readAllStandardError()).trimmed());
            if (model.items[row].warning.isEmpty())
                model.items[row].warning = "The conversion worker exited unexpectedly";
            failures++;
        }
        if (group.size() > 1)
        {
            qint64 totalBefore = 0, totalAfter = final->value("after").toInteger(), assigned = 0;
            for (int index : group)
                totalBefore += model.items[index].before;
            for (int index : group)
            {
                auto &item = model.items[index];
                item.status = model.items[row].status;
                item.progress = model.items[row].progress;
                item.warning = model.items[row].warning;
                item.output = model.items[row].output;
                if (item.status == "Done")
                {
                    item.completedKey = requestedKey;
                    item.after = index == group.last() ? totalAfter - assigned
                                 : totalBefore > 0     ? qint64(totalAfter * (double(item.before) / totalBefore))
                                                       : 0;
                    assigned += item.after;
                    if (index != row)
                        successes++;
                }
                else if (index != row)
                {
                    if (cancelling)
                        cancelled++;
                    else
                        failures++;
                }
                model.update(index);
            }
        }
        model.update(row);
        gpuProcesses.remove(row);
        nvencRows.remove(row);
        active--;
        processes.removeOne(process);
        process->deleteLater();
        emit changed();
        QTimer::singleShot(0, this, [this] { schedule(); });
    };
    connect(process, qOverload<int, QProcess::ExitStatus>(&ChildProcess::finished), this,
            [done](int code, QProcess::ExitStatus) { done(code); });
    connect(process, &ChildProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(-1);
    });
    process->start(QCoreApplication::applicationFilePath(), {"--worker", job});
}
void Controller::cancel()
{
    if (!busy())
        return;
    cancelling = true;
    for (int row = 0; row < count(); row++)
        if (model.items[row].status == "Queued")
        {
            model.items[row].status = "Cancelled";
            cancelled++;
            model.update(row);
        }
    for (auto *p : processes)
        Platform::cancelProcess(p);
    if (active == 0)
        finishBatch();
}
void Controller::finishBatch()
{
    running = false;
    batchTimer.stop();
    message = QString("%1 converted%2%3")
                  .arg(successes)
                  .arg(failures ? QString(" · %1 failed").arg(failures) : QString())
                  .arg(cancelled ? QString(" · %1 cancelled").arg(cancelled) : QString());
    emit changed();
    if (!qEnvironmentVariableIsSet("ATHANOR_TEST"))
    {
        QString notification = message;
        if (compact)
            for (const auto &row : model.items)
                if (!row.warning.isEmpty())
                {
                    notification += "\n" + row.warning;
                    break;
                }
        Platform::notify("Athanor", notification);
    }
    emit batchFinished(successes, failures, cancelled);
}
void Controller::preview(const QString &category, int quality, int selected)
{
    QString source;
    if (selected >= 0 && selected < count() && model.items[selected].category == category)
        source = rowInput(model.items[selected]);
    else
        for (const auto &item : model.items)
            if (item.category == category)
            {
                source = rowInput(item);
                break;
            }
    if (source.isEmpty())
        return;
    previewSource = source;
    previewCategory = category;
    previewBefore = "image://media/" + QString::fromLatin1(source.toUtf8().toBase64(QByteArray::Base64UrlEncoding |
                                                                                    QByteArray::OmitTrailingEquals));
    emit previewOpened(category, quality);
    launchPreview(source, quality, selected);
}
void Controller::launchPreview(const QString &source, int quality, int)
{
    closePreview();
    previewSource = source;
    previewRunning = true;
    previewFailure.clear();
    previewAfter.clear();
    previewDirectory = std::make_unique<Scratch>(Platform::scratchPattern("athanor-preview"));
    if (!previewDirectory->isValid())
    {
        previewFailure = "Cannot create preview workspace";
        previewRunning = false;
        emit previewChanged();
        return;
    }
    previewProcess = new ChildProcess(this);
    int generation = ++previewGeneration;
    auto *p = previewProcess;
    Platform::setupProcess(p, true);
    Options options = opts;
    options.sizeMode = false;
    if (previewCategory == "image")
    {
        options.quality = quality;
        if (options.image == "pdf")
            options.image = "avif";
    }
    else
    {
        options.crf = quality;
        if (QStringList{"opus", "mp3", "wav"}.contains(options.video))
            options.video = "webm";
        options.acceleration = "CPU";
        options.speed = "Fast";
    }
    QString job = previewDirectory->path() + "/job.json";
    QFile file(job);
    const auto jobBytes = QJsonDocument(QJsonObject{{"source", source},
                                                    {"preview", true},
                                                    {"options", options.json()},
                                                    {"scratch", previewDirectory->path()}})
                              .toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(jobBytes) != jobBytes.size() || !file.flush())
    {
        previewFailure = "Cannot write preview job";
        previewRunning = false;
        p->deleteLater();
        previewProcess = nullptr;
        emit previewChanged();
        return;
    }
    file.close();
    connect(p, qOverload<int, QProcess::ExitStatus>(&ChildProcess::finished), this,
            [this, p, generation](int, QProcess::ExitStatus) {
                if (generation != previewGeneration)
                    return;
                QJsonObject final;
                for (auto line : p->readAllStandardOutput().split('\n'))
                {
                    auto obj = QJsonDocument::fromJson(line).object();
                    if (obj.contains("ok"))
                        final = obj;
                }
                if (final.value("ok").toBool())
                    previewAfter =
                        "image://media/" + QString::fromLatin1(final.value("output").toString().toUtf8().toBase64(
                                               QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
                else
                    previewFailure = final.value("error").toString("Preview could not be generated");
                previewRunning = false;
                p->deleteLater();
                previewProcess = nullptr;
                emit previewChanged();
            });
    connect(p, &ChildProcess::errorOccurred, this, [this, p, generation](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && generation == previewGeneration)
        {
            previewFailure = p->errorString();
            previewRunning = false;
            emit previewChanged();
        }
    });
    p->start(QCoreApplication::applicationFilePath(), {"--worker", job});
    emit previewChanged();
}
void Controller::closePreview()
{
    previewGeneration++;
    if (previewProcess)
    {
        Platform::cancelProcess(previewProcess);
        previewProcess->waitForFinished(500);
        previewProcess->deleteLater();
        previewProcess = nullptr;
    }
    previewDirectory.reset();
    previewRunning = false;
    previewAfter.clear();
    emit previewChanged();
}
void Controller::usePreview(const QString &category, int quality)
{
    setOption(category == "image" ? "quality" : "crf", quality);
    closePreview();
}
void Controller::updatePreview(int value)
{
    if (!previewSource.isEmpty())
        launchPreview(previewSource, value, -1);
}

void Controller::setMotion(const QString &value)
{
    if (value != "System" && value != "Full" && value != "Reduced")
        return;
    motionPreference = value;
    save();
    emit changed();
}
bool Controller::reducedMotion() const
{
    return motionPreference == "Reduced" || (motionPreference == "System" && !systemMotion);
}
QJsonObject Controller::queueSummary() const
{
    int done = 0, failed = 0, waiting = 0, warnings = 0, cancelledCount = 0;
    qint64 before = 0, completedBefore = 0, after = 0;
    double totalWork = 0, completedWork = 0;
    for (int i = 0; i < count(); ++i)
    {
        const auto &row = model.items[i];
        done += row.status == "Done";
        failed += row.status == "Error";
        cancelledCount += row.status == "Cancelled";
        waiting += row.status == "Queued";
        warnings += !row.warning.isEmpty() && row.status != "Error";
        before += qMax<qint64>(0, row.before);
        if (row.status == "Done" && row.after >= 0)
        {
            completedBefore += row.before;
            after += row.after;
        }
        if (!running || batchRows.contains(i))
        {
            const double work = estimatedWork(i);
            totalWork += work;
            const int progress = row.status == "Error" ? 100 : qBound(0, row.progress, 100);
            completedWork += work * progress / 100.0;
        }
    }
    const double fraction = totalWork > 0 ? qBound(0.0, completedWork / totalWork, 1.0) : 0;
    QString remaining;
    if (running && !cancelling && batchElapsed.isValid() && batchElapsed.elapsed() >= 10000 && fraction > .005)
    {
        const double seconds = batchElapsed.elapsed() / 1000.0 * (1 - fraction) / fraction;
        if (seconds < 60)
            remaining = "About " + QString::number(qMax(1, int(std::ceil(seconds)))) + "s left";
        else if (seconds < 3600)
            remaining = "About " + QString::number(int(std::ceil(seconds / 60))) + "m left";
        else
            remaining = "About " + QString::number(seconds / 3600, 'f', 1) + "h left";
    }
    const qint64 saved = completedBefore - after;
    return {{"done", done},
            {"failed", failed},
            {"waiting", waiting},
            {"cancelled", cancelledCount},
            {"warnings", warnings},
            {"percent", int(std::round(fraction * 100))},
            {"saved", sizeText(saved)},
            {"saved_bytes", saved},
            {"before", sizeText(before)},
            {"after", done ? sizeText(after) : QString::fromUtf8("—")},
            {"smaller", completedBefore > 0 ? QString::number(100.0 * saved / completedBefore, 'f', 1) + "%"
                                            : QString::fromUtf8("—")},
            {"active", active},
            {"workers", workerLimit},
            {"remaining", remaining}};
}
QJsonObject Controller::rowInfo(int index) const
{
    if (index < 0 || index >= count())
        return {};
    const auto &row = model.items[index];
    return {{"source", row.source},
            {"name", QFileInfo(row.source).fileName()},
            {"output", row.output},
            {"warning", row.warning},
            {"status", row.status},
            {"category", row.category},
            {"hasOutput", !row.output.isEmpty() && QFileInfo::exists(row.output)}};
}
void Controller::openFolder(int index)
{
    if (index < 0 || index >= count())
        return;
    const auto &row = model.items[index];
    auto path = row.output.isEmpty() ? row.source : row.output;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}
QString Controller::outputError() const
{
    if (opts.output.isEmpty())
        return {};
    QFileInfo path(opts.output);
    if (path.exists() && !path.isDir())
        return "Choose a folder instead of a file.";
#ifdef Q_OS_WIN
    if (opts.output.contains(QRegularExpression("[<>\"|?*]")))
        return "Remove invalid characters from the folder path.";
#endif
    return {};
}
