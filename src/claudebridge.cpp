#include "claudebridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QUuid>

namespace {

constexpr int kMaxRequestBytes = 4 * 1024 * 1024;

QString environmentString(const char *name, const QString &fallback)
{
    const QByteArray value = qgetenv(name);
    return value.isEmpty() ? fallback : QString::fromLocal8Bit(value).trimmed();
}

int environmentInt(const char *name, int fallback)
{
    const QByteArray value = qgetenv(name);
    if (value.isEmpty()) {
        return fallback;
    }
    bool valid = false;
    const int parsed = QString::fromLocal8Bit(value).trimmed().toInt(&valid);
    return valid ? parsed : fallback;
}

QString endpointFilePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("claude-endpoint.json"));
}

QString condense(const QString &text, int limit)
{
    QString flat = text;
    flat.replace(QLatin1Char('\r'), QLatin1Char(' '));
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    flat.replace(QLatin1Char('\t'), QLatin1Char(' '));
    flat = flat.simplified();
    if (flat.size() > limit) {
        flat = flat.left(limit - 1) + QStringLiteral("…");
    }
    return flat;
}

QString projectLabel(const QString &cwd)
{
    if (cwd.isEmpty()) {
        return QString();
    }
    const QFileInfo info(QDir::fromNativeSeparators(cwd));
    const QString name = info.fileName();
    return name.isEmpty() ? info.absoluteFilePath() : name;
}

// The one field of a tool call a person actually needs in order to decide.
QString toolSummary(const QString &toolName, const QJsonObject &input)
{
    static const QHash<QString, QString> primaryField{
        {QStringLiteral("Bash"), QStringLiteral("command")},
        {QStringLiteral("PowerShell"), QStringLiteral("command")},
        {QStringLiteral("Write"), QStringLiteral("file_path")},
        {QStringLiteral("Edit"), QStringLiteral("file_path")},
        {QStringLiteral("MultiEdit"), QStringLiteral("file_path")},
        {QStringLiteral("NotebookEdit"), QStringLiteral("notebook_path")},
        {QStringLiteral("Read"), QStringLiteral("file_path")},
        {QStringLiteral("WebFetch"), QStringLiteral("url")},
        {QStringLiteral("WebSearch"), QStringLiteral("query")},
        {QStringLiteral("Task"), QStringLiteral("description")},
        {QStringLiteral("Agent"), QStringLiteral("description")},
        {QStringLiteral("Glob"), QStringLiteral("pattern")},
        {QStringLiteral("Grep"), QStringLiteral("pattern")},
    };

    const QString field = primaryField.value(toolName);
    if (!field.isEmpty() && input.value(field).isString()) {
        return condense(input.value(field).toString(), 180);
    }

    // Unknown or MCP tool: show the first meaningful string argument.
    for (auto it = input.constBegin(); it != input.constEnd(); ++it) {
        if (it.value().isString() && !it.value().toString().isEmpty()) {
            return condense(it.key() + QStringLiteral(": ") + it.value().toString(), 180);
        }
    }
    return QString();
}

} // namespace

ClaudeBridge::ClaudeBridge(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_approvalEnabled = settings.value(QStringLiteral("claude/approvalEnabled"), true).toBool();

    // A card that is never touched must hand the decision back to the terminal
    // quickly, so the two answer surfaces stay usable one after the other.
    m_stopGraceMs = qBound(0, environmentInt("AVA_CLAUDE_STOP_GRACE_MS", 8000), 600000);
    m_permissionLifetimeMs = qBound(2000,
                                    environmentInt("AVA_CLAUDE_ASK_TIMEOUT_MS", 12000),
                                    3600000);
    m_approveTools = environmentString(
                         "AVA_CLAUDE_APPROVE_TOOLS",
                         QStringLiteral("Bash,PowerShell,Write,Edit,MultiEdit,NotebookEdit,WebFetch"))
                         .split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &tool : m_approveTools) {
        tool = tool.trimmed();
    }

    m_headTimer.setSingleShot(true);
    connect(&m_headTimer, &QTimer::timeout, this, &ClaudeBridge::headExpired);

    // Claude Code never tells us it went idle, so activity decays on its own.
    m_busyTimer.setSingleShot(true);
    m_busyTimer.setInterval(45000);
    connect(&m_busyTimer, &QTimer::timeout, this, [this]() { setBusy(false); });

    loadEndpointFile();
    startListening();
}

