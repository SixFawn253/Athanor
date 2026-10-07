#include <climits>
#include "conversion.h"
#include "childprocess.h"
#include "pdf_bridge.h"
#include "platform.h"
#include "scratch.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <QUuid>
#include <QtEndian>
#include <avif/avif.h>
#include <cmath>
#include <libheif/heif.h>
#include <libheif/heif_items.h>
#include <libheif/heif_properties.h>
#include <memory>
#include <stdexcept>
#include <webp/demux.h>
#include <webp/encode.h>
#include <webp/mux.h>
static void fail(const QString &text)
{
    throw std::runtime_error(text.toUtf8().constData());
}

static QString tool(const QString &name)
{
    const auto path = Platform::toolPath(name);
    if (path.isEmpty())
        fail("Missing conversion tool: " + name);
    return path;
}

QJsonObject Options::json() const
{
    return {{"image", image},
            {"video", video},
            {"audio", audio},
            {"audio_bitrate", audioBitrate},
            {"pdf_output", pdfOutput},
            {"pdf_dpi", pdfDpi},
            {"pdf", pdf},
            {"speed", speed},
            {"acceleration", acceleration},
            {"output", output},
            {"quality", quality},
            {"crf", crf},
            {"threads", threads},
            {"batch_workers", batchWorkers},
            {"delete", deleteOriginal},
            {"resize_webp", resizeWebp},
            {"size_mode", sizeMode},
            {"convert_only", convertOnly},
            {"target_bytes", targetBytes},
            {"image_lossless", imageLossless},
            {"video_lossless", videoLossless}};
}

Options Options::fromJson(const QJsonObject &j)
{
    Options o;
    o.image = j.value("image").toString(o.image);
    o.video = j.value("video").toString(o.video);
    o.audio = j.value("audio").toString(o.audio);
    o.audioBitrate = qBound(6, j.value("audio_bitrate").toInt(96), 320);
    o.pdfOutput = j.value("pdf_output").toString("pdf");
    o.pdfDpi = qBound(36, j.value("pdf_dpi").toInt(150), 600);
    o.pdf = j.value("pdf").toString(o.pdf);
    o.speed = j.value("speed").toString(o.speed);
    o.acceleration = j.value("acceleration").toString(o.acceleration);
    o.output = j.value("output").toString();
    o.quality = qBound(1, j.value("quality").toInt(o.quality), 100);
    o.crf = qBound(0, j.value("crf").toInt(o.crf), 63);
    o.threads = qBound(0, j.value("threads").toInt(0), 256);
    o.batchWorkers = qBound(1, j.value("batch_workers").toInt(2), 8);
    o.deleteOriginal = j.value("delete").toBool(false);
    o.resizeWebp = j.value("resize_webp").toBool(false);
    o.sizeMode = j.value("size_mode").toBool(false);
    o.convertOnly = j.value("convert_only").toBool(false);
    o.targetBytes = qBound<qint64>(0, j.value("target_bytes").toInteger(2000000), 1000000000000000LL);
    o.imageLossless = j.value("image_lossless").toBool(false);
    o.videoLossless = j.value("video_lossless").toBool(false);
    return o;
}

QStringList Conversion::imageExtensions()
{
    return QString("jpg jpeg png webp avif bmp tif tiff ico heic heif jxl exr hdr dpx ppm pgm pbm tga svg psd jp2 "
                   "j2k qoi apng")
        .split(' ');
}

QStringList Conversion::videoExtensions()
{
    return QString("gif mp4 mkv mov avi webm m4v mpg mpeg mts m2ts ts wmv flv ogv 3gp vob av1 ivf mxf asf rm rmvb")
        .split(' ');
}

QStringList Conversion::audioExtensions()
{
    return QString("opus mp3 wav flac ogg m4a aac wma aiff aif alac ac3 mka").split(' ');
}

QString Conversion::kind(const QString &p)
{
    auto ext = QFileInfo(p).suffix().toLower();
    if (imageExtensions().contains(ext))
        return "image";
    if (videoExtensions().contains(ext))
        return "video";
    if (audioExtensions().contains(ext))
        return "audio";
    return ext == "pdf" ? "pdf" : "";
}

QString Conversion::supportedFilter()
{
    auto list = imageExtensions() + videoExtensions() + audioExtensions() + QStringList{"pdf"};
    for (auto &s : list)
        s = "*." + s;
    return "Supported files (" + list.join(' ') + ")";
}

struct ProcessResult
{
    int code = 0;
    QByteArray output, error;
};
static ProcessResult process(const QString &program, const QStringList &args, Progress progress = {},
                             double duration = 0)
{
    ChildProcess p;
    Platform::setupProcess(&p);
    p.setProgram(program);
    p.setArguments(args);
    p.start();
    if (!p.waitForStarted(10000))
        fail(p.errorString());
    ProcessResult result;
    QByteArray pending;
    while (p.state() != QProcess::NotRunning)
    {
        p.waitForReadyRead(60);
        QByteArray data = p.readAllStandardOutput();
        result.output += data;
        result.error += p.readAllStandardError();
        if (result.error.size() > 65536)
            result.error = result.error.right(65536);
        pending += data;
        while (pending.contains('\n'))
        {
            int n = pending.indexOf('\n');
            auto line = pending.left(n).trimmed();
            pending.remove(0, n + 1);
            if (progress && duration > 0 && line.startsWith("out_time_us="))
            {
                auto t = line.mid(12).toDouble() / 1000000.0;
                progress(qBound(2, int(t / duration * 93), 95), "Encoding media");
            }
        }
    }
    result.output += p.readAllStandardOutput();
    result.error += p.readAllStandardError();
    result.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return result;
}

static QByteArray alphaHash(const QImage &source)
{
    auto image = source.convertToFormat(QImage::Format_RGBA8888);
    QCryptographicHash h(QCryptographicHash::Sha256);
    QByteArray row(image.width(), 0);
    for (int y = 0; y < image.height(); y++)
    {
        auto p = image.constScanLine(y);
        for (int x = 0; x < image.width(); x++)
            row[x] = char(p[x * 4 + 3]);
        h.addData(row);
    }
    return h.result();
}

static QImage avifFrame(avifDecoder *d)
{
    if (d->image->width > INT_MAX || d->image->height > INT_MAX)
        fail("Image dimensions exceed the native image limit");
    QImage image(int(d->image->width), int(d->image->height), QImage::Format_RGBA8888);
    if (image.isNull())
        fail("Insufficient memory to decode this image");
    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, d->image);
    rgb.depth = 8;
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    rgb.alphaPremultiplied = AVIF_FALSE;
    rgb.pixels = image.bits();
    rgb.rowBytes = uint32_t(image.bytesPerLine());
    rgb.maxThreads = 1;
    rgb.avoidLibYUV = AVIF_TRUE;
    auto rc = avifImageYUVToRGB(d->image, &rgb);
    if (rc != AVIF_RESULT_OK)
        fail("AVIF RGB conversion: " + QString::fromUtf8(avifResultToString(rc)));
    if (d->image->icc.size)
        image.setColorSpace(
            QColorSpace::fromIccProfile(QByteArray((const char *)d->image->icc.data, qsizetype(d->image->icc.size))));
    if (d->image->transformFlags & AVIF_TRANSFORM_CLAP)
    {
        avifCropRect crop;
        avifDiagnostics diagnostics{};
        if (!avifCropRectFromCleanApertureBox(&crop, &d->image->clap, d->image->width, d->image->height, &diagnostics))
            fail("Invalid AVIF crop rectangle");
        image = image.copy(int(crop.x), int(crop.y), int(crop.width), int(crop.height));
    }
    if (d->image->transformFlags & AVIF_TRANSFORM_IROT)
        image = image.transformed(QTransform().rotate(-90 * d->image->irot.angle));
    if (d->image->transformFlags & AVIF_TRANSFORM_IMIR)
        image = image.mirrored(d->image->imir.axis == 1, d->image->imir.axis == 0);
    return image;
}

