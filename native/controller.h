#pragma once
#include "childprocess.h"
#include "conversion.h"
#include "scratch.h"
#include "hardwareload.h"
#include <QElapsedTimer>
#include <QAbstractListModel>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <memory>
struct QueueItem
{
    QString groupRoot;
    QString source, category, status = "Queued", output, warning, completedKey;
    int progress = 0, width = 0, height = 0;
    double duration = 0;
    qint64 before = 0, after = -1;
};
class QueueModel : public QAbstractListModel
{
    Q_OBJECT
  public:
    enum Roles
    {
        Source = Qt::UserRole + 1,
        Name,
        Category,
        Status,
        ProgressRole,
        Output,
        Warning,
        Width,
        Height,
        FileSize,
        CompressedSize,
        Smaller,
        Gained,
        FolderGroup,
        FolderSection
    };
    QVector<QueueItem> items;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : items.size();
    }
    QVariant data(const QModelIndex &, int) const override;
    QHash<int, QByteArray> roleNames() const override;
    void add(const QVector<QueueItem> &);
    void update(int);
    void remove(int);
    void clear();
};
class Controller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QueueModel *queue READ queue CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QStringList queueCategories READ queueCategories NOTIFY changed)
    Q_PROPERTY(bool hasFolders READ hasFolders NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canConvert READ canConvert NOTIFY changed)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY changed)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY changed)
    Q_PROPERTY(bool oversizedWebp READ oversizedWebp NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY themeChanged)
    Q_PROPERTY(QString appearance READ appearance WRITE setAppearance NOTIFY themeChanged)
    Q_PROPERTY(bool contextMenus READ contextMenus WRITE setContextMenus NOTIFY changed)
    Q_PROPERTY(QJsonObject options READ options NOTIFY changed)
    Q_PROPERTY(bool quick READ quick CONSTANT)
    Q_PROPERTY(QString previewOriginal READ previewOriginal NOTIFY previewChanged)
    Q_PROPERTY(QString previewResult READ previewResult NOTIFY previewChanged)
    Q_PROPERTY(bool previewBusy READ previewBusy NOTIFY previewChanged)
    Q_PROPERTY(QString previewError READ previewError NOTIFY previewChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)
    Q_PROPERTY(QString motion READ motion WRITE setMotion NOTIFY changed)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY changed)
    Q_PROPERTY(QJsonObject queueSummary READ queueSummary NOTIFY changed)
    Q_PROPERTY(QString outputError READ outputError NOTIFY changed)
    Q_PROPERTY(QString conversionWarning READ conversionWarning NOTIFY changed)
    Q_PROPERTY(QString firstCategory READ firstCategory NOTIFY changed)
  public:
    explicit Controller(bool quick = false, QObject *parent = nullptr);
    QString version() const;
    QueueModel *queue()
    {
        return &model;
    }
    int count() const
    {
        return model.items.size();
    }
    bool busy() const
    {
        return active > 0 || running;
    }
    bool canConvert() const;
    bool needsConversion(const QueueItem &) const;
    bool hasImage() const;
    bool hasVideo() const;
    bool oversizedWebp() const;
    bool dark() const;
    QString appearance() const
    {
        return theme;
    }
    void setAppearance(const QString &);
    bool contextMenus() const
    {
        return contexts;
    }
    void setContextMenus(bool);
    QJsonObject options() const
    {
        return opts.json();
    }
    bool quick() const
    {
        return compact;
    }
    QString previewOriginal() const
    {
        return previewBefore;
    }
    QString previewResult() const
    {
        return previewAfter;
    }
    bool previewBusy() const
    {
        return previewRunning;
    }
    QString previewError() const
    {
        return previewFailure;
    }
    QString statusMessage() const
    {
        return message;
    }
    QString firstCategory() const
    {
        return model.items.isEmpty() ? QString("image") : model.items[0].category;
    }
    QString motion() const
    {
        return motionPreference;
    }
    void setMotion(const QString &);
    bool reducedMotion() const;
    QJsonObject queueSummary() const;
    QString outputError() const;
    Q_INVOKABLE QJsonObject rowInfo(int) const;
    Q_INVOKABLE void openFolder(int);
    Q_INVOKABLE void setOption(const QString &, const QVariant &);
    Q_INVOKABLE void addFiles();
    Q_INVOKABLE void addFolder(bool recursive);
    Q_INVOKABLE int groupCount(const QString &) const;
    Q_INVOKABLE QJsonObject folderSectionInfo(const QString &) const;
    QStringList queueCategories() const;
    bool hasFolders() const;
    Q_INVOKABLE void addUrls(const QList<QUrl> &, bool recursive = false);
    Q_INVOKABLE void remove(int);
    Q_INVOKABLE void clear();
    QString conversionWarning() const;
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void chooseOutput();
    Q_INVOKABLE void openOutput(int);
    Q_INVOKABLE void preview(const QString &, int, int selected = -1);
    Q_INVOKABLE void closePreview();
    Q_INVOKABLE void usePreview(const QString &, int);
    Q_INVOKABLE void updatePreview(int);
    Q_INVOKABLE void transitionDeadline()
    {
        revealDeadline.start(400);
    }
    void addPaths(const QStringList &, bool recursive = false);
    void setTarget(const QString &);
  signals:
    void changed();
    void themeChanged();
    void batchFinished(int success, int failed, int cancelled);
    void jobStarted(int row);
    void previewChanged();
    void previewOpened(const QString &, int);
    void filesAdded();
    void transitionExpired();

  private:
    QueueModel model;
    Options opts;
    QString theme = "System", message, motionPreference = "System";
    bool systemMotion = true;
    bool contexts = true, compact = false, running = false, cancelling = false;
    int active = 0, successes = 0, failures = 0, cancelled = 0;
    QVector<ChildProcess *> processes;
    QSet<QString> seen;
    QHash<int, QVector<int>> pdfGroups;
    QSet<int> pdfMembers;
    QTimer systemTimer, revealDeadline, batchTimer;
    QElapsedTimer batchElapsed;
    HardwareMonitor hardwareMonitor;
    HardwareLoad hardwareLoad;
    int workerLimit = 1, headroomSamples = 0;
    QSet<int> batchRows;
    QHash<int, qint64> gpuProcesses;
    QSet<int> nvencRows;
    void updateBatchLoad();
    QString settingsPath;
    bool systemTheme = false;
    void save();
    void schedule();
    int nextJob() const;
    double estimatedWork(int row) const;
    void launch(int);
    void finishBatch();
    ChildProcess *previewProcess = nullptr;
    std::unique_ptr<Scratch> previewDirectory;
    QString previewBefore, previewAfter, previewFailure, previewSource, previewCategory;
    bool previewRunning = false;
    int queueGeneration = 0;
    int previewGeneration = 0;
    void launchPreview(const QString &, int, int);
};
