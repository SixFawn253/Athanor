#pragma once
#include <QImage>
#include <QJsonObject>
#include <QStringList>
#include <functional>
struct Options
{
    QString image = "avif", video = "webm", audio = "opus", pdfOutput = "pdf", pdf = "balanced", speed = "Smallest",
            acceleration = "Auto", output;
    int audioBitrate = 96, pdfDpi = 150;
    int quality = 75, crf = 32, threads = 0, batchWorkers = 2;
    bool autoWorkers = true;
    bool deleteOriginal = false, resizeWebp = false, sizeMode = false, convertOnly = false, imageLossless = false,
         videoLossless = false;
    qint64 targetBytes = 2000000;
    QJsonObject json() const;
    static Options fromJson(const QJsonObject &);
};
using Progress = std::function<void(int, const QString &)>;
namespace Conversion
{
QString kind(const QString &);
QStringList imageExtensions();
QStringList videoExtensions();
QStringList audioExtensions();
QString supportedFilter();
QImage readImage(const QString &, int frame = 0);
QJsonObject run(const QJsonObject &, Progress);
void writeLine(const QJsonObject &);
int worker(const QString &);
} // namespace Conversion