static QByteArray pixelHash(const QImage &source)
{
    QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (int y = 0; y < image.height(); y++)
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)), image.width() * 4));
    return hash.result();
}
static QStringList decoderArguments(const QString &path);
struct Frames
{
    QVector<QImage> images;
    QVector<int> durations;
    int loop = 0;
};
static Frames readFrames(const QString &path, bool strict8bit = false)
{
    Frames f;
    auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "heic" || suffix == "heif")
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            fail(file.errorString());
        auto bytes = file.readAll();
        std::unique_ptr<heif_context, decltype(&heif_context_free)> context(heif_context_alloc(), heif_context_free);
        if (!context)
            fail("Cannot allocate HEIF decoder");
        auto err =
            heif_context_read_from_memory_without_copy(context.get(), bytes.constData(), size_t(bytes.size()), nullptr);
        if (err.code)
            fail(QString::fromUtf8(err.message));
        int count = heif_context_get_number_of_top_level_images(context.get());
        QVector<heif_item_id> ids(count);
        heif_context_get_list_of_top_level_image_IDs(context.get(), ids.data(), count);
        heif_item_id primary = 0;
        heif_context_get_primary_image_ID(context.get(), &primary);
        auto primaryIndex = ids.indexOf(primary);
        if (primaryIndex > 0)
            ids.move(primaryIndex, 0);
        for (auto id : ids)
        {
            heif_image_handle *rawHandle = nullptr;
            err = heif_context_get_image_handle(context.get(), id, &rawHandle);
            if (err.code)
                fail(QString::fromUtf8(err.message));
            std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)> handle(rawHandle,
                                                                                            heif_image_handle_release);
            if (strict8bit && heif_image_handle_get_luma_bits_per_pixel(handle.get()) > 8)
                fail("Lossless conversion requires an 8-bit source image");
            heif_image *rawImage = nullptr;
            err =
                heif_decode_image(handle.get(), &rawImage, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);
            if (err.code)
                fail(QString::fromUtf8(err.message));
            std::unique_ptr<heif_image, decltype(&heif_image_release)> decoded(rawImage, heif_image_release);
            int stride = 0;
            const uint8_t *pixels = heif_image_get_plane_readonly(decoded.get(), heif_channel_interleaved, &stride);
            int width = heif_image_get_primary_width(decoded.get()),
                height = heif_image_get_primary_height(decoded.get());
            if (!pixels || width <= 0 || height <= 0)
                fail("Invalid HEIF image plane");
            QImage image(pixels, width, height, stride, QImage::Format_RGBA8888);
            auto copy = image.copy();
            size_t iccSize = heif_image_handle_get_raw_color_profile_size(handle.get());
            if (iccSize)
            {
                QByteArray icc(qsizetype(iccSize), 0);
                if (heif_image_handle_get_raw_color_profile(handle.get(), icc.data()).code == 0)
                    copy.setColorSpace(QColorSpace::fromIccProfile(icc));
            }
            f.images.append(copy);
            f.durations.append(100);
        }
        if (f.images.isEmpty())
            fail("HEIF contains no image");
        return f;
    }
    if (suffix == "png" || suffix == "apng")
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
        {
            QVector<int> delays;
            bool animated = false;
            file.seek(8);
            while (!file.atEnd())
            {
                auto header = file.read(8);
                if (header.size() != 8)
                    break;
                quint32 length = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(header.constData()));
                auto tag = header.mid(4, 4);
                if (tag == "acTL" && length == 8)
                {
                    auto chunk = file.read(8);
                    if (chunk.size() != 8)
                        fail("Truncated APNG animation header");
                    animated = true;
                    f.loop = int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(chunk.constData() + 4)));
                }
                else if (tag == "fcTL" && length == 26)
                {
                    auto chunk = file.read(26);
                    if (chunk.size() != 26)
                        fail("Truncated APNG frame header");
                    int numerator = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(chunk.constData() + 20));
                    int denominator = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(chunk.constData() + 22));
                    delays.append(qMax(1, qRound(1000.0 * numerator / (denominator ? denominator : 100))));
                }
                else
                {
                    if (!file.seek(file.pos() + length))
                        break;
                }
                if (!file.seek(file.pos() + 4))
                    break;
                if (tag == "IEND")
                    break;
            }
            if (animated)
            {
                Scratch temp(Platform::scratchPattern("athanor-apng"));
                if (!temp.isValid())
                    fail("Cannot create animation workspace");
                auto result = process(tool("ffmpeg"), {"-nostdin", "-hide_banner", "-v", "error", "-i", path, "-fps_mode",
                                                       "passthrough", "-pix_fmt", "rgba", temp.path() + "/frame-%08d.png"});
                if (result.code)
                    fail(QString::fromUtf8(result.error));
                auto names = QDir(temp.path()).entryList({"frame-*.png"}, QDir::Files, QDir::Name);
                for (int i = 0; i < names.size(); i++)
                {
                    QImage image(temp.path() + '/' + names[i]);
                    if (image.isNull())
                        fail("APNG frame decode failed");
                    f.images.append(image);
                    f.durations.append(delays.value(i, 100));
                }
                if (f.images.isEmpty())
                    fail("APNG contains no frames");
                return f;
            }
        }
    }
    if (QFileInfo(path).suffix().compare("webp", Qt::CaseInsensitive) == 0)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            fail(file.errorString());
        auto bytes = file.readAll();
        WebPData data{reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size())};
        WebPAnimDecoderOptions options;
        WebPAnimDecoderOptionsInit(&options);
        options.color_mode = MODE_RGBA;
        std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(WebPAnimDecoderNew(&data, &options),
                                                                                   WebPAnimDecoderDelete);
        if (!decoder)
            fail("Unable to decode WebP");
        WebPAnimInfo info;
        if (!WebPAnimDecoderGetInfo(decoder.get(), &info))
            fail("Invalid WebP metadata");
        f.loop = info.loop_count;
        int previous = 0;
        QColorSpace colors;
        WebPChunkIterator chunk;
        if (WebPDemuxGetChunk(WebPAnimDecoderGetDemuxer(decoder.get()), "ICCP", 1, &chunk))
        {
            colors = QColorSpace::fromIccProfile(
                QByteArray(reinterpret_cast<const char *>(chunk.chunk.bytes), qsizetype(chunk.chunk.size)));
            WebPDemuxReleaseChunkIterator(&chunk);
        }
        while (WebPAnimDecoderHasMoreFrames(decoder.get()))
        {
            uint8_t *pixels = nullptr;
            int timestamp = 0;
            if (!WebPAnimDecoderGetNext(decoder.get(), &pixels, &timestamp))
                fail("WebP frame decode failed");
            QImage image(pixels, info.canvas_width, info.canvas_height, info.canvas_width * 4, QImage::Format_RGBA8888);
            auto copy = image.copy();
            if (colors.isValid())
                copy.setColorSpace(colors);
            f.images.append(copy);
            f.durations.append(qMax(1, timestamp - previous));
            previous = timestamp;
        }
        return f;
    }
    if (QFileInfo(path).suffix().compare("avif", Qt::CaseInsensitive) == 0)
    {
        std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)> d(avifDecoderCreate(), avifDecoderDestroy);
        if (!d)
            fail("Cannot allocate AVIF decoder");
        d->maxThreads = qMin(8, QThread::idealThreadCount());
        d->imageDimensionLimit = 0;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            fail(file.errorString());
        QByteArray bytes = file.readAll();
        auto rc =
            avifDecoderSetIOMemory(d.get(), reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()));
        if (rc == AVIF_RESULT_OK)
            rc = avifDecoderParse(d.get());
        if (rc != AVIF_RESULT_OK)
            fail("AVIF parse: " + QString::fromUtf8(avifResultToString(rc)));
        f.loop = d->repetitionCount < 0 ? 0 : d->repetitionCount + 1;
        for (int i = 0; i < d->imageCount; i++)
        {
            rc = avifDecoderNextImage(d.get());
            if (rc != AVIF_RESULT_OK)
                fail(QString::fromUtf8(avifResultToString(rc)));
            if (strict8bit && d->image->depth > 8)
                fail("Lossless conversion requires an 8-bit source image");
            f.images.append(avifFrame(d.get()));
            f.durations.append(qMax(1, int(std::round(d->imageTiming.duration * 1000))));
        }
        return f;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(0);
    f.loop = reader.loopCount() < 0 ? 0 : reader.loopCount() + 1;
    do
    {
        auto image = reader.read();
        if (strict8bit && image.depth() > 32)
            fail("Lossless conversion requires an 8-bit source image for these output formats");
        if (image.isNull())
            break;
        f.images.append(image.convertToFormat(QImage::Format_RGBA8888));
        f.durations.append(qMax(1, reader.nextImageDelay()));
    } while (reader.supportsAnimation() && reader.canRead());
    if (!f.images.isEmpty())
        return f;
    if (strict8bit)
        fail("Lossless decoding is not available for this source format");
    Scratch temp(Platform::scratchPattern("athanor-decode"));
    QStringList decode = {"-nostdin", "-hide_banner", "-v", "error"};
    decode << decoderArguments(path) << "-i" << path << "-frames:v" << "1" << "-pix_fmt" << "rgba"
           << temp.path() + "/decoded.png";
    auto result = process(tool("ffmpeg"), decode);
    if (result.code)
        fail(QString::fromUtf8(result.error));
    QImage image(temp.path() + "/decoded.png");
    if (image.isNull())
        fail("Unable to decode this image format");
    f.images.append(image);
    f.durations.append(100);
    return f;
}

QImage Conversion::readImage(const QString &path, int frame)
{
    auto frames = readFrames(path);
    return frames.images.value(qBound(0, frame, frames.images.size() - 1));
}