ClaudeBridge::~ClaudeBridge()
{
    // Never leave a hook request hanging on a socket that is going away.
    while (!m_queue.isEmpty()) {
        respond(m_queue.first().socket, QJsonObject());
        m_queue.removeFirst();
    }
    if (m_server) {
        m_server->close();
    }
}

QString ClaudeBridge::endpoint() const
{
    return QStringLiteral("http://127.0.0.1:%1/hook").arg(m_port);
}

// Ava and the installer share one file so whichever runs first defines the
// port and the token, and the other one agrees with it.
void ClaudeBridge::loadEndpointFile()
{
    m_port = static_cast<quint16>(qBound(1, environmentInt("AVA_CLAUDE_PORT", 8722), 65535));

    QFile file(endpointFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject stored = QJsonDocument::fromJson(file.readAll()).object();
        const int storedPort = stored.value(QStringLiteral("port")).toInt();
        if (storedPort > 0 && storedPort < 65536 && qgetenv("AVA_CLAUDE_PORT").isEmpty()) {
            m_port = static_cast<quint16>(storedPort);
        }
        m_token = stored.value(QStringLiteral("token")).toString();
    }
    if (m_token.isEmpty()) {
        m_token = QUuid::createUuid().toString(QUuid::Id128)
                  + QUuid::createUuid().toString(QUuid::Id128);
    }
}

void ClaudeBridge::writeEndpointFile()
{
    const QString path = endpointFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject endpointObject;
    endpointObject.insert(QStringLiteral("port"), m_port);
    endpointObject.insert(QStringLiteral("token"), m_token);
    endpointObject.insert(QStringLiteral("url"), endpoint());

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(endpointObject).toJson(QJsonDocument::Indented));
    }
}

void ClaudeBridge::startListening()
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ClaudeBridge::acceptConnection);

    const quint16 requestedPort = m_port;
    for (quint16 offset = 0; offset < 10; ++offset) {
        if (m_server->listen(QHostAddress::LocalHost, static_cast<quint16>(requestedPort + offset))) {
            m_port = static_cast<quint16>(requestedPort + offset);
            m_available = true;
            break;
        }
    }
    if (!m_available) {
        m_errorText = m_server->errorString();
    }
    writeEndpointFile();

    // The island has no console, so record what happened to the endpoint.
    QFile log(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                  .filePath(QStringLiteral("ava-claude.log")));
    if (log.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&log);
        if (m_available) {
            stream << "listening on " << endpoint() << '\n';
            if (m_port != requestedPort) {
                stream << "port " << requestedPort
                       << " was taken; re-run Install-AvaClaude.ps1 to update settings.json\n";
            }
        } else {
            stream << "failed to listen on 127.0.0.1:" << requestedPort << ": " << m_errorText
                   << '\n';
        }
        stream << "endpoint file: " << QDir::toNativeSeparators(endpointFilePath()) << '\n';
    }

    emit availableChanged();
}

QString ClaudeBridge::settingsFilePath()
{
    const QString override = environmentString("AVA_CLAUDE_SETTINGS", QString());
    if (!override.isEmpty()) {
        return override;
    }
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QDir(home).filePath(QStringLiteral(".claude/settings.json"));
}

