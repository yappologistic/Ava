#include "codexappserverclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString commandInterpreter()
{
#ifdef Q_OS_WIN
    const QString comspec = qEnvironmentVariable("COMSPEC");
    return comspec.isEmpty() ? QStringLiteral("cmd.exe") : comspec;
#else
    return {};
#endif
}

void appendUniqueExecutable(QStringList &candidates, const QString &candidate)
{
    if (candidate.trimmed().isEmpty())
        return;
    const QFileInfo info(candidate);
    const QString absolute = info.isAbsolute()
        ? info.absoluteFilePath()
        : QStandardPaths::findExecutable(candidate);
    if (absolute.isEmpty() || !QFileInfo::exists(absolute))
        return;
    if (!candidates.contains(absolute, Qt::CaseInsensitive))
        candidates.append(QDir::cleanPath(absolute));
}

} // namespace

CodexAppServerClient::CodexAppServerClient(QObject *parent)
    : QObject(parent),
      m_candidates(discoverExecutables())
{
    connect(&m_process, &QProcess::started, this, [this]() {
        m_seenProcessStart = true;
        emit runningChanged();
        clearError();
        m_initializeRequestId = request(
            QStringLiteral("initialize"),
            QJsonObject{
                {QStringLiteral("clientInfo"),
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("ava_native")},
                             {QStringLiteral("title"), QStringLiteral("Ava Chat")},
                             {QStringLiteral("version"),
                              QCoreApplication::applicationVersion().isEmpty()
                                  ? QStringLiteral("0.1.0")
                                  : QCoreApplication::applicationVersion()}}},
                {QStringLiteral("capabilities"),
                 QJsonObject{{QStringLiteral("experimentalApi"), true}}}
            });
    });

    connect(&m_process,
            &QProcess::readyReadStandardOutput,
            this,
            &CodexAppServerClient::consumeStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
        const QString text = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
        if (!text.isEmpty()) {
            m_standardErrorTail = text.right(1200);
            emit standardErrorReceived(text);
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (m_stopping)
            return;
        if (error == QProcess::FailedToStart
            && m_candidateIndex + 1 < m_candidates.size()) {
            QTimer::singleShot(0, this, [this]() {
                startCandidate(m_candidateIndex + 1);
            });
            return;
        }
        setReady(false);
        setError(QStringLiteral("Codex could not be started: %1")
                     .arg(m_process.errorString()));
    });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus status) {
        const bool stoppedIntentionally = m_stopping;
        setReady(false);
        m_pendingMethods.clear();
        m_initializeRequestId = 0;
        emit runningChanged();
        if (!stoppedIntentionally) {
            QString reason = status == QProcess::CrashExit
                ? QStringLiteral("Codex stopped unexpectedly")
                : QStringLiteral("Codex closed with exit code %1").arg(exitCode);
            if (!m_standardErrorTail.isEmpty())
                reason += QStringLiteral(": ") + m_standardErrorTail;
            setError(reason);
        }
    });

    if (qEnvironmentVariableIntValue("AVA_CODEX_DISABLE_AUTOSTART") != 1)
        QTimer::singleShot(0, this, &CodexAppServerClient::start);
}

CodexAppServerClient::~CodexAppServerClient()
{
    stop();
}

qint64 CodexAppServerClient::request(const QString &method,
                                     const QJsonObject &params)
{
    if (m_process.state() != QProcess::Running)
        return 0;
    const qint64 id = m_nextRequestId++;
    m_pendingMethods.insert(id, method);
    sendObject(QJsonObject{{QStringLiteral("id"), id},
                           {QStringLiteral("method"), method},
                           {QStringLiteral("params"), params}});
    return id;
}

void CodexAppServerClient::notify(const QString &method,
                                  const QJsonObject &params)
{
    sendObject(QJsonObject{{QStringLiteral("method"), method},
                           {QStringLiteral("params"), params}});
}

void CodexAppServerClient::respond(const QJsonValue &id,
                                   const QJsonObject &result)
{
    sendObject(QJsonObject{{QStringLiteral("id"), id},
                           {QStringLiteral("result"), result}});
}

void CodexAppServerClient::respondError(const QJsonValue &id,
                                        int code,
                                        const QString &message)
{
    sendObject(QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("error"),
         QJsonObject{{QStringLiteral("code"), code},
                     {QStringLiteral("message"), message}}}
    });
}