static void webp(const Frames &f, const QString &path, int quality, bool lossless = false)
{
    WebPConfig config;
    if (!WebPConfigInit(&config))
        fail("WebP ABI mismatch");
    config.quality = float(quality);
    config.lossless = lossless ? 1 : 0;
    config.method = 6;
    config.alpha_quality = 100;
    config.exact = 1;
    if (!WebPValidateConfig(&config))
        fail("Invalid WebP configuration");
    WebPData data = {};
    if (lossless && f.images.size() == 1)
    {
        auto image = f.images[0].convertToFormat(QImage::Format_RGBA8888);
        WebPPicture picture;
        WebPPictureInit(&picture);
        picture.use_argb = 1;
        picture.width = image.width();
        picture.height = image.height();
        WebPMemoryWriter writer;
        WebPMemoryWriterInit(&writer);
        picture.writer = WebPMemoryWrite;
        picture.custom_ptr = &writer;
        if (!WebPPictureImportRGBA(&picture, image.constBits(), image.bytesPerLine()) || !WebPEncode(&config, &picture))
        {
            WebPPictureFree(&picture);
            WebPMemoryWriterClear(&writer);
            fail("Lossless WebP encoding failed");
        }
        WebPPictureFree(&picture);
        data.bytes = writer.mem;
        data.size = writer.size;
    }
    else
    {
        WebPAnimEncoderOptions opts;
        WebPAnimEncoderOptionsInit(&opts);
        opts.anim_params.loop_count = f.loop;
        opts.anim_params.bgcolor = 0;
        opts.minimize_size = 1;
        std::unique_ptr<WebPAnimEncoder, decltype(&WebPAnimEncoderDelete)> encoder(
            WebPAnimEncoderNew(f.images[0].width(), f.images[0].height(), &opts), WebPAnimEncoderDelete);
        if (!encoder)
            fail("Unable to create WebP encoder");
        int timestamp = 0;
        for (int i = 0; i < f.images.size(); i++)
        {
            auto image = f.images[i].convertToFormat(QImage::Format_RGBA8888);
            WebPPicture picture;
            if (!WebPPictureInit(&picture))
                fail("WebP picture ABI mismatch");
            picture.use_argb = 1;
            picture.width = image.width();
            picture.height = image.height();
            if (!WebPPictureImportRGBA(&picture, image.constBits(), image.bytesPerLine()))
            {
                WebPPictureFree(&picture);
                fail("Unable to allocate WebP image");
            }
            int ok = WebPAnimEncoderAdd(encoder.get(), &picture, timestamp, &config);
            WebPPictureFree(&picture);
            if (!ok)
                fail(QString::fromUtf8(WebPAnimEncoderGetError(encoder.get())));
            timestamp += f.durations[i];
        }
        if (!WebPAnimEncoderAdd(encoder.get(), nullptr, timestamp, nullptr) ||
            !WebPAnimEncoderAssemble(encoder.get(), &data))
            fail(QString::fromUtf8(WebPAnimEncoderGetError(encoder.get())));
    }
    if (f.images[0].colorSpace().isValid())
    {
        WebPMux *mux = WebPMuxCreate(&data, 1);
        auto icc = f.images[0].colorSpace().iccProfile();
        WebPData profile{(const uint8_t *)icc.constData(), size_t(icc.size())};
        WebPMuxSetChunk(mux, "ICCP", &profile, 1);
        WebPDataClear(&data);
        auto rc = WebPMuxAssemble(mux, &data);
        WebPMuxDelete(mux);
        if (rc != WEBP_MUX_OK)
            fail("Cannot preserve the color profile");
    }
    // A cropped opaque animation frame can still leave a transparent canvas.
    // Declare alpha explicitly so consumers choose an RGBA canvas for such files.
    if (data.size >= 30 && memcmp(data.bytes + 12, "VP8X", 4) == 0 &&
        std::any_of(f.images.cbegin(), f.images.cend(), [](const QImage &img) { return img.hasAlphaChannel(); }))
        const_cast<uint8_t *>(data.bytes)[20] |= 0x10;
    QFile file(path);
    bool ok = file.open(QIODevice::WriteOnly) &&
              file.write((const char *)data.bytes, qsizetype(data.size)) == qsizetype(data.size);
    file.close();
    WebPDataClear(&data);
    if (!ok)
        fail("Cannot write WebP output");
}

static int fitTarget(const QString &destination, const Options &options, bool imageKind, Progress progress,
                     const std::function<void(int, Progress)> &encode)
{
    bool lossless = imageKind ? options.imageLossless : options.videoLossless;
    if (lossless)
    {
        encode(imageKind ? 100 : 0, progress);
        if (options.sizeMode && QFileInfo(destination).size() > options.targetBytes)
            fail("Lossless output exceeds the target size. Increase the target or turn off Lossless.");
        return imageKind ? 100 : 0;
    }
    if (!options.sizeMode)
    {
        int value = imageKind ? options.quality : options.crf;
        encode(value, progress);
        return value;
    }
    if (options.targetBytes <= 0)
        fail("Enter a positive target file size");
    Scratch temp(QFileInfo(destination).absolutePath() + "/target-XXXXXX");
    if (!temp.isValid())
        fail("Cannot create target size workspace");
    QString best = temp.path() + "/best." + QFileInfo(destination).suffix();
    QString smallest = temp.path() + "/smallest." + QFileInfo(destination).suffix();
    qint64 smallestSize = LLONG_MAX;
    int smallestValue = -1;
    int low = imageKind ? 1 : 0, high = imageKind ? 100 : 63, chosen = -1, attempt = 0;
    while (low <= high)
    {
        int rank = (low + high) / 2, value = imageKind ? rank : 63 - rank;
        int base = 18 + attempt * 9;
        QString label =
            QString("Target size · attempt %1 · %2 %3").arg(++attempt).arg(imageKind ? "quality" : "CRF").arg(value);
        QFile::remove(destination);
        progress(base, label);
        encode(value, [=](int p, const QString &) { progress(qMin(92, base + p * 8 / 100), label); });
        qint64 size = QFileInfo(destination).size();
        if (size > 0 && size < smallestSize)
        {
            QFile::remove(smallest);
            if (!QFile::copy(destination, smallest))
                fail("Cannot retain smallest candidate");
            smallestSize = size;
            smallestValue = value;
        }
        if (size > 0 && size <= options.targetBytes)
        {
            QFile::remove(best);
            if (!QFile::copy(destination, best))
                fail("Cannot retain target size candidate");
            chosen = value;
            low = rank + 1;
        }
        else
            high = rank - 1;
    }
    QFile::remove(destination);
    if (chosen < 0)
    {
        best = smallest;
        chosen = smallestValue;
    }
    if (!QFile::copy(best, destination))
        fail("Cannot save target size candidate");
    return chosen;
}

static void heifCheck(heif_error error)
{
    if (error.code != heif_error_Ok)
        fail(QString::fromUtf8(error.message));
}
static void heic(const QImage &source, const QString &path, int quality, const Options &o)
{
    std::unique_ptr<heif_context, decltype(&heif_context_free)> context(heif_context_alloc(), heif_context_free);
    if (!context)
        fail("Cannot allocate HEIF encoder");
    heif_encoder *rawEncoder = nullptr;
    heifCheck(heif_context_get_encoder_for_format(context.get(), heif_compression_HEVC, &rawEncoder));
    std::unique_ptr<heif_encoder, decltype(&heif_encoder_release)> encoder(rawEncoder, heif_encoder_release);
    heifCheck(heif_encoder_set_lossy_quality(encoder.get(), quality));
    heifCheck(heif_encoder_set_lossless(encoder.get(), o.imageLossless));
    heifCheck(heif_encoder_set_parameter_string(encoder.get(), "chroma", o.imageLossless ? "444" : "420"));
    heifCheck(heif_encoder_set_parameter_string(encoder.get(), "preset",
                                                o.speed == "Fast"       ? "veryfast"
                                                : o.speed == "Balanced" ? "medium"
                                                                        : "slow"));
    heifCheck(heif_encoder_set_parameter(
        encoder.get(), "x265:pools",
        QByteArray::number(o.threads ? o.threads : qBound(1, QThread::idealThreadCount(), 8)).constData()));
    heif_encoder_set_logging_level(encoder.get(), 0);
    QImage rgba = source.convertToFormat(QImage::Format_RGB888);
    heif_image *rawImage = nullptr;
    heifCheck(
        heif_image_create(rgba.width(), rgba.height(), heif_colorspace_RGB, heif_chroma_interleaved_RGB, &rawImage));
    std::unique_ptr<heif_image, decltype(&heif_image_release)> image(rawImage, heif_image_release);
    heifCheck(heif_image_add_plane(image.get(), heif_channel_interleaved, rgba.width(), rgba.height(), 8));
    int stride = 0;
    auto pixels = heif_image_get_plane(image.get(), heif_channel_interleaved, &stride);
    if (!pixels || stride < rgba.width() * 3)
        fail("Cannot allocate HEIF image plane");
    for (int y = 0; y < rgba.height(); y++)
        memcpy(pixels + qsizetype(y) * stride, rgba.constScanLine(y), size_t(rgba.width()) * 3);
    auto icc = source.colorSpace().iccProfile();
    if (!icc.isEmpty())
        heifCheck(heif_image_set_raw_color_profile(image.get(), "prof", icc.constData(), size_t(icc.size())));
    std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)> nclx(
        heif_nclx_color_profile_alloc(), heif_nclx_color_profile_free);
    std::unique_ptr<heif_encoding_options, decltype(&heif_encoding_options_free)> options(heif_encoding_options_alloc(),
                                                                                          heif_encoding_options_free);
    if (!nclx || !options)
        fail("Cannot allocate HEIF encoding options");
    nclx->color_primaries = heif_color_primaries_ITU_R_BT_709_5;
    nclx->transfer_characteristics = heif_transfer_characteristic_IEC_61966_2_1;
    nclx->matrix_coefficients =
        o.imageLossless ? heif_matrix_coefficients_RGB_GBR : heif_matrix_coefficients_ITU_R_BT_709_5;
    nclx->full_range_flag = 1;
    heifCheck(heif_image_set_nclx_color_profile(image.get(), nclx.get()));
    options->output_nclx_profile = nclx.get();
    options->save_alpha_channel = 0;
    options->save_two_colr_boxes_when_ICC_and_nclx_available = 1;
    heif_image_handle *rawColorHandle = nullptr;
    heifCheck(heif_context_encode_image(context.get(), image.get(), encoder.get(), options.get(), &rawColorHandle));
    std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)> colorHandle(rawColorHandle,
                                                                                         heif_image_handle_release);
    if (source.hasAlphaChannel())
    {
        QImage alpha = source.convertToFormat(QImage::Format_RGBA8888);
        heif_image *rawAlpha = nullptr;
        heifCheck(heif_image_create(alpha.width(), alpha.height(), heif_colorspace_monochrome, heif_chroma_monochrome,
                                    &rawAlpha));
        std::unique_ptr<heif_image, decltype(&heif_image_release)> alphaImage(rawAlpha, heif_image_release);
        heifCheck(heif_image_add_plane(alphaImage.get(), heif_channel_Y, alpha.width(), alpha.height(), 8));
        int alphaStride = 0;
        auto plane = heif_image_get_plane(alphaImage.get(), heif_channel_Y, &alphaStride);
        if (!plane || alphaStride < alpha.width())
            fail("Cannot allocate HEIF alpha plane");
        for (int y = 0; y < alpha.height(); y++)
            for (int x = 0; x < alpha.width(); x++)
                plane[qsizetype(y) * alphaStride + x] = alpha.constScanLine(y)[x * 4 + 3];
        heifCheck(heif_encoder_set_lossless(encoder.get(), true));
        nclx->matrix_coefficients = heif_matrix_coefficients_ITU_R_BT_709_5;
        heifCheck(heif_image_set_nclx_color_profile(alphaImage.get(), nclx.get()));
        heif_image_handle *rawAlphaHandle = nullptr;
        heifCheck(
            heif_context_encode_image(context.get(), alphaImage.get(), encoder.get(), options.get(), &rawAlphaHandle));
        std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)> alphaHandle(rawAlphaHandle,
                                                                                             heif_image_handle_release);
        auto alphaId = heif_image_handle_get_item_id(alphaHandle.get());
        QByteArray aux(4, 0);
        aux.append("urn:mpeg:hevc:2015:auxid:1");
        aux.append(char(0));
        heifCheck(heif_item_add_raw_property(context.get(), alphaId, heif_fourcc('a', 'u', 'x', 'C'), nullptr,
                                             reinterpret_cast<const uint8_t *>(aux.constData()), size_t(aux.size()), 1,
                                             nullptr));
        heifCheck(heif_context_add_item_reference(context.get(), heif_fourcc('a', 'u', 'x', 'l'), alphaId,
                                                  heif_image_handle_get_item_id(colorHandle.get())));
    }
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly))
        fail(output.errorString());
    heif_writer writer{};
    writer.writer_api_version = 1;
    writer.write = [](heif_context *, const void *data, size_t size, void *user) -> heif_error {
        auto file = static_cast<QFile *>(user);
        if (size > size_t(LLONG_MAX) || file->write(static_cast<const char *>(data), qint64(size)) != qint64(size))
            return {heif_error_Encoding_error, heif_suberror_Unspecified, "Cannot write HEIF output"};
        return {heif_error_Ok, heif_suberror_Unspecified, "Success"};
    };
    heifCheck(heif_context_write(context.get(), &writer, &output));
    if (!output.flush())
        fail(output.errorString());
}

