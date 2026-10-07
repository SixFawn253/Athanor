#pragma once
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QWaitCondition>
#include <atomic>
#include <thread>
class ChildProcess : public QObject
{
    Q_OBJECT
  public:
    explicit ChildProcess(QObject *parent = nullptr);
    ~ChildProcess();
    void setProgram(const QString &p)
    {
        program = p;
    }
    void setArguments(const QStringList &a)
    {
        arguments = a;
    }
    void setGroup(bool group)
    {
        workerGroup = group;
    }
    void start();
    void start(const QString &p, const QStringList &a)
    {
        setProgram(p);
        setArguments(a);
        start();
    }
    bool waitForStarted(int = 10000) const;
    bool waitForReadyRead(int);
    bool waitForFinished(int);
    QByteArray readAllStandardOutput();
    QByteArray readAllStandardError();
    QProcess::ProcessState state() const;
    QProcess::ExitStatus exitStatus() const;
    int exitCode() const;
    qint64 processId() const;
    QString errorString() const
    {
        return failure;
    }
    void kill();
  signals:
    void started();
    void readyReadStandardOutput();
    void readyReadStandardError();
    void finished(int, QProcess::ExitStatus);
    void errorOccurred(QProcess::ProcessError);

  private:
    QString program, failure;
    QStringList arguments;
    bool workerGroup = false;
#ifdef Q_OS_WIN
    void *processHandle = nullptr;
    void *jobHandle = nullptr;
    std::atomic<int> processState{QProcess::NotRunning}, result{0};
    std::atomic<qint64> pid{0};
    std::atomic<bool> abnormal{false};
    QMutex mutex;
    QWaitCondition available;
    QByteArray output, error;
    std::thread monitor;
#else
    QProcess *process;
#endif
};