void ClaudeBridge::installHooks()
{
    const QString error = syncSettingsHooks(true, endpoint(), m_token);
    if (!error.isEmpty()) {
        m_errorText = error;
    }

    QFile log(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                  .filePath(QStringLiteral("ava-claude.log")));
    if (log.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(&log) << "hooks: " << QDir::toNativeSeparators(settingsFilePath())
                          << (error.isEmpty() ? QStringLiteral(" ok") : QStringLiteral(" - ") + error)
                          << '\n';
    }
}

// Ava owns its own hook registration so there is nothing to install by hand.
// Other people's hooks are preserved; only entries pointing at Ava are rewritten.
QString ClaudeBridge::syncSettingsHooks(bool install,
                                        const QString &endpointUrl,
                                        const QString &token)
{
    struct HookSpec
    {
        const char *event;
        const char *matcher;
        int timeout;
        const char *status;
    };
    static const HookSpec specs[] = {
        {"PreToolUse",
         "Bash|PowerShell|Write|Edit|NotebookEdit|WebFetch|AskUserQuestion|mcp__",
         600,
         "Answer in Ava or wait for the prompt"},
        {"Stop", nullptr, 600, "Answer in Ava"},
        {"Notification", nullptr, 15, nullptr},
        {"UserPromptSubmit", nullptr, 15, nullptr},
        {"SessionEnd", nullptr, 15, nullptr},
    };

    const QString path = settingsFilePath();
    QJsonObject settings;
    QByteArray original;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        original = file.readAll();
        file.close();
        const QJsonDocument document = QJsonDocument::fromJson(original);
        if (document.isObject()) {
            settings = document.object();
        } else if (!original.trimmed().isEmpty()) {
            // Never overwrite a settings file we cannot parse.
            return QStringLiteral("Could not parse %1; hooks were left alone.").arg(path);
        }
    }

    QJsonObject hooks = settings.value(QStringLiteral("hooks")).toObject();
    for (const HookSpec &spec : specs) {
        const QString eventName = QString::fromLatin1(spec.event);
        QJsonArray kept;
        for (const QJsonValue &entryValue : hooks.value(eventName).toArray()) {
            const QJsonObject entry = entryValue.toObject();
            bool isAva = false;
            for (const QJsonValue &handlerValue : entry.value(QStringLiteral("hooks")).toArray()) {
                const QJsonObject handler = handlerValue.toObject();
                const QString url = handler.value(QStringLiteral("url")).toString();
                const QString command = handler.value(QStringLiteral("command")).toString();
                if ((url.contains(QStringLiteral("127.0.0.1"))
                     && url.contains(QStringLiteral("/hook")))
                    || command.contains(QStringLiteral("ava-claude-hook"))) {
                    isAva = true;
                    break;
                }
            }
            if (!isAva) {
                kept.append(entry);
            }
        }

        if (install) {
            QJsonObject headers;
            headers.insert(QStringLiteral("Authorization"), QStringLiteral("Bearer ") + token);

            QJsonObject handler;
            handler.insert(QStringLiteral("type"), QStringLiteral("http"));
            handler.insert(QStringLiteral("url"), endpointUrl);
            handler.insert(QStringLiteral("timeout"), spec.timeout);
            handler.insert(QStringLiteral("headers"), headers);
            if (spec.status) {
                handler.insert(QStringLiteral("statusMessage"), QString::fromUtf8(spec.status));
            }

            QJsonArray handlers;
            handlers.append(handler);
            QJsonObject entry;
            if (spec.matcher) {
                entry.insert(QStringLiteral("matcher"), QString::fromLatin1(spec.matcher));
            }
            entry.insert(QStringLiteral("hooks"), handlers);
            kept.append(entry);
        }

        if (kept.isEmpty()) {
            hooks.remove(eventName);
        } else {
            hooks.insert(eventName, kept);
        }
    }

    if (hooks.isEmpty()) {
        settings.remove(QStringLiteral("hooks"));
    } else {
        settings.insert(QStringLiteral("hooks"), hooks);
    }

    const QByteArray updated = QJsonDocument(settings).toJson(QJsonDocument::Indented);
    if (updated == original) {
        return QString();
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!original.isEmpty()) {
        QFile::remove(path + QStringLiteral(".ava-backup"));
        QFile::copy(path, path + QStringLiteral(".ava-backup"));
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("Could not write %1").arg(path);
    }
    out.write(updated);
    return QString();
}

