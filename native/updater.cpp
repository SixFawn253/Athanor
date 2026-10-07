#include "updater.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QVersionNumber>
#include <QUuid>
#include <memory>
static const QString releaseApi = "https://api.github.com/repos/SixFawn253/Athanor/releases/latest";
static bool trustedAsset(const QUrl &url)
{
    return url.scheme() == "https" && url.host() == "github.com" && url.userInfo().isEmpty() &&
           url.path().startsWith("/SixFawn253/Athanor/releases/download/");
}
static QNetworkRequest request(const QUrl &url)
{
    QNetworkRequest r(url);
    r.setRawHeader("User-Agent", "Athanor/" + QCoreApplication::applicationVersion().toUtf8());
    r.setRawHeader("Accept", "application/vnd.github+json");
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(60000);
    return r;
}
Updater::Updater(QObject *owner) : QObject(owner), controller(owner)
{
    auto folder = qEnvironmentVariable("ATHANOR_SETTINGS_DIR", QCoreApplication::applicationDirPath());
    preferences = folder + "/updates.ini";
    QDir downloads(folder + "/updates");
    for (const auto &name : downloads.entryList({"update-*.exe"}, QDir::Files))
        if (QRegularExpression("^update-[0-9a-f]{32}\\.exe$").match(name).hasMatch())
            QFile::remove(downloads.filePath(name));
    autoCheck = QSettings(preferences, QSettings::IniFormat).value("automatic", true).toBool();
    message = "Athanor " + QCoreApplication::applicationVersion();
    timer.setInterval(6 * 60 * 60 * 1000);
    connect(&timer, &QTimer::timeout, this, [this] {
        if (autoCheck)
            check();
    });
    if (!qEnvironmentVariableIsSet("ATHANOR_TEST"))
    {
        timer.start();
        if (autoCheck)
            QTimer::singleShot(2500, this, &Updater::check);
    }
}
bool Updater::canInstall() const
{
#ifdef Q_OS_WIN
    return !pending && !asset.isEmpty() && !qEnvironmentVariable("ATHANOR_LAUNCHER_PATH").isEmpty();
#else
    return false;
#endif
}
void Updater::setAutomatic(bool value)
{
    autoCheck = value;
    QSettings(preferences, QSettings::IniFormat).setValue("automatic", value);
    emit changed();
}
void Updater::check()
{
    if (pending)
        return;
    pending = true;
    percent = 0;
    message = "Checking GitHubâ€¦";
    emit changed();
    QUrl endpoint(releaseApi);
    if (qEnvironmentVariableIsSet("ATHANOR_TEST") && qEnvironmentVariableIsSet("ATHANOR_TEST_UPDATE_API"))
        endpoint = QUrl(qEnvironmentVariable("ATHANOR_TEST_UPDATE_API"));
    auto reply = network.get(request(endpoint));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        pending = false;
        auto bytes = reply->readAll();
        auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError)
            message = code == 403 || code == 429 ? "GitHubâ€™s request limit was reached. Try again later."
                                                 : "Couldnâ€™t check for updates. Try again when connected.";
        else
        {
            QJsonParseError error;
            auto release = QJsonDocument::fromJson(bytes, &error).object();
            QString tag = release.value("tag_name").toString();
            QRegularExpression pattern("^(?:alpha-|v)?(\\d+\\.\\d+(?:\\.\\d+)?)$");
            auto match = pattern.match(tag);
            if (error.error || !match.hasMatch() || release.value("draft").toBool() ||
                release.value("prerelease").toBool())
                message = "GitHub returned an unsupported release.";
            else if (QVersionNumber::fromString(match.captured(1)) <=
                     QVersionNumber::fromString(QCoreApplication::applicationVersion()))
            {
                asset = {};
                version.clear();
                message = "Athanor is up to date.";
            }
            else
            {
                asset = {};
                version.clear();
                QString name = "Athanor-Alpha-" + match.captured(1) + "-Windows-x64.exe";
                for (auto value : release.value("assets").toArray())
                {
                    auto a = value.toObject();
                    if (a.value("name").toString() == name && a.value("state").toString() == "uploaded" &&
                        a.value("size").toInteger() > 0 && a.value("size").toInteger() < 2000000000 &&
                        QRegularExpression("^sha256:[0-9a-fA-F]{64}$").match(a.value("digest").toString()).hasMatch() &&
                        trustedAsset(QUrl(a.value("browser_download_url").toString())))
                    {
                        asset = a;
                        version = release.value("name").toString(tag);
                        break;
                    }
                }
                message =
                    asset.isEmpty() ? "This release has no verified Windows download yet." : version + " is available.";
                if (!asset.isEmpty() && qEnvironmentVariable("ATHANOR_LAUNCHER_PATH").isEmpty())
                    message += " Run the single-file portable edition to install updates.";
            }
        }
        reply->deleteLater();
        emit changed();
    });
}
void Updater::install()
{
    if (!canInstall())
        return;
    if (controller->property("busy").toBool() || controller->property("previewBusy").toBool())
    {
        message = "Finish the current conversion or preview before updating.";
        emit changed();
        return;
    }
    QString folder = QFileInfo(preferences).absolutePath() + "/updates";
    if (!QDir().mkpath(folder))
    {
        message = "The update folder is not writable.";
        emit changed();
        return;
    }
    auto file = std::make_shared<QFile>(folder + "/update-" + QUuid::createUuid().toString(QUuid::Id128) + ".exe");
    if (!file->open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        message = "Cannot save the update. Check free disk space.";
        emit changed();
        return;
    }
    pending = true;
    percent = 0;
    message = "Downloading " + version + "â€¦";
    emit changed();
    QUrl download(asset.value("browser_download_url").toString());
    if (qEnvironmentVariableIsSet("ATHANOR_TEST") && qEnvironmentVariableIsSet("ATHANOR_TEST_UPDATE_DOWNLOAD"))
        download = QUrl(qEnvironmentVariable("ATHANOR_TEST_UPDATE_DOWNLOAD"));
    auto reply = network.get(request(download));
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto count = std::make_shared<qint64>(0);
    qint64 expected = asset.value("size").toInteger();
    auto consume = [reply, file, hash, count, expected] {
        auto bytes = reply->readAll();
        *count += bytes.size();
        if (*count > expected || file->write(bytes) != bytes.size())
        {
            reply->abort();
            return;
        }
        hash->addData(bytes);
    };
    connect(reply, &QNetworkReply::readyRead, this, consume);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, expected](qint64 done, qint64) {
        percent = int(qBound(qint64(0), done * 100 / expected, qint64(100)));
        emit changed();
    });
    QString digest = asset.value("digest").toString().mid(7).toLower();
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, hash, count, expected, digest, consume] {
        consume();
        bool flushed = file->flush();
        file->close();
        pending = false;
        bool valid = reply->error() == QNetworkReply::NoError && flushed && *count == expected &&
                     hash->result().toHex() == digest.toLatin1();
        reply->deleteLater();
        if (!valid)
        {
            file->remove();
            message = "The update could not be verified. Your current app is unchanged.";
            emit changed();
            return;
        }
        if (controller->property("busy").toBool() || controller->property("previewBusy").toBool())
        {
            file->remove();
            message = "Finish the current job, then download the update again.";
            emit changed();
            return;
        }
        QString launcher = qEnvironmentVariable("ATHANOR_LAUNCHER_PATH");
        QStringList args{"--apply-update", launcher, QString::number(QCoreApplication::applicationPid()),
                         qEnvironmentVariable("ATHANOR_LAUNCHER_PID", "0"), digest};
        if (!QProcess::startDetached(file->fileName(), args, QFileInfo(launcher).absolutePath()))
        {
            file->remove();
            message = "Couldnâ€™t start the update. Your current app is unchanged.";
            emit changed();
            return;
        }
        message = "Restarting Athanorâ€¦";
        emit changed();
        QCoreApplication::quit();
    });
}