static void image(const QString &source, const QString &destination, const Options &o, Progress progress,
                  int previewSize = 0)
{
    progress(2, "Decoding image");
    Frames frames = readFrames(source, o.imageLossless);
    QVector<QByteArray> alpha;
    QVector<QSize> sizes;
    QVector<QByteArray> colors;
    for (auto &img : frames.images)
    {
        if (previewSize > 0)
            img = img.scaled(previewSize, previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (o.imageLossless && !o.convertOnly && o.image == "webp" && (img.width() > 16383 || img.height() > 16383))
            fail("WebP cannot preserve these dimensions losslessly. Choose AVIF.");
        if (o.image == "webp" && (img.width() > 16383 || img.height() > 16383))
            img = img.scaled(16383, 16383, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        sizes.append(img.size());
        alpha.append(alphaHash(img));
        if (o.imageLossless)
            colors.append(pixelHash(img));
    }
    if (o.image == "avif")
    {
        Scratch temp(QFileInfo(destination).absolutePath() + "/frames-XXXXXX");
        QStringList args = {"--yuv",
                            (frames.images[0].width() > 8192 || frames.images[0].height() > 8192 ||
                             qint64(frames.images[0].width()) * frames.images[0].height() > 64000000)
                                ? "420"
                                : "444",
                            "--depth",
                            "8",
                            "--speed",
                            o.speed == "Fast"       ? "8"
                            : o.speed == "Balanced" ? "6"
                                                    : "4",
                            "--jobs",
                            QString::number(o.threads ? o.threads : qBound(1, QThread::idealThreadCount(), 8)),
                            "-q",
                            QString::number(o.quality),
                            "--qalpha",
                            "100",
                            "--ignore-exif",
                            "--ignore-xmp"};
        if (o.imageLossless)
        {
            args[args.indexOf("--yuv") + 1] = "444";
            args << "--lossless" << "--cicp" << "1/13/0" << "--range" << "full";
        }
        if (!o.imageLossless && std::any_of(frames.images.cbegin(), frames.images.cend(),
                                            [](const QImage &img) { return img.hasAlphaChannel(); }))
            args << "--premultiply";
        if (frames.images.size() > 1)
            args << "--timescale" << "1000" << "--repetition-count"
                 << (frames.loop == 0 ? "infinite" : QString::number(frames.loop - 1));
        for (int i = 0; i < frames.images.size(); i++)
        {
            QString png = temp.path() + QString("/frame-%1.png").arg(i);
            QImageWriter writer(png, "png");
            writer.setCompression(1);
            if (!writer.write(frames.images[i]))
                fail(writer.errorString());
            if (frames.images.size() > 1)
                args << "--duration" << QString::number(frames.durations[i]);
            args << png;
            progress(5 + (i * 10) / frames.images.size(), "Preparing image");
        }
        args << destination;
        frames.images.clear();
        progress(18, "Encoding AVIF");
        fitTarget(destination, o, true, progress, [&](int quality, Progress report) {
            auto trialArgs = args;
            trialArgs[trialArgs.indexOf("-q") + 1] = QString::number(quality);
            report(18, "Encoding AVIF");
            auto result = process(tool("avifenc"), trialArgs);
            if (result.code)
                fail(QString::fromUtf8(result.error + result.output));
        });
    }
    else if (o.image == "heic" || o.image == "heif")
    {
        if (frames.images.size() != 1)
            fail("HEIF output stores one still image. Choose AVIF or WebP to retain animation.");
        fitTarget(destination, o, true, progress, [&](int quality, Progress report) {
            report(18, "Encoding " + o.image.toUpper());
            heic(frames.images[0], destination, quality, o);
        });
    }
    else if (o.image == "webp")
    {
        progress(18, "Encoding WebP");
        fitTarget(destination, o, true, progress, [&](int quality, Progress report) {
            report(18, "Encoding WebP");
            webp(frames, destination, quality, o.imageLossless);
        });
    }
    else
    {
        if (frames.images.size() != 1)
            fail("This output stores one image. Choose AVIF or WebP to retain animation.");
        if (o.image == "ico")
        {
            frames.images[0] = frames.images[0].scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QImage canvas(256, 256, QImage::Format_RGBA8888);
            canvas.fill(Qt::transparent);
            QPainter painter(&canvas);
            painter.drawImage((256 - frames.images[0].width()) / 2, (256 - frames.images[0].height()) / 2,
                              frames.images[0]);
            painter.end();
            frames.images[0] = canvas;
            sizes[0] = canvas.size();
            alpha[0] = alphaHash(canvas);
            if (o.imageLossless)
                colors[0] = pixelHash(canvas);
        }
        if (o.image == "jpg")
        {
            QImage opaque(frames.images[0].size(), QImage::Format_RGB888);
            opaque.fill(Qt::white);
            QPainter painter(&opaque);
            painter.drawImage(0, 0, frames.images[0]);
            painter.end();
            frames.images[0] = opaque;
            alpha[0] = alphaHash(opaque);
        }
        Options settings = o;
        settings.imageLossless = o.image != "jpg";
        fitTarget(destination, settings, true, progress, [&](int quality, Progress report) {
            report(18, "Encoding " + o.image.toUpper());
            if (o.image == "ico")
            {
                QByteArray png;
                QBuffer buffer(&png);
                buffer.open(QIODevice::WriteOnly);
                if (!frames.images[0].save(&buffer, "PNG"))
                    fail("Cannot encode icon image");
                QByteArray header(22, 0);
                qToLittleEndian<quint16>(1, reinterpret_cast<uchar *>(header.data() + 2));
                qToLittleEndian<quint16>(1, reinterpret_cast<uchar *>(header.data() + 4));
                qToLittleEndian<quint16>(1, reinterpret_cast<uchar *>(header.data() + 10));
                qToLittleEndian<quint16>(32, reinterpret_cast<uchar *>(header.data() + 12));
                qToLittleEndian<quint32>(quint32(png.size()), reinterpret_cast<uchar *>(header.data() + 14));
                qToLittleEndian<quint32>(22, reinterpret_cast<uchar *>(header.data() + 18));
                QFile output(destination);
                if (!output.open(QIODevice::WriteOnly) || output.write(header) != header.size() ||
                    output.write(png) != png.size())
                    fail("Cannot write icon");
            }
            else
            {
                QImageWriter writer(destination, o.image == "jpg" ? "jpeg" : "png");
                if (o.image == "jpg")
                {
                    writer.setQuality(quality);
                    writer.setOptimizedWrite(true);
                }
                else
                    writer.setCompression(9);
                if (!writer.write(frames.images[0]))
                    fail(writer.errorString());
            }
        });
    }
    frames.images.clear();
    progress(94, "Validating image");
    Frames result = readFrames(destination);
    if (result.images.size() != sizes.size())
        fail("Image frame count changed during conversion");
    for (int i = 0; i < result.images.size(); i++)
        if (result.images[i].size() != sizes[i] || alphaHash(result.images[i]) != alpha[i])
            fail("Image validation failed: dimensions or transparency changed");
    if (o.imageLossless && o.image != "jpg")
        for (int i = 0; i < result.images.size(); i++)
            if (pixelHash(result.images[i]) != colors[i])
                fail("Lossless image pixel validation failed");
}

static QJsonObject probe(const QString &path)
{
    auto r = process(tool("ffprobe"), {"-v", "error", "-show_streams", "-show_format", "-of", "json", path});
    if (r.code)
        fail(QString::fromUtf8(r.error));
    return QJsonDocument::fromJson(r.output).object();
}

static bool videoHasAlpha(const QString &path)
{
    if (QFileInfo(path).suffix().compare("gif", Qt::CaseInsensitive) != 0)
    {
        for (auto stream : probe(path).value("streams").toArray())
        {
            auto v = stream.toObject();
            if (v.value("codec_type") != "video")
                continue;
            auto fmt = v.value("pix_fmt").toString();
            if (v.value("tags").toObject().value("alpha_mode").toString() == "1" || fmt.startsWith("yuva") ||
                fmt.startsWith("gbrap") || fmt.startsWith("rgba") || fmt.startsWith("bgra") || fmt == "argb" ||
                fmt == "abgr")
                return true;
        }
        return false;
    }
    QImageReader reader(path);
    reader.setAllocationLimit(0);
    do
    {
        QImage image = reader.read();
        if (image.isNull())
            break;
        if (image.hasAlphaChannel())
        {
            image = image.convertToFormat(QImage::Format_RGBA8888);
            for (int y = 0; y < image.height(); y++)
            {
                auto row = image.constScanLine(y);
                for (int x = 0; x < image.width(); x++)
                    if (row[x * 4 + 3] != 255)
                        return true;
            }
        }
    } while (reader.supportsAnimation() && reader.canRead());
    return false;
}
static QStringList decoderArguments(const QString &path)
{
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext != "webm" && ext != "mkv")
        return {};
    auto streams = probe(path).value("streams").toArray();
    for (auto v : streams)
        if (v.toObject().value("codec_name") == "vp9")
            return {"-c:v", "libvpx-vp9"};
    return {};
}
static QStringList gifFilters(const Options &options, bool preview)
{
    QStringList filters;
    if (preview)
        filters << "scale='min(1024,iw)':-2";
    if (!(options.videoLossless && options.video == "mkv"))
        filters << "fps=30" << "pad=ceil(iw/2)*2:ceil(ih/2)*2:color=black@0";
    return filters;
}
static QByteArray alphaFingerprint(const QString &path, const Options &o, bool original, bool preview)
{
    Scratch temp(Platform::scratchPattern("athanor-alpha"));
    if (!temp.isValid())
        fail("Cannot create transparency validation workspace");
    QString file = temp.path() + "/alpha.raw";
    QStringList args = {"-nostdin", "-v", "error", "-y"};
    args << decoderArguments(path) << "-i" << path;
    QStringList filters;
    if (original)
    {
        if (QFileInfo(path).suffix().compare("gif", Qt::CaseInsensitive) == 0)
            filters = gifFilters(o, preview);
        else if (preview)
            filters << "scale='min(1024,iw)':-2";
    }
    if (original && preview)
        args << "-t" << "1";
    filters << "format=rgba" << "alphaextract";
    args << "-vf" << filters.join(',') << "-fps_mode" << "passthrough" << "-pix_fmt" << "gray" << "-f" << "rawvideo" << file;
    auto result = process(tool("ffmpeg"), args);
    if (result.code)
        fail("Transparency decode failed: " + QString::fromUtf8(result.error));
    QFile data(file);
    if (!data.open(QIODevice::ReadOnly))
        fail("Cannot read alpha validation data");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&data);
    return hash.result();
}
static void validateVideo(const QString &source, const QString &destination, const Options &o, Progress progress,
                          bool preview)
{
    auto info = probe(source);
    auto output = probe(destination);
    QJsonObject outVideo, inVideo;
    int inAudio = 0, outAudio = 0;
    for (auto s : info.value("streams").toArray())
    {
        auto j = s.toObject();
        if (j.value("codec_type") == "video" && inVideo.isEmpty())
            inVideo = j;
        if (j.value("codec_type") == "audio")
            inAudio++;
    }
    for (auto s : output.value("streams").toArray())
    {
        auto j = s.toObject();
        if (j.value("codec_type") == "video" && outVideo.isEmpty())
            outVideo = j;
        if (j.value("codec_type") == "audio")
            outAudio++;
    }
    if (QFileInfo(source).suffix().compare("gif", Qt::CaseInsensitive) == 0 && !(o.videoLossless && o.video == "mkv"))
    {
        inVideo["width"] = (inVideo.value("width").toInt() + 1) / 2 * 2;
        inVideo["height"] = (inVideo.value("height").toInt() + 1) / 2 * 2;
    }
    QString expected = o.videoLossless && o.video == "mkv"                                                 ? "ffv1"
                       : videoHasAlpha(source) && o.video != "av1" || o.videoLossless && o.video == "webm" ? "vp9"
                                                                                                           : "av1";
    if (outVideo.value("codec_name") != expected ||
        (!preview &&
         (inVideo.value("width") != outVideo.value("width") || inVideo.value("height") != outVideo.value("height"))) ||
        (o.video != "av1" && inAudio != outAudio))
        fail("Video validation failed");
    progress(97, "Validating video");
    QStringList decode = {"-nostdin", "-hide_banner", "-v", "error"};
    decode << decoderArguments(destination) << "-i" << destination << "-f" << "null" << "-";
    auto r = process(tool("ffmpeg"), decode);
    if (r.code)
        fail("Video decode validation failed: " + QString::fromUtf8(r.error));
    if (videoHasAlpha(source) && o.video != "av1" &&
        alphaFingerprint(source, o, true, preview) != alphaFingerprint(destination, o, false, preview))
        fail("Video transparency validation failed");
}