void ClaudeBridge::acceptConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        m_buffers.insert(socket, QByteArray());

        // A client that connects and says nothing must not pin a slot forever.
        auto *idle = new QTimer(socket);
        idle->setSingleShot(true);
        idle->setInterval(15000);
        connect(idle, &QTimer::timeout, socket, [socket]() { socket->abort(); });
        idle->start();

        connect(socket, &QTcpSocket::readyRead, this, [this, socket, idle]() {
            idle->stop();
            readFromSocket(socket);
        });

        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            for (int index = 0; index < m_queue.size(); ++index) {
                if (m_queue.at(index).socket == socket) {
                    const bool wasHead = index == 0;
                    m_queue.remove(index);
                    if (wasHead) {
                        m_replyMode = false;
                        emit replyModeChanged();
                        refreshHeadTimer();
                    }
                    emit requestChanged();
                    break;
                }
            }
            socket->deleteLater();
        });
    }
}

// A deliberately small HTTP/1.1 reader: one POST, one JSON body, one response.
void ClaudeBridge::readFromSocket(QTcpSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    if (buffer.size() > kMaxRequestBytes) {
        respondStatus(socket, 413, "Payload Too Large");
        return;
    }

    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }

    const QByteArray head = buffer.left(headerEnd);
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty() || !lines.constFirst().startsWith("POST")) {
        respondStatus(socket, 405, "Method Not Allowed");
        return;
    }

    int contentLength = 0;
    QByteArray authorization;
    for (int index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines.at(index).trimmed();
        const int colon = line.indexOf(':');
        if (colon < 0) {
            continue;
        }
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "content-length") {
            contentLength = value.toInt();
        } else if (name == "authorization") {
            authorization = value;
        }
    }

    if (buffer.size() < headerEnd + 4 + contentLength) {
        return;
    }

    const QByteArray expected = "Bearer " + m_token.toUtf8();
    if (authorization != expected) {
        respondStatus(socket, 401, "Unauthorized");
        return;
    }

    const QByteArray body = buffer.mid(headerEnd + 4, contentLength);
    buffer.clear();

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        respondStatus(socket, 400, "Bad Request");
        return;
    }
    handlePayload(socket, document.object());
}

void ClaudeBridge::handlePayload(QTcpSocket *socket, const QJsonObject &payload)
{
    const QString event = payload.value(QStringLiteral("hook_event_name")).toString();
    const QString project = projectLabel(payload.value(QStringLiteral("cwd")).toString());

    if (event == QLatin1String("PreToolUse")) {
        handlePreToolUse(socket, payload);
        return;
    }
    if (event == QLatin1String("Stop")) {
        handleStop(socket, payload);
        return;
    }
    if (event == QLatin1String("Notification")) {
        handleNotification(socket, payload);
        return;
    }
    if (event == QLatin1String("UserPromptSubmit")) {
        setActivity(QString(), condense(payload.value(QStringLiteral("prompt")).toString(), 180),
                    project);
        setBusy(true);
        respond(socket, QJsonObject());
        return;
    }
    if (event == QLatin1String("SessionEnd")) {
        if (m_activityProject == project || project.isEmpty()) {
            setBusy(false);
        }
        respond(socket, QJsonObject());
        return;
    }

    respond(socket, QJsonObject());
}

