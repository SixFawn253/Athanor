#include "childprocess.h"
#include <QElapsedTimer>
#include <QMetaObject>
#ifdef Q_OS_WIN
#include <windows.h>
static QString quote(QString s)
{
    if (!s.isEmpty() && !s.contains(' ') && !s.contains('\t') && !s.contains('"'))
        return s;
    QString result = "\"";
    int backslashes = 0;
    for (auto c : s)
    {
        if (c == '\\')
        {
            backslashes++;
            continue;
        }
        if (c == '"')
        {
            result += QString(backslashes * 2 + 1, '\\');
            result += c;
        }
        else
        {
            result += QString(backslashes, '\\');
            result += c;
        }
        backslashes = 0;
    }
    result += QString(backslashes * 2, '\\');
    return result + '"';
}
static QString windowsError()
{
    wchar_t *message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   GetLastError(), 0, (wchar_t *)&message, 0, nullptr);
    QString text = message ? QString::fromWCharArray(message).trimmed() : QString("Windows process operation failed");
    if (message)
        LocalFree(message);
    return text;
}
#else
#include <signal.h>
#include <unistd.h>
#endif
ChildProcess::ChildProcess(QObject *parent) : QObject(parent)
{
#ifndef Q_OS_WIN
    process = new QProcess(this);
    connect(process, &QProcess::started, this, &ChildProcess::started);
    connect(process, &QProcess::readyReadStandardOutput, this, &ChildProcess::readyReadStandardOutput);
    connect(process, &QProcess::readyReadStandardError, this, &ChildProcess::readyReadStandardError);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &ChildProcess::finished);
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        failure = process->errorString();
        emit errorOccurred(e);
    });