static QString video(const QString &source, const QString &destination, const Options &o, Progress progress,
                     bool preview = false, bool validate = true)
{
    auto info = probe(source);
    double duration = info.value("format").toObject().value("duration").toString().toDouble();
    if (preview)
        duration = qMin(1.0, duration);
    QStringList cpu = {
        "-c:v",           "libsvtav1",
        "-preset",        o.speed == "Fast" ? "8" : o.speed == "Balanced" ? "6" : "4",
        "-crf",           QString::number(o.crf),
        "-svtav1-params", "lp=" + QString::number(o.threads ? o.threads : qBound(1, QThread::idealThreadCount(), 8)),
        "-pix_fmt",       "yuv420p10le"};
    bool gif = QFileInfo(source).suffix().compare("gif", Qt::CaseInsensitive) == 0;
    if (gif)
        cpu = {"-c:v",        "libaom-av1",
               "-cpu-used",   o.speed == "Fast" ? "8" : o.speed == "Balanced" ? "6" : "4",
               "-crf",        QString::number(o.crf),
               "-b:v",        "0",
               "-row-mt",     "1",
               "-threads",    QString::number(o.threads ? o.threads : qBound(1, QThread::idealThreadCount(), 8)),
               "-aom-params", "timing-info=1",
               "-pix_fmt",    "yuv420p"};
    QString sourcePixels = "yuv420p";
    for (auto stream : info.value("streams").toArray())
    {
        auto v = stream.toObject();
        if (v.value("codec_type") == "video")
        {
            sourcePixels = v.value("pix_fmt").toString("yuv420p");
            break;
        }
    }
    if (sourcePixels == "pal8" || sourcePixels == "rgb24" || sourcePixels == "bgr24" || sourcePixels == "rgba" ||
        sourcePixels == "bgra" || sourcePixels == "argb" || sourcePixels == "abgr")
        sourcePixels = "gbrp";
    sourcePixels.replace("yuva", "yuv");
    sourcePixels.replace("gbrap", "gbrp");
    bool transparent = videoHasAlpha(source) && o.video != "av1";
    if (transparent && o.videoLossless && o.video == "webm" && !o.convertOnly)
        fail("For pixel-lossless transparent GIF conversion, choose MKV. WebM preserves transparency but uses 4:2:0 "
             "color.");
    if (transparent)
        cpu = {"-c:v",          "libvpx-vp9",
               "-pix_fmt",      "yuva420p",
               "-lossless",     "0",
               "-crf",          QString::number(o.crf),
               "-b:v",          "0",
               "-auto-alt-ref", "0",
               "-row-mt",       "1",
               "-cpu-used",     o.speed == "Fast" ? "6" : "4",
               "-threads",      QString::number(o.threads ? o.threads : 8)};
    if (o.videoLossless)
    {
        if (o.video == "mkv")
            cpu = {"-c:v",      "ffv1",
                   "-level",    "3",
                   "-coder",    "1",
                   "-context",  "1",
                   "-g",        "1",
                   "-slicecrc", "1",
                   "-threads",  QString::number(o.threads ? o.threads : 8)};
        else if (o.video == "webm" && !transparent)
            cpu = {"-c:v",    "libvpx-vp9", "-lossless", "1",
                   "-b:v",    "0",          "-pix_fmt",  gif ? "gbrp" : sourcePixels,
                   "-row-mt", "1",          "-threads",  QString::number(o.threads ? o.threads : 8)};
        else if (o.video == "av1")
        {
            cpu = {"-c:v",        "libaom-av1",
                   "-crf",        "0",
                   "-b:v",        "0",
                   "-cpu-used",   "6",
                   "-row-mt",     "1",
                   "-pix_fmt",    gif ? "gbrp" : sourcePixels,
                   "-threads",    QString::number(o.threads ? o.threads : 8),
                   "-aom-params", "timing-info=1"};
        }
    }
    if (transparent && o.videoLossless && o.video == "webm" && o.convertOnly)
    {
        cpu[cpu.indexOf("-lossless") + 1] = "1";
        cpu[cpu.indexOf("-crf") + 1] = "0";
    }
    if (o.videoLossless && o.video == "mkv" && gif)
        cpu << "-pix_fmt" << "bgra";
    bool copyAudio = o.videoLossless;
    if (o.videoLossless && o.video == "webm")
        for (auto stream : info.value("streams").toArray())
        {
            auto audio = stream.toObject();
            if (audio.value("codec_type") == "audio" &&
                !QStringList{"opus", "vorbis"}.contains(audio.value("codec_name").toString()))
            {
                if (!o.convertOnly)
                    fail("For lossless conversion with this audio codec, choose MKV.");
                copyAudio = false;
            }
        }
    QStringList encoders;
    if (!gif && !transparent && !o.videoLossless && o.acceleration == "Auto")
        encoders = {"av1_nvenc", "av1_qsv", "av1_amf"};
    encoders << (o.videoLossless && o.video == "mkv"                   ? "ffv1"
                 : transparent || o.videoLossless && o.video == "webm" ? "libvpx-vp9"
                 : gif || o.videoLossless                              ? "libaom-av1"
                                                                       : "libsvtav1");
    QString used;
    for (const auto &encoder : encoders)
    {
        QStringList codec = cpu;
        if (encoder == "av1_nvenc")
            codec = {"-c:v", encoder, "-preset",  "p4",    "-rc", "vbr", "-cq", QString::number(qMin(o.crf, 51)),
                     "-b:v", "0",     "-pix_fmt", "p010le"};
        if (encoder == "av1_qsv")
            codec = {"-c:v",     encoder, "-preset", "fast", "-global_quality", QString::number(o.crf),
                     "-pix_fmt", "p010le"};
        if (encoder == "av1_amf")
            codec = {
                "-c:v",     encoder,  "-rc", "cqp", "-qp_i", QString::number(o.crf), "-qp_p", QString::number(o.crf),
                "-pix_fmt", "yuv420p"};
        QStringList args = {"-nostdin", "-hide_banner",  "-v", "error", "-y", "-progress",
                            "pipe:1",   "-stats_period", "0.2"};
        args << decoderArguments(source) << "-i" << source << "-map" << "0:v:0";
        QStringList filters;
        if (preview)
        {
            args << "-t" << "1";
            filters << "scale='min(1024,iw)':-2";
        }
        if (gif)
        {
            filters = gifFilters(o, preview);
            if (o.videoLossless && o.video == "mkv")
                args << "-fps_mode" << "passthrough";
        }
        if (!filters.isEmpty())
            args << "-vf" << filters.join(',');
        args << codec;
        if (o.video == "av1")
            args << "-an" << "-f" << "obu";
        else
        {
            args << "-map" << "0:a?" << "-c:a" << (copyAudio ? "copy" : "libopus");
            if (!copyAudio)
                args << "-b:a" << (o.convertOnly ? "256k" : "128k");
            args << "-map_metadata" << "0";
            if (o.video == "mkv")
                args << "-map" << "0:s?" << "-c:s" << "copy";
            args << "-f" << (o.video == "webm" ? "webm" : "matroska");
        }
        args << destination;
        progress(2, "Encoding video · " + encoder);
        auto r = process(tool("ffmpeg"), args, progress, duration);
        if (r.code == 0 && transparent && !(o.videoLossless && o.video == "mkv") &&
            alphaFingerprint(source, o, true, preview) != alphaFingerprint(destination, o, false, preview))
        {
            auto exactArgs = args;
            exactArgs[exactArgs.indexOf("-lossless") + 1] = "1";
            if (exactArgs.contains("-crf"))
                exactArgs[exactArgs.indexOf("-crf") + 1] = "0";
            progress(80, "Preserving exact transparency");
            r = process(tool("ffmpeg"), exactArgs, progress, duration);
        }
        if (r.code == 0)
        {
            used = encoder;
            break;
        }
        QFile::remove(destination);
        if (encoder == encoders.last())
            fail(QString::fromUtf8(r.error));
    }
    if (validate)
        validateVideo(source, destination, o, progress, preview);
    return used;
}

