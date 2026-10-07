#pragma once
#include <QStringList>
namespace UnixUpdate
{
bool canInstall();
int apply(const QStringList &arguments);
}