#endif
}
ChildProcess::~ChildProcess()
{
#ifdef Q_OS_WIN
    if (processState != QProcess::NotRunning)
        kill();
    if (monitor.joinable())
        monitor.join();
    if (jobHandle)
        CloseHandle((HANDLE)jobHandle);
    if (processHandle)
        CloseHandle((HANDLE)processHandle);
#else
    if (process->state() != QProcess::NotRunning)
    {
        kill();
        process->waitForFinished(1000);
    }
#endif
}
void ChildProcess::start()
{
#ifdef Q_OS_WIN
    HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    auto closePipes = [&] {
        for (auto h : {outRead, outWrite, errRead, errWrite})
            if (h)
                CloseHandle(h);
    };
    if (!CreatePipe(&outRead, &outWrite, &security, 0) || !CreatePipe(&errRead, &errWrite, &security, 0))
    {
        failure = windowsError();
        closePipes();
        emit errorOccurred(QProcess::FailedToStart);
        return;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW info{};
    info.cb = sizeof(info);
    info.dwFlags = STARTF_USESTDHANDLES;
    info.hStdInput = input;
    info.hStdOutput = outWrite;
    info.hStdError = errWrite;
    PROCESS_INFORMATION pi{};
    QString command = quote(program);
    for (const auto &arg : arguments)
        command += ' ' + quote(arg);
    std::wstring native = command.toStdWString();
    BOOL ok = CreateProcessW((LPCWSTR)program.utf16(), native.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &info, &pi);
    if (input != INVALID_HANDLE_VALUE)
        CloseHandle(input);
    if (!ok)
    {
        failure = windowsError();
        closePipes();
        emit errorOccurred(QProcess::FailedToStart);
        return;
    }
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    outWrite = errWrite = nullptr;
    processHandle = pi.hProcess;
    pid = pi.dwProcessId;
    processState = QProcess::Running;
    if (workerGroup)
    {
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
            AssignProcessToJobObject(job, pi.hProcess))
            jobHandle = job;
        else if (job)
            CloseHandle(job);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    emit started();
    monitor = std::thread([this, outRead, errRead] {
        auto reader = [this](HANDLE handle, bool errors) {
            char bytes[8192];
            DWORD count = 0;
            while (ReadFile(handle, bytes, sizeof(bytes), &count, nullptr) && count)
            {
                {
                    QMutexLocker guard(&mutex);
                    (errors ? error : output).append(bytes, count);
                    available.wakeAll();
                }
                QMetaObject::invokeMethod(
                    this,
                    [this, errors] {
                        if (errors)
                            emit readyReadStandardError();
                        else
                            emit readyReadStandardOutput();
                    },
                    Qt::QueuedConnection);
            }
            CloseHandle(handle);
        };
        std::thread out(reader, outRead, false), err(reader, errRead, true);
        WaitForSingleObject((HANDLE)processHandle, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess((HANDLE)processHandle, &code);
        out.join();
        err.join();
        result = int(code);
        abnormal = code >= 0xc0000000;
        processState = QProcess::NotRunning;
        {
            QMutexLocker guard(&mutex);
            available.wakeAll();
        }
        QMetaObject::invokeMethod(this, [this] { emit finished(result, exitStatus()); }, Qt::QueuedConnection);
    });
#else
    process->setProgram(program);
    process->setArguments(arguments);
    if (workerGroup)
        process->setChildProcessModifier([] { setsid(); });
    process->start();
#endif
}
bool ChildProcess::waitForStarted(int timeout) const
{
#ifdef Q_OS_WIN
    Q_UNUSED(timeout);
    return processHandle != nullptr;
#else
    return process->waitForStarted(timeout);
#endif
}
bool ChildProcess::waitForReadyRead(int timeout)
{
#ifdef Q_OS_WIN
    QMutexLocker guard(&mutex);
    if (!output.isEmpty())
        return true;
    if (processState == QProcess::NotRunning)
        return false;
    available.wait(&mutex, timeout);
    return !output.isEmpty();
#else
    return process->waitForReadyRead(timeout);
#endif
}
bool ChildProcess::waitForFinished(int timeout)
{
#ifdef Q_OS_WIN
    QElapsedTimer timer;
    timer.start();
    while (processState != QProcess::NotRunning)
    {
        QMutexLocker guard(&mutex);
        available.wait(&mutex, 50);
        if (timeout >= 0 && timer.elapsed() >= timeout)
            return false;
    }
    return true;
#else
    return process->waitForFinished(timeout);
#endif
}
QByteArray ChildProcess::readAllStandardOutput()
{
#ifdef Q_OS_WIN
    QMutexLocker guard(&mutex);
    QByteArray bytes;
    bytes.swap(output);
    return bytes;
#else
    return process->readAllStandardOutput();
#endif
}
QByteArray ChildProcess::readAllStandardError()
{
#ifdef Q_OS_WIN
    QMutexLocker guard(&mutex);
    QByteArray bytes;
    bytes.swap(error);
    return bytes;
#else
    return process->readAllStandardError();
#endif
}
QProcess::ProcessState ChildProcess::state() const
{
#ifdef Q_OS_WIN
    return QProcess::ProcessState(processState.load());
#else
    return process->state();
#endif
}
QProcess::ExitStatus ChildProcess::exitStatus() const
{
#ifdef Q_OS_WIN
    return abnormal ? QProcess::CrashExit : QProcess::NormalExit;
#else
    return process->exitStatus();
#endif
}
int ChildProcess::exitCode() const
{
#ifdef Q_OS_WIN
    return result;
#else
    return process->exitCode();
#endif
}
qint64 ChildProcess::processId() const
{
#ifdef Q_OS_WIN
    return pid;
#else
    return process->processId();
#endif
}
void ChildProcess::kill()
{
#ifdef Q_OS_WIN
    if (jobHandle)
        TerminateJobObject((HANDLE)jobHandle, 1);
    else if (processHandle)
        TerminateProcess((HANDLE)processHandle, 1);
#else
    if (workerGroup && processId() > 0)
        ::kill(-pid_t(processId()), SIGKILL);
    else
        process->kill();
#endif
}
