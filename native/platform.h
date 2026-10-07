#pragma once
#include "childprocess.h"
#include <QString>
namespace Platform
{
void setupProcess(ChildProcess *, bool workerGroup = false);
void cancelProcess(ChildProcess *);
bool publish(const QString &, const QString &);
bool contextMenuEnabled(const QString &);
bool contextMenu(bool, const QString &, QString *);
void notify(const QString &, const QString &);
bool systemDark();
bool animationsEnabled();
void print(const QByteArray &);
} // namespace Platform
