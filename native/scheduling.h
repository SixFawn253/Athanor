#pragma once
#include "conversion.h"
#include <QPair>
#include <QVector>
namespace Scheduling
{
double work(const QString &category, qint64 bytes, int width, int height, double duration, const Options &);
int shortestJob(const QVector<QPair<int, double>> &candidates);
}