void ClaudeBridge::handlePreToolUse(QTcpSocket *socket, const QJsonObject &payload)
{
    const QString toolName = payload.value(QStringLiteral("tool_name")).toString();
    const QString permissionMode = payload.value(QStringLiteral("permission_mode")).toString();
    const QJsonObject toolInput = payload.value(QStringLiteral("tool_input")).toObject();
    const QString project = projectLabel(payload.value(QStringLiteral("cwd")).toString());
    const QString summary = toolSummary(toolName, toolInput);

    setActivity(toolName, summary, project);
    setBusy(true);

    if (toolName == QLatin1String("AskUserQuestion")) {
        const QJsonArray questions = toolInput.value(QStringLiteral("questions")).toArray();
        if (!questions.isEmpty()) {
            const QJsonObject question = questions.first().toObject();
            Request request;
            request.socket = socket;
            request.kind = QStringLiteral("question");
            request.title = question.value(QStringLiteral("header")).toString();
            request.detail = condense(question.value(QStringLiteral("question")).toString(), 240);
            request.project = project;
            request.lifetimeMs = m_permissionLifetimeMs;
            const QJsonArray options = question.value(QStringLiteral("options")).toArray();
            for (const QJsonValue &option : options) {
                const QString label = option.toObject().value(QStringLiteral("label")).toString();
                if (!label.isEmpty()) {
                    request.options.append(label);
                }
            }
            if (!request.options.isEmpty()) {
                enqueue(request);
                return;
            }
        }
    }

    if (!shouldAskForTool(toolName, permissionMode)) {
        respond(socket, QJsonObject());
        return;
    }

    Request request;
    request.socket = socket;
    request.kind = QStringLiteral("permission");
    request.title = toolName;
    request.detail = summary;
    request.project = project;
    request.lifetimeMs = m_permissionLifetimeMs;
    enqueue(request);
}

void ClaudeBridge::handleStop(QTcpSocket *socket, const QJsonObject &payload)
{
    setBusy(false);

    // A blocked stop re-runs this hook; offering to block again would loop.
    if (payload.value(QStringLiteral("stop_hook_active")).toBool()) {
        respond(socket, QJsonObject());
        return;
    }

    const QString project = projectLabel(payload.value(QStringLiteral("cwd")).toString());
    const QString message = payload.value(QStringLiteral("last_assistant_message")).toString();

    Request request;
    request.kind = QStringLiteral("done");
    request.title = QStringLiteral("Claude finished");
    request.detail = condense(message, 400);
    request.project = project;

    if (m_stopGraceMs <= 0) {
        respond(socket, QJsonObject());
        request.lifetimeMs = 4000;
    } else {
        request.socket = socket;
        request.lifetimeMs = m_stopGraceMs;
    }
    enqueue(request);
}

void ClaudeBridge::handleNotification(QTcpSocket *socket, const QJsonObject &payload)
{
    respond(socket, QJsonObject());

    const QString type = payload.value(QStringLiteral("notification_type")).toString();
    // Ava already owns the permission surface when approval is on.
    if (m_approvalEnabled && type == QLatin1String("permission_prompt")) {
        return;
    }
    if (type == QLatin1String("auth_success") || type.startsWith(QLatin1String("elicitation_"))) {
        return;
    }

    Request request;
    request.kind = QStringLiteral("notice");
    request.title = type == QLatin1String("idle_prompt") ? QStringLiteral("Claude is waiting")
                                                         : QStringLiteral("Claude needs you");
    request.detail = condense(payload.value(QStringLiteral("message")).toString(), 240);
    request.project = projectLabel(payload.value(QStringLiteral("cwd")).toString());
    request.lifetimeMs = 8000;
    enqueue(request);
}

