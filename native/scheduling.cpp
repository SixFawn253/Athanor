#include "scheduling.h"
#include <algorithm>
#include <cmath>
#include <limits>

double Scheduling::work(const QString &category, qint64 bytes, int width, int height, double duration, const Options &opts)
{
    const double megabytes = std::max(.01, double(bytes) / 1000000);
    const double pixels = std::max(.01, double(std::max(0, width)) * std::max(0, height) / 1000000);
    double estimate = megabytes;
    if (category == "image")
        estimate = width > 0 && height > 0 ? pixels : megabytes;
    else if (category == "video")
        estimate = duration > 0 && std::isfinite(duration) ? duration * std::max(.1, pixels) : megabytes * 20;
    else if (category == "audio")
        estimate = duration > 0 && std::isfinite(duration) ? duration * .02 : megabytes * .25;
    else if (category == "pdf")
        estimate = megabytes * 2;
    // These are relative cost estimates, not promised conversion durations.
    if (category == "image" && QStringList{"avif", "heic", "heif"}.contains(opts.image))
        estimate *= 4;
    if (category == "video" && QStringList{"mp3", "opus", "wav"}.contains(opts.video))
        estimate = duration > 0 && std::isfinite(duration) ? duration * .02 : megabytes * .25;
    if (opts.speed == "Smallest")
        estimate *= 3;
    else if (opts.speed == "Balanced")
        estimate *= 1.5;
    if (opts.sizeMode)
        estimate *= 3;
    return std::max(.01, estimate);
}
int Scheduling::shortestJob(const QVector<QPair<int, double>> &candidates)
{
    int row = -1;
    double best = std::numeric_limits<double>::infinity();
    for (const auto &candidate : candidates)
        if (candidate.second < best || (candidate.second == best && (row < 0 || candidate.first < row)))
        {
            row = candidate.first;
            best = candidate.second;
        }
    return row;
}