QStringList CodexAppServerClient::discoverExecutables()
{
    QStringList candidates;
    appendUniqueExecutable(candidates, qEnvironmentVariable("AVA_CODEX_EXECUTABLE"));

#ifdef Q_OS_WIN
    const QString userProfile = QDir::homePath();
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString npmPackage = QDir(appData).filePath(
        QStringLiteral("npm/node_modules/@openai/codex"));
    if (QFileInfo(npmPackage).isDir()) {
        QDirIterator iterator(npmPackage,
                              {QStringLiteral("codex.exe")},
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
            appendUniqueExecutable(candidates, iterator.next());
    }
    appendUniqueExecutable(candidates,
                           QDir(userProfile).filePath(QStringLiteral(".local/bin/codex.cmd")));
    appendUniqueExecutable(candidates,
                           QDir(appData).filePath(QStringLiteral("npm/codex.cmd")));
    appendUniqueExecutable(candidates,
                           QDir(localAppData).filePath(QStringLiteral("Volta/bin/codex.cmd")));
    appendUniqueExecutable(candidates,
                           QDir(localAppData).filePath(QStringLiteral("agy/bin/codex.cmd")));
    appendUniqueExecutable(candidates,
                           QDir(userProfile).filePath(QStringLiteral("bin/codex.cmd")));
    appendUniqueExecutable(candidates, QStandardPaths::findExecutable(QStringLiteral("codex.cmd")));
#endif
    appendUniqueExecutable(candidates, QStandardPaths::findExecutable(QStringLiteral("codex")));

    // Keep the protected WindowsApps executable last. Some Windows packages expose
    // a readable path that cannot be started by an unrelated desktop process.
    appendUniqueExecutable(candidates, QStandardPaths::findExecutable(QStringLiteral("codex.exe")));
    return candidates;
}

void CodexAppServerClient::start()
{
    if (m_process.state() != QProcess::NotRunning)
        return;
    m_stopping = false;
    m_seenProcessStart = false;
    m_outputBuffer.clear();
    m_standardErrorTail.clear();
    m_pendingMethods.clear();
    setReady(false);
    if (m_candidates.isEmpty()) {
        setError(QStringLiteral("Install the Codex CLI to use Ava Chat"));
        emit availabilityChanged();
        return;
    }
    startCandidate(0);
}

void CodexAppServerClient::stop()
{
    if (m_process.state() == QProcess::NotRunning)
        return;
    m_stopping = true;
    setReady(false);
    m_process.closeWriteChannel();
    m_process.terminate();
    if (!m_process.waitForFinished(900)) {
        m_process.kill();
        m_process.waitForFinished(500);
    }
    m_stopping = false;
}

void CodexAppServerClient::restart()
{
    stop();
    clearError();
    QTimer::singleShot(0, this, &CodexAppServerClient::start);
}

void CodexAppServerClient::startCandidate(int index)
{
    if (index < 0 || index >= m_candidates.size()) {
        setError(QStringLiteral("A compatible Codex CLI could not be started"));
        return;
    }

    m_candidateIndex = index;
    m_executablePath = m_candidates.at(index);
    emit executableChanged();

    QString program = m_executablePath;
    QStringList arguments;
#ifdef Q_OS_WIN
    if (program.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive)
        || program.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive)) {
        const QString nativeProgram = QDir::toNativeSeparators(program);
        program = commandInterpreter();
        m_process.setNativeArguments(
            QStringLiteral("/d /s /c \"\"%1\" app-server --stdio\"")
                .arg(nativeProgram));
    } else
#endif
    {
        arguments = {QStringLiteral("app-server"), QStringLiteral("--stdio")};
#ifdef Q_OS_WIN
        m_process.setNativeArguments({});
#endif
    }

    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start();
}

void CodexAppServerClient::sendObject(const QJsonObject &object)
{
    if (m_process.state() != QProcess::Running)
        return;
    QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    data.append('\n');
    m_process.write(data);
}

void CodexAppServerClient::consumeStandardOutput()
{
    m_outputBuffer.append(m_process.readAllStandardOutput());
    qsizetype newline = -1;
    while ((newline = m_outputBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_outputBuffer.left(newline).trimmed();
        m_outputBuffer.remove(0, newline + 1);
        if (!line.isEmpty())
            consumeLine(line);
    }
}

void CodexAppServerClient::consumeLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit protocolWarning(QStringLiteral("Codex sent invalid JSON: %1")
                                 .arg(parseError.errorString()));
        return;
    }

    const QJsonObject object = document.object();
    const QString method = object.value(QStringLiteral("method")).toString();
    if (object.contains(QStringLiteral("id")) && !method.isEmpty()) {
        emit serverRequestReceived(object.value(QStringLiteral("id")),
                                   method,
                                   object.value(QStringLiteral("params")).toObject());
        return;
    }

    if (object.contains(QStringLiteral("id"))) {
        bool validId = false;
        const qint64 id = object.value(QStringLiteral("id")).toVariant().toLongLong(&validId);
        if (!validId) {
            emit protocolWarning(QStringLiteral("Codex returned an unknown request id"));
            return;
        }
        const QString requestMethod = m_pendingMethods.take(id);
        const QJsonObject result = object.value(QStringLiteral("result")).toObject();
        const QJsonObject error = object.value(QStringLiteral("error")).toObject();

        if (id == m_initializeRequestId && error.isEmpty()) {
            notify(QStringLiteral("initialized"));
            setReady(true);
        } else if (!error.isEmpty() && id == m_initializeRequestId) {
            setError(error.value(QStringLiteral("message")).toString(
                QStringLiteral("Codex initialization failed")));
        }
        emit responseReceived(id, requestMethod, result, error);
        return;
    }

    if (!method.isEmpty()) {
        emit notificationReceived(method,
                                  object.value(QStringLiteral("params")).toObject());
        return;
    }

    emit protocolWarning(QStringLiteral("Codex sent an unrecognized message"));
}

void CodexAppServerClient::setReady(bool ready)
{
    if (m_ready == ready)
        return;
    m_ready = ready;
    emit readyChanged();
}

void CodexAppServerClient::setError(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorChanged();
}

void CodexAppServerClient::clearError()
{
    setError({});
}