bool ClaudeBridge::shouldAskForTool(const QString &toolName, const QString &permissionMode) const
{
    if (!m_approvalEnabled) {
        return false;
    }
    if (permissionMode == QLatin1String("bypassPermissions")
        || permissionMode == QLatin1String("dontAsk")
        || permissionMode == QLatin1String("auto")
        || permissionMode == QLatin1String("plan")) {
        return false;
    }
    const bool editingTool = toolName == QLatin1String("Write") || toolName == QLatin1String("Edit")
                             || toolName == QLatin1String("MultiEdit")
                             || toolName == QLatin1String("NotebookEdit");
    if (permissionMode == QLatin1String("acceptEdits") && editingTool) {
        return false;
    }
    if (toolName.startsWith(QLatin1String("mcp__"))) {
        return true;
    }
    return m_approveTools.contains(toolName, Qt::CaseInsensitive);
}

void ClaudeBridge::enqueue(const Request &request)
{
    const bool becomesHead = m_queue.isEmpty();
    m_queue.append(request);
    if (becomesHead) {
        refreshHeadTimer();
    }
    emit requestChanged();
    emit requestArrived(request.kind);
}

void ClaudeBridge::resolveHead(const QJsonObject &response)
{
    if (m_queue.isEmpty()) {
        return;
    }
    const Request request = m_queue.takeFirst();
    respond(request.socket, response);
    if (m_replyMode) {
        m_replyMode = false;
        emit replyModeChanged();
    }
    refreshHeadTimer();
    emit requestChanged();
}

void ClaudeBridge::refreshHeadTimer()
{
    m_headTimer.stop();
    if (m_queue.isEmpty() || m_replyMode) {
        return;
    }
    const int lifetime = m_queue.first().lifetimeMs;
    if (lifetime > 0) {
        m_headTimer.start(lifetime);
    }
}

void ClaudeBridge::headExpired()
{
    // Timing out always hands the answer back to Claude Code's own prompt.
    deferToTerminal();
}

void ClaudeBridge::approve()
{
    if (requestKind() != QLatin1String("permission")) {
        return;
    }
    resolveHead(preToolUseDecision(QStringLiteral("allow"), QStringLiteral("Approved from Ava.")));
}

void ClaudeBridge::deny()
{
    if (requestKind() != QLatin1String("permission")) {
        return;
    }
    resolveHead(preToolUseDecision(QStringLiteral("deny"),
                                   QStringLiteral("The user denied this from Ava. Do not retry "
                                                  "this call; pick another approach or ask.")));
}

void ClaudeBridge::deferToTerminal()
{
    const QString kind = requestKind();
    if (kind == QLatin1String("permission") || kind == QLatin1String("question")) {
        resolveHead(preToolUseDecision(QStringLiteral("defer"), QString()));
        return;
    }
    resolveHead(QJsonObject());
}

void ClaudeBridge::chooseOption(int index)
{
    if (requestKind() != QLatin1String("question")) {
        return;
    }
    const QStringList options = requestOptions();
    if (index < 0 || index >= options.size()) {
        return;
    }
    // AskUserQuestion has no "answer" hook decision, so the answer travels back
    // as the denial reason, which Claude reads as user input.
    const QString reason = QStringLiteral("The user answered from Ava: \"%1\". Treat that as the "
                                          "answer to your question and continue without asking "
                                          "again.")
                               .arg(options.at(index));
    resolveHead(preToolUseDecision(QStringLiteral("deny"), reason));
}

void ClaudeBridge::beginReply()
{
    if (requestKind() != QLatin1String("done") || m_queue.first().socket.isNull()) {
        return;
    }
    if (m_replyMode) {
        return;
    }
    m_replyMode = true;
    m_headTimer.stop();
    emit replyModeChanged();
}

void ClaudeBridge::cancelReply()
{
    if (!m_replyMode) {
        return;
    }
    m_replyMode = false;
    emit replyModeChanged();
    refreshHeadTimer();
}

void ClaudeBridge::sendReply(const QString &text)
{
    if (requestKind() != QLatin1String("done")) {
        return;
    }
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        cancelReply();
        return;
    }
    QJsonObject response;
    response.insert(QStringLiteral("decision"), QStringLiteral("block"));
    response.insert(QStringLiteral("reason"), trimmed);
    setBusy(true);
    resolveHead(response);
}

