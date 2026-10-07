#pragma once
#include <QDir>
#include <QFileInfo>
#include <QUuid>
class Scratch
{
    QString directory;
    bool created = false;

  public:
    explicit Scratch(QString pattern)
    {
        QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pattern.replace("XXXXXX", token);
        directory = QDir::cleanPath(pattern);
        if (!QFileInfo::exists(directory))
            created = QDir().mkpath(directory);
    }
    ~Scratch()
    {
        if (created && !directory.isEmpty() && !QFileInfo(directory).isSymLink())
            QDir(directory).removeRecursively();
    }
    Scratch(const Scratch &) = delete;
    Scratch &operator=(const Scratch &) = delete;
    QString path() const
    {
        return created ? directory : QString();
    }
    bool isValid() const
    {
        return created;
    }
};
