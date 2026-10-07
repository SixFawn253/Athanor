#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QTimer>
class Updater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString portableUrl READ portableUrl NOTIFY changed)
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY changed)
    Q_PROPERTY(bool working READ working NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
  public:
    explicit Updater(QObject *controller);
    QString status() const
    {
        return message;
    }
    QString availableVersion() const
    {
        return version;
    }
    QString portableUrl() const
    {
        return asset.value("browser_download_url").toString();
    }
    Q_INVOKABLE void openPortableDownload();
    bool working() const
    {
        return pending;
    }
    bool downloading() const
    {
        return downloadActive;
    }
    bool automatic() const
    {
        return autoCheck;
    }
    bool canInstall() const;
    int progress() const
    {
        return percent;
    }
    void setAutomatic(bool);
    Q_INVOKABLE void check();
    Q_INVOKABLE void install();
  signals:
    void changed();

  private:
    QObject *controller;
    QNetworkAccessManager network;
    QTimer timer;
    QString message, version, preferences;
    QJsonObject asset;
    bool pending = false, autoCheck = true, downloadActive = false;
    int percent = 0;
};