void ClaudeBridge::dismiss()
{
    if (m_queue.isEmpty()) {
        return;
    }
    if (m_replyMode) {
        cancelReply();
        return;
    }
    deferToTerminal();
}

QString ClaudeBridge::requestKind() const
{
    return m_queue.isEmpty() ? QString() : m_queue.first().kind;
}

QString ClaudeBridge::requestTitle() const
{
    return m_queue.isEmpty() ? QString() : m_queue.first().title;
}

QString ClaudeBridge::requestDetail() const
{
    return m_queue.isEmpty() ? QString() : m_queue.first().detail;
}

QString ClaudeBridge::requestBody() const
{
    return m_queue.isEmpty() ? QString() : m_queue.first().body;
}

QString ClaudeBridge::requestProject() const
{
    return m_queue.isEmpty() ? QString() : m_queue.first().project;
}

QStringList ClaudeBridge::requestOptions() const
{
    return m_queue.isEmpty() ? QStringList() : m_queue.first().options;
}

int ClaudeBridge::requestLifetimeMs() const
{
    return m_queue.isEmpty() ? 0 : m_queue.first().lifetimeMs;
}

void ClaudeBridge::setApprovalEnabled(bool enabled)
{
    if (m_approvalEnabled == enabled) {
        return;
    }
    m_approvalEnabled = enabled;
    QSettings().setValue(QStringLiteral("claude/approvalEnabled"), enabled);
    emit approvalEnabledChanged();
}

void ClaudeBridge::toggleApprovalEnabled()
{
    setApprovalEnabled(!m_approvalEnabled);
}

void ClaudeBridge::setBusy(bool busy)
{
    if (busy) {
        m_busyTimer.start();
    } else {
        m_busyTimer.stop();
    }
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit activityChanged();
}

void ClaudeBridge::setActivity(const QString &tool, const QString &detail, const QString &project)
{
    if (m_activityTool == tool && m_activityDetail == detail && m_activityProject == project) {
        return;
    }
    m_activityTool = tool;
    m_activityDetail = detail;
    m_activityProject = project;
    emit activityChanged();
}

void ClaudeBridge::respond(QTcpSocket *socket, const QJsonObject &response)
{
    if (!socket || socket->state() != QTcpSocket::ConnectedState) {
        return;
    }
    const QByteArray body = response.isEmpty()
                                ? QByteArray()
                                : QJsonDocument(response).toJson(QJsonDocument::Compact);
    QByteArray reply = "HTTP/1.1 200 OK\r\n";
    reply += "Content-Type: application/json\r\n";
    reply += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    reply += "Connection: close\r\n\r\n";
    reply += body;
    socket->write(reply);
    socket->flush();
    socket->disconnectFromHost();
}

void ClaudeBridge::respondStatus(QTcpSocket *socket, int status, const char *reason)
{
    if (!socket || socket->state() != QTcpSocket::ConnectedState) {
        return;
    }
    QByteArray reply = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    reply += "Content-Length: 0\r\n";
    reply += "Connection: close\r\n\r\n";
    socket->write(reply);
    socket->flush();
    socket->disconnectFromHost();
}

QJsonObject ClaudeBridge::preToolUseDecision(const QString &decision, const QString &reason)
{
    QJsonObject hookSpecificOutput;
    hookSpecificOutput.insert(QStringLiteral("hookEventName"), QStringLiteral("PreToolUse"));
    hookSpecificOutput.insert(QStringLiteral("permissionDecision"), decision);
    if (!reason.isEmpty()) {
        hookSpecificOutput.insert(QStringLiteral("permissionDecisionReason"), reason);
    }
    QJsonObject response;
    response.insert(QStringLiteral("hookSpecificOutput"), hookSpecificOutput);
    response.insert(QStringLiteral("suppressOutput"), true);
    return response;
}