static double mediaDuration(const QJsonObject &info)
{
    double duration = info.value("format").toObject().value("duration").toString().toDouble();
    for (auto stream : info.value("streams").toArray())
        duration = qMax(duration, stream.toObject().value("duration").toString().toDouble());
    return duration;
}
static void validateMedia(const QString &source, const QString &destination, bool audioOnly, bool silent,
                          Progress progress)
{
    auto in = probe(source), out = probe(destination);
    QJsonObject inStream, outStream;
    for (auto value : in.value("streams").toArray())
        if (value.toObject().value("codec_type") == (audioOnly ? "audio" : "video") && inStream.isEmpty())
            inStream = value.toObject();
    for (auto value : out.value("streams").toArray())
        if (value.toObject().value("codec_type") == (audioOnly ? "audio" : "video") && outStream.isEmpty())
            outStream = value.toObject();
    if (inStream.isEmpty() || outStream.isEmpty())
        fail("Output is missing the expected media stream");
    if (!audioOnly)
    {
        int expectedWidth = inStream.value("width").toInt(), expectedHeight = inStream.value("height").toInt();
        if (QFileInfo(destination).suffix() == "mp4")
        {
            expectedWidth = (expectedWidth + 1) / 2 * 2;
            expectedHeight = (expectedHeight + 1) / 2 * 2;
        }
        if (outStream.value("width").toInt() != expectedWidth || outStream.value("height").toInt() != expectedHeight)
            fail("Video output dimensions changed unexpectedly");
    }
    double before = mediaDuration(in), after = mediaDuration(out);
    if (before > 0 && after > 0 && std::abs(before - after) > qMax(0.3, before * 0.02))
        fail("Media output duration changed unexpectedly");
    if (!audioOnly && !silent)
    {
        int beforeAudio = 0, afterAudio = 0;
        for (auto v : in.value("streams").toArray())
            if (v.toObject().value("codec_type") == "audio")
                beforeAudio++;
        for (auto v : out.value("streams").toArray())
            if (v.toObject().value("codec_type") == "audio")
                afterAudio++;
        if (beforeAudio != afterAudio)
            fail("Video output is missing an audio track");
    }
    progress(96, "Validating media");
    auto r = process(tool("ffmpeg"), {"-nostdin", "-v", "error", "-i", destination, "-f", "null", "-"});
    if (r.code)
        fail("Output cannot be decoded: " + QString::fromUtf8(r.error));
}
static void audio(const QString &source, const QString &destination, const Options &o, Progress progress)
{
    auto info = probe(source);
    double duration = mediaDuration(info);
    bool hasAudio = false;
    for (auto v : info.value("streams").toArray())
        if (v.toObject().value("codec_type") == "audio")
            hasAudio = true;
    if (!hasAudio)
        fail("This file has no audio track to extract");
    auto encode = [&](int bitrate, Progress report) {
        QStringList args = {"-nostdin", "-v",   "error", "-y",  "-progress",     "pipe:1", "-i",
                            source,     "-map", "0:a:0", "-vn", "-map_metadata", "0"};
        if (o.audio == "wav")
            args << "-c:a" << "pcm_s24le";
        else if (o.audio == "opus")
            args << "-c:a" << "libopus" << "-b:a" << QString::number(bitrate) + "k" << "-vbr" << "on";
        else
        {
            static const int rates[] = {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
            int chosen = 8;
            for (int rate : rates)
                if (rate <= bitrate)
                    chosen = rate;
            args << "-c:a" << "libmp3lame" << "-b:a" << QString::number(chosen) + "k";
        }
        args << destination;
        report(5, "Encoding " + o.audio.toUpper());
        auto r = process(tool("ffmpeg"), args, report, duration);
        if (r.code)
            fail(QString::fromUtf8(r.error));
    };
    if (!o.sizeMode || o.audio == "wav")
        encode(o.convertOnly ? (o.audio == "opus" ? 256 : 320) : o.audioBitrate, progress);
    else
    {
        if (duration <= 0)
            fail("Cannot determine duration for target-size audio");
        int floor = o.audio == "opus" ? 6 : 8;
        int rate = qBound(floor, int(qMin(320.0, o.targetBytes * 8.0 / duration / 1000.0 * 0.94)), 320);
        if (o.audio == "mp3")
        {
            static const int rates[] = {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
            int chosen = 8;
            for (int r : rates)
                if (r <= rate)
                    chosen = r;
            rate = chosen;
        }
        encode(rate, progress);
        for (int attempt = 0; attempt < 3 && QFileInfo(destination).size() > o.targetBytes && rate > floor; attempt++)
        {
            int next = qMax(floor, int(rate * o.targetBytes / double(QFileInfo(destination).size()) * 0.9));
            if (o.audio == "mp3")
            {
                static const int rates[] = {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
                int chosen = 8;
                for (int r : rates)
                    if (r <= next)
                        chosen = r;
                next = chosen;
            }
            rate = next;
            encode(rate, progress);
        }
    }
    validateMedia(source, destination, true, true, progress);
}
static void compatibleVideo(const QString &source, const QString &destination, const Options &o, Progress progress,
                            bool preview)
{
    const bool gif = o.video == "gif";
    auto info = probe(source);
    double duration = mediaDuration(info);
    Options settings = o;
    settings.videoLossless = false;
    fitTarget(destination, settings, false, progress, [&](int crf, Progress report) {
        QStringList args = {"-nostdin", "-v", "error", "-y", "-progress", "pipe:1"};
        args << decoderArguments(source) << "-i" << source;
        if (preview)
            args << "-t" << "1";
        if (gif)
        {
            int colors = o.convertOnly ? 256 : qBound(4, int(256.0 * (63 - crf) / 63.0), 256);
            QString graph = QString("[0:v:0]%1split[a][b];[a]palettegen=max_colors=%2:reserve_transparent=1[p];[b][p]"
                                    "paletteuse=alpha_threshold=128:diff_mode=rectangle[v]")
                                .arg(preview ? "scale='min(1024,iw)':-1," : "")
                                .arg(colors);
            args << "-filter_complex" << graph << "-map" << "[v]" << "-an" << "-loop" << "0";
        }
        else
        {
            args << "-map" << "0:v:0" << "-map" << "0:a?" << "-vf"
                 << (preview ? "scale='min(1024,iw)':-2,pad=ceil(iw/2)*2:ceil(ih/2)*2"
                             : "pad=ceil(iw/2)*2:ceil(ih/2)*2")
                 << "-c:v" << (o.convertOnly ? "libx264rgb" : "libx264") << "-crf"
                 << QString::number(o.convertOnly ? 0 : qRound(crf * 51.0 / 63.0)) << "-preset"
                 << (o.speed == "Fast"       ? "veryfast"
                     : o.speed == "Balanced" ? "medium"
                                             : "slow")
                 << "-pix_fmt" << (o.convertOnly ? "rgb24" : "yuv420p") << "-threads"
                 << QString::number(o.threads ? o.threads : 8) << "-c:a" << "aac" << "-b:a"
                 << (o.convertOnly ? "256k" : "160k") << "-movflags"
                 << "+faststart";
        }
        args << destination;
        auto r = process(tool("ffmpeg"), args, report, preview ? 1 : duration);
        if (r.code)
            fail(QString::fromUtf8(r.error));
    });
    if (!preview)
        validateMedia(source, destination, false, gif, progress);
}

struct PdfState
{
    QByteArray signature;
    int dpi = 0, quality = 0;
    Progress progress;
};
static void pdfRecord(void *pointer, const char *type, int page, const char *data, size_t size)
{
    auto state = (PdfState *)pointer;
    state->signature += QByteArray(type) + ':' + QByteArray::number(page) + ':' + QByteArray::number(size) + ':';
    state->signature +=
        QCryptographicHash::hash(QByteArray::fromRawData(data, qsizetype(size)), QCryptographicHash::Sha256);
}

static int pdfEncode(void *pointer, const unsigned char *data, int width, int height, int stride, double pointsWidth,
                     double pointsHeight, unsigned char **result, size_t *length, int *newWidth, int *newHeight)
{
    auto state = (PdfState *)pointer;
    QImage img(data, width, height, stride, QImage::Format_RGB888);
    double scale = qMin(1.0, qMin(pointsWidth * state->dpi / 72.0 / width, pointsHeight * state->dpi / 72.0 / height));
    QSize size(qMax(1, qRound(width * scale)), qMax(1, qRound(height * scale)));
    if (size != img.size())
        img = img.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(state->quality);
    writer.setOptimizedWrite(true);
    writer.setProgressiveScanWrite(false);
    if (!writer.write(img))
        return 0;
    *result = (unsigned char *)malloc(size_t(encoded.size()));
    if (!*result)
        return 0;
    memcpy(*result, encoded.constData(), size_t(encoded.size()));
    *length = size_t(encoded.size());
    *newWidth = size.width();
    *newHeight = size.height();
    return 1;
}

static void pdfProgress(void *pointer, int n, int total)
{
    auto state = (PdfState *)pointer;
    if (state->progress)
        state->progress(qBound(3, n * 80 / qMax(1, total), 90), "Optimizing PDF");
}

static void pdf(const QString &source, const QString &destination, const Options &o, Progress progress)
{
    char error[1024] = {};
    PdfState baseline;
    auto src = QFile::encodeName(source);
    int count = athanor_pdf_snapshot(src.constData(), pdfRecord, &baseline, error, sizeof(error));
    if (count < 0)
        fail(QString::fromUtf8(error));
    QVector<QPair<int, int>> profiles = {{0, 0}};
    if (o.pdf == "lossless")
        profiles = {{0, 0}};
    else if (o.sizeMode)
        profiles = {{0, 0}, {150, 85}, {100, 65}, {72, 50}, {48, 40}, {36, 25}};
    else if (o.pdf == "balanced")
        profiles.append({150, 75});
    if (!o.sizeMode && o.pdf == "smallest")
        profiles << QPair<int, int>{100, 55} << QPair<int, int>{100, 40};
    if (!o.sizeMode && o.pdf == "extreme")
        profiles << QPair<int, int>{72, 60} << QPair<int, int>{72, 40};
    Scratch temp(QFileInfo(destination).absolutePath() + "/pdf-XXXXXX");
    QString best = source;
    qint64 bestSize = QFileInfo(source).size();
    for (int i = 0; i < profiles.size(); i++)
    {
        PdfState state;
        state.dpi = profiles[i].first;
        state.quality = profiles[i].second;
        state.progress = [=](int p, const QString &s) { progress(qMin(95, (i * 90 + p) / profiles.size()), s); };
        QString path = temp.path() + QString("/candidate-%1.pdf").arg(i);
        auto dst = QFile::encodeName(path);
        if (athanor_pdf_compress(src.constData(), dst.constData(), state.dpi, state.quality, pdfEncode, pdfProgress,
                                 &state, error, sizeof(error)) < 0)
            fail(QString::fromUtf8(error));
        PdfState check;
        int pages = athanor_pdf_snapshot(dst.constData(), pdfRecord, &check, error, sizeof(error));
        if (pages != count || check.signature != baseline.signature)
            fail("PDF validation failed: text or document structure changed");
        qint64 size = QFileInfo(path).size();
        if (size < bestSize)
        {
            best = path;
            bestSize = size;
        }
        if (o.sizeMode && bestSize <= o.targetBytes)
            break;
    }
    if (!QFile::copy(best, destination))
        fail("Cannot save compressed PDF");
}

struct PageExport
{
    QString folder, error;
    Options options;
    Progress progress;
    qint64 bytes = 0;
};
static int exportPage(void *pointer, const unsigned char *samples, int width, int height, int stride, int page,
                      int count)
{
    auto &state = *static_cast<PageExport *>(pointer);
    try
    {
        QImage image(samples, width, height, stride, QImage::Format_RGB888);
        QString path = state.folder + QString("/page-%1.jpg").arg(page + 1, 4, 10, QChar('0'));
        Options options = state.options;
        options.imageLossless = false;
        if (options.sizeMode)
            options.targetBytes = qMax<qint64>(1, options.targetBytes / count);
        fitTarget(
            path, options, true, [](int, const QString &) {},
            [&](int quality, Progress) {
                QImageWriter writer(path, "jpeg");
                writer.setQuality(quality);
                writer.setOptimizedWrite(true);
                if (!writer.write(image))
                    fail(writer.errorString());
            });
        QImageReader check(path);
        if (check.size() != image.size() || !check.canRead())
            fail("PDF page image validation failed");
        state.bytes += QFileInfo(path).size();
        state.progress(5 + (page + 1) * 90 / count, "Exporting PDF pages");
        return 1;
    }
    catch (const std::exception &e)
    {
        state.error = QString::fromUtf8(e.what());
        return 0;
    }
}
static void imagesPdf(const QStringList &sources, const QString &destination, const Options &o, Progress progress)
{
    Scratch temp(QFileInfo(destination).absolutePath() + "/pdf-images-XXXXXX");
    QVector<QByteArray> paths;
    QStringList prepared;
    for (int i = 0; i < sources.size(); i++)
    {
        Frames frames = readFrames(sources[i]);
        if (frames.images.size() != 1)
            fail("Animated images require a still frame before PDF conversion");
        QString path =
            temp.path() + QString("/page-%1.%2").arg(i).arg(o.convertOnly || o.imageLossless ? "png" : "jpg");
        if (o.convertOnly || o.imageLossless)
        {
            if (!frames.images[0].save(path, "PNG"))
                fail("Cannot prepare PDF image");
        }
        else
        {
            QImage opaque(frames.images[0].size(), QImage::Format_RGB888);
            opaque.fill(Qt::white);
            QPainter painter(&opaque);
            painter.drawImage(0, 0, frames.images[0]);
            painter.end();
            QImageWriter writer(path, "jpeg");
            writer.setQuality(o.quality);
            writer.setOptimizedWrite(true);
            if (!writer.write(opaque))
                fail(writer.errorString());
        }
        prepared << path;
        paths << path.toUtf8();
        progress(5 + (i + 1) * 40 / sources.size(), "Preparing PDF pages");
    }
    QVector<const char *> raw;
    for (const auto &path : paths)
        raw << path.constData();
    PdfState state;
    state.progress = progress;
    char error[1024]{};
    auto dst = destination.toUtf8();
    if (!athanor_pdf_from_images(raw.constData(), raw.size(), dst.constData(), pdfProgress, &state, error,
                                 sizeof(error)))
        fail(QString::fromUtf8(error));
    PdfState check;
    if (athanor_pdf_snapshot(dst.constData(), pdfRecord, &check, error, sizeof(error)) != sources.size())
        fail("Combined PDF page validation failed");
}

QJsonObject Conversion::run(const QJsonObject &spec, Progress progress)
{
    QString source = spec.value("source").toString();
    Options o = Options::fromJson(spec.value("options").toObject());
    if (o.convertOnly)
    {
        o.sizeMode = false;
        o.imageLossless = true;
        o.videoLossless = true;
        o.pdf = "lossless";
    }
    if (o.convertOnly && (o.image == "jpg" || o.pdfOutput == "jpg"))
        o.quality = 100;
    QString category = kind(source);
    QString originalCategory = spec.value("original_category").toString(category);
    if (originalCategory == "image" && category == "pdf")
        fail("The original image was deleted. Add the saved PDF to the queue to export its pages.");
    if (originalCategory == "video" && category == "audio")
    {
        if (!QStringList{"opus", "mp3", "wav"}.contains(o.video))
            fail("The original video was deleted. Its extracted audio cannot restore the video frames.");
        o.audio = o.video;
    }

    QFileInfo original(source);
    if (!original.isFile() || category.isEmpty())
        fail("Unsupported or missing input file");
    qint64 before = original.size();
    auto modified = original.lastModified();
    bool preview = spec.value("preview").toBool();
    if (preview)
        o.sizeMode = false;
    if (o.sizeMode)
    {
        o.imageLossless = false;
        o.videoLossless = false;
        if (o.pdf == "lossless")
            o.pdf = "balanced";
    }
    if (o.sizeMode && o.targetBytes <= 0)
        fail("Enter a positive target file size");
    QString extension = category == "pdf"     ? o.pdfOutput
                        : category == "image" ? o.image
                        : category == "audio" ? o.audio
                                              : o.video;
    if ((category == "image" &&
         !QStringList{"avif", "webp", "heic", "heif", "jpg", "png", "ico", "pdf"}.contains(extension)) ||
        (category == "video" &&
         !QStringList{"webm", "mkv", "av1", "mp4", "gif", "opus", "mp3", "wav"}.contains(extension)) ||
        (category == "audio" && !QStringList{"opus", "mp3", "wav"}.contains(extension)) ||
        (category == "pdf" && !QStringList{"pdf", "jpg"}.contains(extension)))
        fail("Unsupported output format");
    QString folder = preview              ? spec.value("scratch").toString()
                     : o.output.isEmpty() ? original.absolutePath()
                                          : o.output;
    if (!QDir().mkpath(folder))
        fail("Output folder cannot be created");
    std::unique_ptr<Scratch> staging;
    QString stagingRoot = spec.value("scratch").toString();
    if (preview || stagingRoot.isEmpty())
    {
        staging = std::make_unique<Scratch>(folder + "/.athanor-XXXXXX");
        if (!staging->isValid())
            fail("Output folder is not writable");
        stagingRoot = staging->path();
    }
    else
    {
        QString absolute = QDir::cleanPath(QFileInfo(stagingRoot).absoluteFilePath());
        QString parent = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
        if (!absolute.startsWith(parent + '/', Qt::CaseInsensitive) || !QFileInfo(absolute).isDir() ||
            QFileInfo(absolute).isSymLink())
            fail("Invalid worker staging folder");
        stagingRoot = absolute;
    }
    QString stage = stagingRoot + "/output." + extension;
    QStringList sources{source};
    if (category == "image" && extension == "pdf" && spec.contains("sources"))
    {
        sources.clear();
        QSet<QString> unique;
        for (auto value : spec.value("sources").toArray())
        {
            QString path = value.toString();
            QString key = QFileInfo(path).canonicalFilePath();
            if (!QFileInfo(path).isFile() || kind(path) != "image" || key.isEmpty())
                fail("Invalid PDF input image");
#ifdef Q_OS_WIN
            key = key.toLower();
#endif
            if (!unique.contains(key))
            {
                unique.insert(key);
                sources << path;
            }
        }
        if (sources.isEmpty())
            fail("Add images to create a PDF");
    }
    QVector<qint64> originalSizes;
    QVector<QDateTime> originalTimes;
    before = 0;
    for (const auto &path : sources)
    {
        QFileInfo f(path);
        originalSizes << f.size();
        originalTimes << f.lastModified();
        before += f.size();
    }
    bool collection = category == "pdf" && extension == "jpg";
    qint64 collectionBytes = 0;
    if (collection)
    {
        stage = stagingRoot + "/pages";
        if (!QDir().mkpath(stage))
            fail("Cannot prepare PDF page collection");
        PageExport state{stage, {}, o, progress};
        char error[1024]{};
        auto src = source.toUtf8();
        if (athanor_pdf_render(src.constData(), o.pdfDpi, exportPage, &state, error, sizeof(error)) < 0)
            fail(state.error.isEmpty() ? QString::fromUtf8(error) : state.error);
        collectionBytes = state.bytes;
    }
    else if (category == "image" && extension == "pdf")
    {
        Options trial = o;
        fitTarget(stage, o, true, progress, [&](int quality, Progress report) {
            trial.quality = quality;
            imagesPdf(sources, stage, trial, report);
        });
    }
    else if (category == "image")
        image(source, stage, o, progress, preview ? 1024 : 0);
    else if (category == "audio" || (category == "video" && QStringList{"opus", "mp3", "wav"}.contains(extension)))
    {
        Options encoding = o;
        encoding.audio = extension;
        audio(source, stage, encoding, progress);
    }
    else if (category == "video" && (extension == "mp4" || extension == "gif"))
        compatibleVideo(source, stage, o, progress, preview);
    else if (category == "video")
    {
        Options encoding = o;
        fitTarget(stage, o, false, progress, [&](int crf, Progress report) {
            encoding.crf = crf;
            QString encoder = video(source, stage, encoding, report, preview, false);
            if (encoder == "libsvtav1")
                encoding.acceleration = "CPU";
        });
        validateVideo(source, stage, encoding, progress, preview);
    }
    else
        pdf(source, stage, o, progress);
    if ((collection ? collectionBytes : QFileInfo(stage).size()) <= 0)
        fail("Encoder produced an empty output");

    QString baseName = spec.value("stem").toString(original.completeBaseName());
    if (baseName.contains('/') || baseName.contains('\\'))
        fail("Invalid output name");
    QString stem = preview ? "preview" : baseName + "_athanor-compressed";
    QString destination;
    int suffix = 0;
    do
    {
        destination = folder + '/' + stem + (suffix ? "-" + QString::number(suffix) : QString()) +
                      (collection ? QString() : '.' + extension);
        suffix++;
    } while (!Platform::publish(stage, destination) && QFileInfo::exists(destination));
    if (!QFileInfo::exists(destination))
        fail("Cannot publish validated output");
    qint64 after = collection ? collectionBytes : QFileInfo(destination).size();
    QString warning;
    if (collection)
        warning = "PDF pages exported as images; text is rasterized";
    if (category == "image" && extension == "ico")
        warning = "Icon fitted to a 256 × 256 canvas";
    if (category == "video" && extension == "gif")
        warning = "GIF uses up to 256 colors and binary transparency; audio is omitted";
    if (o.sizeMode && after > o.targetBytes)
        warning = QString("Target too small; saved smallest valid result (%1 bytes)").arg(after);
    if (category == "video" && (o.video == "av1" || o.video == "mp4") && videoHasAlpha(source))
        warning += (warning.isEmpty() ? QString() : QString(" · ")) + "Transparency flattened for " + o.video.toUpper();
    if (!preview && category == "image" && o.image == "webp")
    {
        QImageReader reader(source);
        if (reader.size().width() > 16383 || reader.size().height() > 16383)
            warning = "Resized to fit WebP's 16,383-pixel limit";
    }
    if (!preview && o.deleteOriginal)
        for (int i = 0; i < sources.size(); i++)
        {
            QFileInfo now(sources[i]);
            if (now.size() != originalSizes[i] || now.lastModified() != originalTimes[i])
                warning += (warning.isEmpty() ? QString() : QString(" · ")) +
                           "Original changed during conversion and was kept";
            else if (!QFile::remove(sources[i]))
                warning += (warning.isEmpty() ? QString() : QString(" · ")) +
                           "Converted successfully; original could not be deleted";
        }
    if (preview && category == "video" && !QStringList{"opus", "mp3", "wav"}.contains(extension))
    {
        QString poster = folder + "/preview-poster.png";
        QStringList decode = {"-nostdin", "-hide_banner", "-v", "error", "-y"};
        decode << decoderArguments(destination) << "-i" << destination << "-frames:v" << "1" << poster;
        auto r = process(tool("ffmpeg"), decode);
        if (r.code)
            fail(QString::fromUtf8(r.error));
        destination = poster;
    }
    progress(100, "Done");
    return {{"ok", true},       {"source", source}, {"output", destination},
            {"before", before}, {"after", after},   {"warning", warning}};
}

void Conversion::writeLine(const QJsonObject &obj)
{
    Platform::print(QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n');
}

int Conversion::worker(const QString &job)
{
    QFile file(job);
    if (!file.open(QIODevice::ReadOnly))
    {
        writeLine({{"ok", false}, {"error", "Cannot open worker job"}});
        return 1;
    }
    try
    {
        auto result = run(QJsonDocument::fromJson(file.readAll()).object(),
                          [last = -1, label = QString()](int p, const QString &stage) mutable {
                              if (p != last || stage != label)
                              {
                                  last = p;
                                  label = stage;
                                  writeLine({{"percent", p}, {"stage", stage}});
                              }
                          });
        writeLine(result);
        return 0;
    }
    catch (const std::exception &e)
    {
        writeLine({{"ok", false}, {"error", QString::fromUtf8(e.what())}});
        return 1;
    }
}
