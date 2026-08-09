#include "codexmodels.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMimeDatabase>
#include <QUrl>

#include <algorithm>

namespace {

QString normalizedStatus(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (!type.isEmpty())
            return type;
        if (!object.isEmpty())
            return object.constBegin().key();
    }
    return QStringLiteral("idle");
}

QString textFromUserContent(const QJsonArray &content)
{
    QStringList parts;
    for (const QJsonValue &value : content) {
        const QJsonObject item = value.toObject();
        const QString type = item.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("text"))
            parts.append(item.value(QStringLiteral("text")).toString());
        else if (type == QStringLiteral("localImage") || type == QStringLiteral("mention"))
            parts.append(QFileInfo(item.value(QStringLiteral("path")).toString()).fileName());
        else if (type == QStringLiteral("image"))
            parts.append(QStringLiteral("Image"));
    }
    return parts.join(QStringLiteral("\n"));
}

QString reasoningText(const QJsonValue &value)
{
    QStringList parts;
    if (value.isString())
        return value.toString();
    for (const QJsonValue &part : value.toArray()) {
        if (part.isString()) {
            parts.append(part.toString());
        } else {
            const QJsonObject object = part.toObject();
            const QString text = object.value(QStringLiteral("text")).toString();
            if (!text.isEmpty())
                parts.append(text);
        }
    }
    return parts.join(QStringLiteral("\n"));
}

QString formattedReviewText(const QString &text)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return text;

    const QJsonObject review = document.object();
    if (!review.contains(QStringLiteral("findings")))
        return text;

    QStringList sections;
    const QJsonArray findings = review.value(QStringLiteral("findings")).toArray();
    if (findings.isEmpty()) {
        sections.append(QStringLiteral("No findings."));
    } else {
        for (const QJsonValue &value : findings) {
            const QJsonObject finding = value.toObject();
            QString section;
            const QString title = finding.value(QStringLiteral("title")).toString().trimmed();
            const QString body = finding.value(QStringLiteral("body")).toString().trimmed();
            if (!title.isEmpty())
                section.append(QStringLiteral("**%1**").arg(title));
            if (!body.isEmpty()) {
                if (!section.isEmpty())
                    section.append(QStringLiteral("\n"));
                section.append(body);
            }
            const QJsonObject location = finding.value(QStringLiteral("code_location")).toObject();
            const QString path = location.value(QStringLiteral("absolute_file_path"))
                                     .toString().trimmed();
            const int line = location.value(QStringLiteral("line_range"))
                                 .toObject().value(QStringLiteral("start")).toInt();
            if (!path.isEmpty()) {
                if (!section.isEmpty())
                    section.append(QStringLiteral("\n"));
                section.append(QStringLiteral("`%1%2`").arg(
                    path, line > 0 ? QStringLiteral(":%1").arg(line) : QString()));
            }
            if (!section.isEmpty())
                sections.append(section);
        }
    }

    const QString explanation = review.value(QStringLiteral("overall_explanation"))
                                    .toString().trimmed();
    if (!explanation.isEmpty())
        sections.append(QStringLiteral("**Overall**\n") + explanation);
    return sections.isEmpty() ? text : sections.join(QStringLiteral("\n\n"));
}

qint64 epochFromValue(const QJsonValue &value)
{
    if (value.isDouble())
        return static_cast<qint64>(value.toDouble());
    return 0;
}

QPair<int, int> diffStats(const QString &diff)
{
    int additions = 0;
    int deletions = 0;
    const QStringList lines = diff.split(QChar('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QChar('+')) && !line.startsWith(QStringLiteral("+++")))
            ++additions;
        else if (line.startsWith(QChar('-')) && !line.startsWith(QStringLiteral("---")))
            ++deletions;
    }
    return {additions, deletions};
}

QString firstString(const QJsonObject &object,
                    std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QString value = object.value(QLatin1String(key)).toString();
        if (!value.isEmpty())
            return value;
    }
    return {};
}

QString sourceFavicon(const QJsonObject &result,
                      const QJsonObject &source,
                      const QUrl &url)
{
    QString favicon = firstString(result, {"favicon", "faviconUrl", "icon", "logo"});
    if (favicon.isEmpty())
        favicon = firstString(source, {"favicon", "faviconUrl", "icon", "logo"});
    if (!favicon.isEmpty())
        return favicon;
    if (!url.isValid() || url.host().isEmpty())
        return {};
    return QStringLiteral("https://icons.duckduckgo.com/ip3/%1.ico")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(url.host())));
}

QVariantList searchSources(const QJsonObject &item)
{
    QVariantList sources;
    QJsonArray results = item.value(QStringLiteral("results")).toArray();
    const QJsonObject action = item.value(QStringLiteral("action")).toObject();
    if (results.isEmpty() && !action.value(QStringLiteral("url")).toString().isEmpty())
        results.append(action);

    for (const QJsonValue &value : results) {
        const QJsonObject result = value.isString()
            ? QJsonObject{{QStringLiteral("url"), value.toString()}}
            : value.toObject();
        const QJsonObject source = result.value(QStringLiteral("source")).toObject();
        QString urlText = firstString(result, {"url", "link", "href"});
        if (urlText.isEmpty())
            urlText = firstString(source, {"url", "link", "href"});
        const QUrl url(urlText);
        if (!url.isValid()
            || (url.scheme() != QStringLiteral("http")
                && url.scheme() != QStringLiteral("https")))
            continue;
        QString title = firstString(result, {"title", "name", "label"});
        if (title.isEmpty())
            title = firstString(source, {"title", "name", "label"});
        if (title.isEmpty())
            title = url.host();
        QVariantMap presentation;
        presentation.insert(QStringLiteral("title"), title.simplified());
        presentation.insert(QStringLiteral("url"), url.toString());
        presentation.insert(QStringLiteral("host"), url.host());
        presentation.insert(QStringLiteral("favicon"), sourceFavicon(result, source, url));
        presentation.insert(QStringLiteral("snippet"),
                            firstString(result, {"snippet", "description", "text"}).simplified());
        sources.append(presentation);
    }
    return sources;
}

QString formatDuration(qint64 durationMs)
{
    const qint64 seconds = std::max<qint64>(0, durationMs / 1000);
    if (seconds < 60)
        return QStringLiteral("%1s").arg(seconds);
    const qint64 minutes = seconds / 60;
    const qint64 remaining = seconds % 60;
    return remaining == 0
        ? QStringLiteral("%1m").arg(minutes)
        : QStringLiteral("%1m %2s").arg(minutes).arg(remaining);
}

void appendBounded(QString &target, const QString &delta, qsizetype maximum)
{
    if (delta.isEmpty())
        return;
    target.append(delta);
    if (target.size() <= maximum)
        return;
    target = QStringLiteral("…\n") + target.right(maximum - 2);
}

} // namespace

CodexThreadListModel::CodexThreadListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CodexThreadListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant CodexThreadListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case ThreadIdRole: return entry.id;
    case TitleRole: return entry.title;
    case PreviewRole: return entry.preview;
    case CwdRole: return entry.cwd;
    case UpdatedAtRole: return entry.updatedAt;
    case StatusRole: return entry.status;
    case PinnedRole: return entry.pinned;
    default: return {};
    }
}

QHash<int, QByteArray> CodexThreadListModel::roleNames() const
{
    return {{ThreadIdRole, "threadId"}, {TitleRole, "title"},
            {PreviewRole, "preview"}, {CwdRole, "cwd"},
            {UpdatedAtRole, "updatedAt"}, {StatusRole, "threadStatus"},
            {PinnedRole, "pinned"}};
}

void CodexThreadListModel::replace(const QJsonArray &threads)
{
    QVector<Entry> next;
    next.reserve(threads.size());
    for (const QJsonValue &value : threads) {
        const Entry entry = fromJson(value.toObject());
        if (!entry.id.isEmpty())
            next.append(entry);
    }
    std::sort(next.begin(), next.end(), [](const Entry &a, const Entry &b) {
        if (a.pinned != b.pinned)
            return a.pinned > b.pinned;
        return a.updatedAt > b.updatedAt;
    });
    beginResetModel();
    m_entries = std::move(next);
    endResetModel();
}

void CodexThreadListModel::replaceSearchResults(const QJsonArray &results)
{
    QVector<Entry> next;
    next.reserve(results.size());
    for (const QJsonValue &value : results) {
        const QJsonObject result = value.toObject();
        QJsonObject thread = result.value(QStringLiteral("thread")).toObject();
        if (thread.isEmpty())
            continue;
        const QString snippet = result.value(QStringLiteral("snippet"))
                                    .toString().simplified();
        if (!snippet.isEmpty())
            thread.insert(QStringLiteral("preview"), snippet);
        const Entry entry = fromJson(thread);
        if (!entry.id.isEmpty())
            next.append(entry);
    }
    beginResetModel();
    m_entries = std::move(next);
    endResetModel();
}

void CodexThreadListModel::upsert(const QJsonObject &thread)
{
    Entry entry = fromJson(thread);
    if (entry.id.isEmpty())
        return;
    const int existing = rowForId(entry.id);
    if (existing >= 0) {
        beginResetModel();
        m_entries[existing] = std::move(entry);
        std::sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) {
            if (a.pinned != b.pinned)
                return a.pinned > b.pinned;
            return a.updatedAt > b.updatedAt;
        });
        endResetModel();
        return;
    }
    beginInsertRows({}, 0, 0);
    m_entries.prepend(std::move(entry));
    endInsertRows();
}

void CodexThreadListModel::updateStatus(const QString &threadId,
                                        const QJsonValue &status)
{
    const int row = rowForId(threadId);
    if (row < 0)
        return;
    const QString next = normalizedStatus(status);
    if (m_entries.at(row).status == next)
        return;
    m_entries[row].status = next;
    emit dataChanged(index(row), index(row), {StatusRole});
}

void CodexThreadListModel::removeById(const QString &threadId)
{
    const int row = rowForId(threadId);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
}

QString CodexThreadListModel::threadIdAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).id : QString();
}

QString CodexThreadListModel::cwdAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).cwd : QString();
}

int CodexThreadListModel::rowForId(const QString &threadId) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).id == threadId)
            return row;
    }
    return -1;
}

CodexThreadListModel::Entry CodexThreadListModel::fromJson(const QJsonObject &thread)
{
    Entry entry;
    entry.id = thread.value(QStringLiteral("id")).toString();
    entry.title = thread.value(QStringLiteral("name")).toString().simplified();
    entry.preview = thread.value(QStringLiteral("preview")).toString().simplified();
    entry.cwd = thread.value(QStringLiteral("cwd")).toString();
    entry.updatedAt = epochFromValue(thread.value(QStringLiteral("updatedAt")));
    if (entry.updatedAt == 0)
        entry.updatedAt = epochFromValue(thread.value(QStringLiteral("createdAt")));
    entry.status = normalizedStatus(thread.value(QStringLiteral("status")));
    entry.pinned = thread.value(QStringLiteral("isPinned")).toBool();
    if (entry.title.isEmpty())
        entry.title = entry.preview;
    if (entry.title.isEmpty())
        entry.title = QStringLiteral("New chat");
    if (entry.title.size() > 54)
        entry.title = entry.title.left(53).trimmed() + QStringLiteral("…");
    if (entry.preview.size() > 92)
        entry.preview = entry.preview.left(91).trimmed() + QStringLiteral("…");
    return entry;
}

CodexTimelineModel::CodexTimelineModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CodexTimelineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant CodexTimelineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case ItemIdRole: return entry.id;
    case KindRole: return entry.kind;
    case PhaseRole: return entry.phase;
    case TitleRole: return entry.title;
    case BodyRole: return entry.body;
    case DetailRole: return entry.detail;
    case StatusRole: return entry.status;
    case CwdRole: return entry.cwd;
    case TimestampRole: return entry.timestamp;
    case RunningRole: return entry.running;
    case ErrorRole: return entry.error;
    case FileChangesRole: return entry.fileChanges;
    case AdditionsRole: return entry.additions;
    case DeletionsRole: return entry.deletions;
    case ActivitiesRole: return entry.activities;
    case ElapsedRole: return entry.elapsed;
    default: return {};
    }
}

QHash<int, QByteArray> CodexTimelineModel::roleNames() const
{
    return {{ItemIdRole, "itemId"}, {KindRole, "kind"},
            {PhaseRole, "phase"}, {TitleRole, "title"},
            {BodyRole, "body"}, {DetailRole, "detail"},
            {StatusRole, "itemStatus"}, {CwdRole, "cwd"},
            {TimestampRole, "timestamp"}, {RunningRole, "running"},
            {ErrorRole, "isError"}, {FileChangesRole, "fileChanges"},
            {AdditionsRole, "additions"}, {DeletionsRole, "deletions"},
            {ActivitiesRole, "activities"}, {ElapsedRole, "elapsed"}};
}

void CodexTimelineModel::clear()
{
    beginResetModel();
    m_entries.clear();
    m_rowsById.clear();
    m_activeWorkGroups.clear();
    m_previousWorkGroups.clear();
    m_workSegmentCounts.clear();
    endResetModel();
}

void CodexTimelineModel::replaceFromThread(const QJsonObject &thread)
{
    beginResetModel();
    m_rebuilding = true;
    m_entries.clear();
    m_rowsById.clear();
    m_activeWorkGroups.clear();
    m_previousWorkGroups.clear();
    m_workSegmentCounts.clear();
    const QJsonArray turns = thread.value(QStringLiteral("turns")).toArray();
    for (const QJsonValue &turnValue : turns) {
        const QJsonObject turn = turnValue.toObject();
        const QString turnId = turn.value(QStringLiteral("id")).toString();
        for (const QJsonValue &itemValue : turn.value(QStringLiteral("items")).toArray()) {
            Entry entry = fromItem(itemValue.toObject(), true);
            if (entry.id.isEmpty())
                continue;
            entry.turnId = turnId;
            if (entry.kind == QStringLiteral("user")) {
                if (isDuplicateProtocolUserMessage(entry))
                    continue;
                startHistoricalWorkSegment(turnId, entry.id);
                m_entries.append(std::move(entry));
            } else if (belongsInWork(entry)) {
                upsertWorkActivity(std::move(entry), turnId);
            } else {
                m_entries.append(std::move(entry));
            }
        }
        qint64 durationMs = static_cast<qint64>(
            turn.value(QStringLiteral("durationMs")).toDouble(-1));
        const QString turnStatus = turn.value(QStringLiteral("status")).toString();
        const qint64 startedAt = epochFromValue(turn.value(QStringLiteral("startedAt")));
        const qint64 completedAt = epochFromValue(turn.value(QStringLiteral("completedAt")));
        if (durationMs < 0 && startedAt > 0 && completedAt >= startedAt)
            durationMs = (completedAt - startedAt) * 1000;
        const bool completed = completedAt > 0
            || turnStatus == QStringLiteral("completed")
            || turnStatus == QStringLiteral("failed")
            || turnStatus == QStringLiteral("interrupted");
        if (completed || durationMs >= 0)
            completeWork(turnId, durationMs);
    }
    m_rebuilding = false;
    rebuildRowIndex();
    endResetModel();
}

void CodexTimelineModel::beginOptimisticTurn(const QString &clientMessageId,
                                             const QString &text)
{
    if (clientMessageId.isEmpty() || text.isEmpty() || rowForId(clientMessageId) >= 0)
        return;

    Entry message;
    message.id = clientMessageId;
    message.kind = QStringLiteral("user");
    message.title = QStringLiteral("You");
    message.body = text;
    message.status = QStringLiteral("sending");
    message.timestamp = QDateTime::currentSecsSinceEpoch();

    Entry work;
    work.id = QStringLiteral("work:pending:") + clientMessageId;
    work.turnId = QStringLiteral("pending:") + clientMessageId;
    work.kind = QStringLiteral("work");
    work.title = QStringLiteral("Working");
    work.status = QStringLiteral("inProgress");
    work.running = true;
    work.timestamp = message.timestamp;
    work.activities.append(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("thinking:") + clientMessageId},
        {QStringLiteral("kind"), QStringLiteral("reasoning")},
        {QStringLiteral("title"), QStringLiteral("Thinking")},
        {QStringLiteral("body"), QString()},
        {QStringLiteral("detail"), QString()},
        {QStringLiteral("status"), QStringLiteral("optimistic")},
        {QStringLiteral("running"), true},
        {QStringLiteral("error"), false},
        {QStringLiteral("sources"), QVariantList{}}
    });

    const int first = m_entries.size();
    beginInsertRows({}, first, first + 1);
    m_entries.append(std::move(message));
    m_entries.append(std::move(work));
    m_rowsById.insert(m_entries.at(first).id, first);
    m_rowsById.insert(m_entries.at(first + 1).id, first + 1);
    endInsertRows();
}

void CodexTimelineModel::beginOptimisticSteer(const QString &clientMessageId,
                                              const QString &text,
                                              const QString &turnId)
{
    if (turnId.isEmpty()) {
        beginOptimisticTurn(clientMessageId, text);
        return;
    }
    if (clientMessageId.isEmpty() || text.isEmpty() || rowForId(clientMessageId) >= 0)
        return;

    const QString previousGroup = workGroupId(turnId);
    if (rowForId(previousGroup) >= 0)
        finishWorkGroup(previousGroup);

    beginOptimisticTurn(clientMessageId, text);
    const QString pendingId = QStringLiteral("work:pending:") + clientMessageId;
    const int pendingRow = rowForId(pendingId);
    if (pendingRow < 0)
        return;

    m_entries[pendingRow].turnId = turnId;
    m_previousWorkGroups.insert(clientMessageId, previousGroup);
    m_activeWorkGroups.insert(turnId, pendingId);
    m_workSegmentCounts[turnId] = std::max(2, m_workSegmentCounts.value(turnId, 1) + 1);
}

void CodexTimelineModel::acknowledgeOptimisticTurn(const QString &clientMessageId,
                                                   const QString &turnId)
{
    if (clientMessageId.isEmpty())
        return;

    const int messageRow = rowForId(clientMessageId);
    if (messageRow >= 0) {
        Entry &message = m_entries[messageRow];
        message.status = QStringLiteral("sent");
        message.turnId = turnId;
        emit dataChanged(index(messageRow), index(messageRow),
                         {StatusRole});
    }

    const QString pendingId = QStringLiteral("work:pending:") + clientMessageId;
    const int pendingRow = rowForId(pendingId);
    if (pendingRow < 0 || turnId.isEmpty())
        return;

    const bool steering = m_activeWorkGroups.value(turnId) == pendingId;
    const QString authoritativeId = steering
        ? QStringLiteral("work:%1:steer:%2").arg(turnId, clientMessageId)
        : QStringLiteral("work:") + turnId;
    const int authoritativeRow = rowForId(authoritativeId);
    if (authoritativeRow >= 0 && authoritativeRow != pendingRow) {
        removeEntryAt(pendingRow);
        return;
    }

    m_rowsById.remove(pendingId);
    Entry &work = m_entries[pendingRow];
    work.id = authoritativeId;
    work.turnId = turnId;
    m_rowsById.insert(authoritativeId, pendingRow);
    m_activeWorkGroups.insert(turnId, authoritativeId);
    m_previousWorkGroups.remove(clientMessageId);
    emit dataChanged(index(pendingRow), index(pendingRow),
                     {ItemIdRole, StatusRole});
}

void CodexTimelineModel::failOptimisticTurn(const QString &clientMessageId,
                                            const QString &message)
{
    if (clientMessageId.isEmpty())
        return;

    const int messageRow = rowForId(clientMessageId);
    if (messageRow >= 0) {
        Entry &entry = m_entries[messageRow];
        entry.status = QStringLiteral("failed");
        entry.error = true;
        entry.detail = message;
        emit dataChanged(index(messageRow), index(messageRow),
                         {StatusRole, ErrorRole, DetailRole});
    }

    const QString pendingId = QStringLiteral("work:pending:") + clientMessageId;
    const int pendingRow = rowForId(pendingId);
    const QString turnId = pendingRow >= 0 ? m_entries.at(pendingRow).turnId : QString();
    removeEntryAt(pendingRow);

    const QString previousGroup = m_previousWorkGroups.take(clientMessageId);
    if (!turnId.isEmpty() && !previousGroup.isEmpty()) {
        m_activeWorkGroups.insert(turnId, previousGroup);
        const int previousRow = rowForId(previousGroup);
        if (previousRow >= 0) {
            Entry &work = m_entries[previousRow];
            work.running = true;
            work.status = QStringLiteral("inProgress");
            work.title = QStringLiteral("Working");
            work.elapsed.clear();
            emit dataChanged(index(previousRow), index(previousRow),
                             {TitleRole, StatusRole, RunningRole, ElapsedRole});
        }
    }
}

void CodexTimelineModel::upsertItem(const QJsonObject &item, bool completed,
                                    const QString &turnId)
{
    Entry entry = fromItem(item, completed);
    if (entry.id.isEmpty())
        return;
    entry.turnId = turnId;
    if (belongsInWork(entry)) {
        upsertWorkActivity(std::move(entry), turnId);
        return;
    }
    if (entry.kind == QStringLiteral("user")
        && rowForId(entry.id) < 0
        && isDuplicateProtocolUserMessage(entry)) {
        return;
    }
    insertOrReplace(std::move(entry));
}

void CodexTimelineModel::appendAgentDelta(const QString &itemId,
                                          const QString &delta)
{
    if (itemId.isEmpty() || delta.isEmpty())
        return;
    int row = rowForId(itemId);
    if (row < 0) {
        Entry entry;
        entry.id = itemId;
        entry.kind = QStringLiteral("agent");
        entry.phase = QStringLiteral("commentary");
        entry.title = QStringLiteral("Codex");
        entry.running = true;
        entry.timestamp = QDateTime::currentSecsSinceEpoch();
        beginInsertRows({}, m_entries.size(), m_entries.size());
        m_entries.append(std::move(entry));
        m_rowsById.insert(m_entries.constLast().id, m_entries.size() - 1);
        endInsertRows();
        row = m_entries.size() - 1;
    }
    appendBounded(m_entries[row].body, delta, 256 * 1024);
    m_entries[row].running = true;
    emit dataChanged(index(row), index(row), {BodyRole, RunningRole});
}

void CodexTimelineModel::appendWorkDelta(const QString &itemId,
                                         const QString &turnId,
                                         const QString &kind,
                                         const QString &bodyDelta,
                                         const QString &detailDelta)
{
    if (itemId.isEmpty() || (bodyDelta.isEmpty() && detailDelta.isEmpty()))
        return;

    const QString resolvedTurnId = turnId.isEmpty()
        ? QStringLiteral("current") : turnId;
    const QString groupId = workGroupId(resolvedTurnId);
    int row = rowForId(groupId);
    if (row < 0) {
        Entry activity;
        activity.id = itemId;
        activity.kind = kind;
        activity.title = kind == QStringLiteral("command")
            ? QStringLiteral("Running command")
            : (kind == QStringLiteral("plan") ? QStringLiteral("Plan")
                                                : QStringLiteral("Reasoning"));
        activity.running = true;
        activity.status = QStringLiteral("inProgress");
        activity.timestamp = QDateTime::currentSecsSinceEpoch();
        appendBounded(activity.body, bodyDelta, 24 * 1024);
        appendBounded(activity.detail, detailDelta, 48 * 1024);
        upsertWorkActivity(std::move(activity), resolvedTurnId);
        return;
    }

    Entry &work = m_entries[row];
    for (int index = work.activities.size() - 1; index >= 0; --index) {
        if (work.activities.at(index).toMap().value(QStringLiteral("status")).toString()
            == QStringLiteral("optimistic")) {
            work.activities.removeAt(index);
        }
    }

    int activityIndex = -1;
    for (int index = 0; index < work.activities.size(); ++index) {
        if (work.activities.at(index).toMap().value(QStringLiteral("id")).toString()
            == itemId) {
            activityIndex = index;
            break;
        }
    }
    if (activityIndex < 0) {
        QVariantMap activity{
            {QStringLiteral("id"), itemId},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("title"), kind == QStringLiteral("command")
                 ? QStringLiteral("Running command")
                 : (kind == QStringLiteral("plan") ? QStringLiteral("Plan")
                                                     : QStringLiteral("Reasoning"))},
            {QStringLiteral("body"), QString()},
            {QStringLiteral("detail"), QString()},
            {QStringLiteral("status"), QStringLiteral("inProgress")},
            {QStringLiteral("running"), true},
            {QStringLiteral("error"), false},
            {QStringLiteral("sources"), QVariantList{}}
        };
        work.activities.append(activity);
        activityIndex = work.activities.size() - 1;
    }

    QVariantMap activity = work.activities.at(activityIndex).toMap();
    QString body = activity.value(QStringLiteral("body")).toString();
    QString detail = activity.value(QStringLiteral("detail")).toString();
    appendBounded(body, bodyDelta, 24 * 1024);
    appendBounded(detail, detailDelta, 48 * 1024);
    activity.insert(QStringLiteral("body"), body);
    activity.insert(QStringLiteral("detail"), detail);
    activity.insert(QStringLiteral("running"), true);
    activity.insert(QStringLiteral("status"), QStringLiteral("inProgress"));
    work.activities[activityIndex] = activity;
    work.running = true;
    work.status = QStringLiteral("inProgress");
    work.title = QStringLiteral("Working");
    work.elapsed.clear();
    if (!m_rebuilding)
        emit dataChanged(index(row), index(row),
                         {TitleRole, StatusRole, RunningRole, ActivitiesRole, ElapsedRole});
}

void CodexTimelineModel::updatePlan(const QString &turnId, const QJsonArray &plan)
{
    Entry entry;
    entry.id = QStringLiteral("plan:") + turnId;
    entry.kind = QStringLiteral("plan");
    entry.title = QStringLiteral("Plan");
    QStringList lines;
    bool running = false;
    for (const QJsonValue &value : plan) {
        const QJsonObject step = value.toObject();
        const QString status = step.value(QStringLiteral("status")).toString();
        const QString marker = status == QStringLiteral("completed")
            ? QStringLiteral("✓")
            : (status == QStringLiteral("inProgress") ? QStringLiteral("●")
                                                       : QStringLiteral("○"));
        lines.append(marker + QStringLiteral("  ")
                     + step.value(QStringLiteral("step")).toString());
        running = running || status == QStringLiteral("inProgress");
    }
    entry.body = lines.join(QStringLiteral("\n"));
    entry.running = running;
    entry.status = running ? QStringLiteral("inProgress") : QStringLiteral("completed");
    entry.timestamp = QDateTime::currentSecsSinceEpoch();
    upsertWorkActivity(std::move(entry), turnId);
}

void CodexTimelineModel::completeWork(const QString &turnId, qint64 durationMs)
{
    const QString resolvedTurnId = turnId.isEmpty()
        ? QStringLiteral("current") : turnId;
    const QString groupId = workGroupId(resolvedTurnId);
    finishWorkGroup(groupId,
                    m_workSegmentCounts.value(resolvedTurnId, 1) > 1
                        ? -1 : durationMs);
}

void CodexTimelineModel::finishWorkGroup(const QString &groupId, qint64 durationMs)
{
    const int row = rowForId(groupId);
    if (row < 0)
        return;
    Entry &work = m_entries[row];
    work.running = false;
    work.status = QStringLiteral("completed");
    work.elapsed = durationMs >= 0 ? formatDuration(durationMs) : QString();
    work.title = work.elapsed.isEmpty()
        ? QStringLiteral("Worked")
        : QStringLiteral("Worked for %1").arg(work.elapsed);
    for (QVariant &value : work.activities) {
        QVariantMap activity = value.toMap();
        activity.insert(QStringLiteral("running"), false);
        value = activity;
    }
    if (!m_rebuilding)
        emit dataChanged(index(row), index(row),
                         {TitleRole, StatusRole, RunningRole, ActivitiesRole, ElapsedRole});
}

void CodexTimelineModel::appendError(const QString &message)
{
    if (message.trimmed().isEmpty())
        return;
    Entry entry;
    entry.id = QStringLiteral("error:%1:%2")
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(m_entries.size());
    entry.kind = QStringLiteral("error");
    entry.title = QStringLiteral("Codex stopped");
    entry.body = message;
    entry.error = true;
    entry.status = QStringLiteral("failed");
    entry.timestamp = QDateTime::currentSecsSinceEpoch();
    beginInsertRows({}, m_entries.size(), m_entries.size());
    m_entries.append(std::move(entry));
    m_rowsById.insert(m_entries.constLast().id, m_entries.size() - 1);
    endInsertRows();
}

QString CodexTimelineModel::bodyAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).body : QString();
}

int CodexTimelineModel::rowForItem(const QString &id) const
{
    return rowForId(id);
}

int CodexTimelineModel::rowForId(const QString &id) const
{
    const auto iterator = m_rowsById.constFind(id);
    return iterator == m_rowsById.cend() ? -1 : iterator.value();
}

QString CodexTimelineModel::workGroupId(const QString &turnId) const
{
    const QString resolvedTurnId = turnId.isEmpty()
        ? QStringLiteral("current") : turnId;
    return m_activeWorkGroups.value(
        resolvedTurnId, QStringLiteral("work:") + resolvedTurnId);
}

void CodexTimelineModel::startHistoricalWorkSegment(const QString &turnId,
                                                    const QString &messageId)
{
    if (turnId.isEmpty() || !m_activeWorkGroups.contains(turnId))
        return;

    finishWorkGroup(m_activeWorkGroups.value(turnId));
    const int segment = m_workSegmentCounts.value(turnId, 1) + 1;
    m_workSegmentCounts.insert(turnId, segment);
    m_activeWorkGroups.insert(
        turnId,
        QStringLiteral("work:%1:segment:%2:%3").arg(turnId, QString::number(segment),
                                                    messageId));
}

void CodexTimelineModel::rebuildRowIndex()
{
    m_rowsById.clear();
    m_rowsById.reserve(m_entries.size());
    for (int row = 0; row < m_entries.size(); ++row)
        m_rowsById.insert(m_entries.at(row).id, row);
}

void CodexTimelineModel::removeEntryAt(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    rebuildRowIndex();
}

bool CodexTimelineModel::isDuplicateProtocolUserMessage(const Entry &entry) const
{
    if (entry.kind != QStringLiteral("user") || !entry.clientId.isEmpty()
        || entry.body.isEmpty() || m_entries.isEmpty()) {
        return false;
    }

    const Entry &previous = m_entries.constLast();
    return previous.kind == QStringLiteral("user")
        && previous.clientId.isEmpty()
        && previous.turnId == entry.turnId
        && previous.body == entry.body;
}

void CodexTimelineModel::insertOrReplace(Entry entry)
{
    const int row = rowForId(entry.id);
    if (row < 0) {
        beginInsertRows({}, m_entries.size(), m_entries.size());
        m_entries.append(std::move(entry));
        m_rowsById.insert(m_entries.constLast().id, m_entries.size() - 1);
        endInsertRows();
        return;
    }
    const QString streamedBody = m_entries.at(row).body;
    if (entry.body.isEmpty() && !streamedBody.isEmpty())
        entry.body = streamedBody;
    m_entries[row] = std::move(entry);
    emit dataChanged(index(row), index(row));
}

bool CodexTimelineModel::belongsInWork(const Entry &entry)
{
    return entry.kind == QStringLiteral("reasoning")
        || entry.kind == QStringLiteral("plan")
        || entry.kind == QStringLiteral("command")
        || entry.kind == QStringLiteral("tool")
        || entry.kind == QStringLiteral("search")
        || entry.kind == QStringLiteral("image")
        || entry.kind == QStringLiteral("compaction");
}

QVariantMap CodexTimelineModel::activityPresentation(const Entry &entry)
{
    return QVariantMap{
        {QStringLiteral("id"), entry.id},
        {QStringLiteral("kind"), entry.kind},
        {QStringLiteral("title"), entry.title},
        {QStringLiteral("body"), entry.body},
        {QStringLiteral("detail"), entry.detail},
        {QStringLiteral("status"), entry.status},
        {QStringLiteral("running"), entry.running},
        {QStringLiteral("error"), entry.error},
        {QStringLiteral("sources"), entry.sources}
    };
}

void CodexTimelineModel::upsertWorkActivity(Entry activity,
                                            const QString &turnId)
{
    const QString resolvedTurnId = turnId.isEmpty()
        ? QStringLiteral("current") : turnId;
    const QString groupId = workGroupId(resolvedTurnId);
    int row = rowForId(groupId);
    if (row < 0) {
        Entry work;
        work.id = groupId;
        work.turnId = resolvedTurnId;
        work.kind = QStringLiteral("work");
        work.title = QStringLiteral("Working");
        work.status = QStringLiteral("inProgress");
        work.running = true;
        work.timestamp = QDateTime::currentSecsSinceEpoch();
        work.activities.append(activityPresentation(activity));
        if (m_rebuilding) {
            m_entries.append(std::move(work));
            m_rowsById.insert(m_entries.constLast().id, m_entries.size() - 1);
        } else {
            beginInsertRows({}, m_entries.size(), m_entries.size());
            m_entries.append(std::move(work));
            m_rowsById.insert(m_entries.constLast().id, m_entries.size() - 1);
            endInsertRows();
        }
        m_activeWorkGroups.insert(resolvedTurnId, groupId);
        if (!m_workSegmentCounts.contains(resolvedTurnId))
            m_workSegmentCounts.insert(resolvedTurnId, 1);
        return;
    }

    Entry &work = m_entries[row];
    const QVariantMap presentation = activityPresentation(activity);
    for (int index = work.activities.size() - 1; index >= 0; --index) {
        if (work.activities.at(index).toMap().value(QStringLiteral("status")).toString()
            == QStringLiteral("optimistic")) {
            work.activities.removeAt(index);
        }
    }
    bool replaced = false;
    for (int index = 0; index < work.activities.size(); ++index) {
        if (work.activities.at(index).toMap().value(QStringLiteral("id")).toString()
            == activity.id) {
            work.activities[index] = presentation;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        work.activities.append(presentation);
    work.running = true;
    work.status = QStringLiteral("inProgress");
    work.title = QStringLiteral("Working");
    work.elapsed.clear();
    if (!m_rebuilding)
        emit dataChanged(index(row), index(row),
                         {TitleRole, StatusRole, RunningRole, ActivitiesRole, ElapsedRole});
}

CodexTimelineModel::Entry CodexTimelineModel::fromItem(const QJsonObject &item,
                                                       bool completed)
{
    Entry entry;
    entry.id = item.value(QStringLiteral("id")).toString();
    entry.kind = item.value(QStringLiteral("type")).toString();
    entry.phase = item.value(QStringLiteral("phase")).toString();
    entry.status = item.value(QStringLiteral("status")).toString();
    entry.cwd = item.value(QStringLiteral("cwd")).toString();
    entry.timestamp = QDateTime::currentSecsSinceEpoch();
    entry.running = !completed && entry.status != QStringLiteral("completed")
        && entry.status != QStringLiteral("failed")
        && entry.status != QStringLiteral("declined");
    entry.error = entry.status == QStringLiteral("failed");

    if (entry.kind == QStringLiteral("userMessage")) {
        const QString clientId = item.value(QStringLiteral("clientId")).toString();
        entry.clientId = clientId;
        if (!clientId.isEmpty())
            entry.id = clientId;
        entry.kind = QStringLiteral("user");
        entry.title = QStringLiteral("You");
        entry.body = textFromUserContent(item.value(QStringLiteral("content")).toArray());
        entry.running = false;
    } else if (entry.kind == QStringLiteral("agentMessage")) {
        entry.kind = QStringLiteral("agent");
        entry.title = QStringLiteral("Codex");
        entry.body = formattedReviewText(item.value(QStringLiteral("text")).toString());
    } else if (entry.kind == QStringLiteral("plan")) {
        entry.title = QStringLiteral("Plan");
        entry.body = item.value(QStringLiteral("text")).toString();
    } else if (entry.kind == QStringLiteral("reasoning")) {
        entry.title = QStringLiteral("Reasoning");
        entry.body = reasoningText(item.value(QStringLiteral("summary")));
    } else if (entry.kind == QStringLiteral("commandExecution")) {
        entry.kind = QStringLiteral("command");
        entry.title = entry.running ? QStringLiteral("Running command")
                                    : QStringLiteral("Command");
        entry.body = item.value(QStringLiteral("command")).toString();
        entry.detail = item.value(QStringLiteral("aggregatedOutput")).toString();
        const int exitCode = item.value(QStringLiteral("exitCode")).toInt(0);
        if (completed && entry.status == QStringLiteral("completed"))
            entry.title = QStringLiteral("Command finished");
        if (completed && exitCode != 0)
            entry.error = true;
    } else if (entry.kind == QStringLiteral("fileChange")) {
        entry.kind = QStringLiteral("file");
        entry.title = completed ? QStringLiteral("Files changed")
                                : QStringLiteral("Editing files");
        QStringList paths;
        for (const QJsonValue &value : item.value(QStringLiteral("changes")).toArray()) {
            const QJsonObject change = value.toObject();
            const QString path = change.value(QStringLiteral("path")).toString();
            if (path.isEmpty())
                continue;
            const auto [additions, deletions] = diffStats(
                change.value(QStringLiteral("diff")).toString());
            const QFileInfo info(path);
            QVariantMap presentation;
            presentation.insert(QStringLiteral("path"), path);
            presentation.insert(QStringLiteral("name"), info.fileName());
            presentation.insert(QStringLiteral("directory"), info.path() == QStringLiteral(".")
                                                        ? QString() : info.path());
            presentation.insert(QStringLiteral("extension"), info.suffix().left(4).toUpper());
            presentation.insert(QStringLiteral("kind"),
                                change.value(QStringLiteral("kind")).toString());
            presentation.insert(QStringLiteral("additions"), additions);
            presentation.insert(QStringLiteral("deletions"), deletions);
            entry.fileChanges.append(presentation);
            entry.additions += additions;
            entry.deletions += deletions;
            paths.append(path);
        }
        entry.body = paths.join(QStringLiteral("\n"));
    } else if (entry.kind == QStringLiteral("mcpToolCall")
               || entry.kind == QStringLiteral("dynamicToolCall")) {
        entry.kind = QStringLiteral("tool");
        entry.title = completed ? QStringLiteral("Tool finished")
                                : QStringLiteral("Using tool");
        entry.body = item.value(QStringLiteral("tool")).toString();
        if (entry.body.isEmpty())
            entry.body = item.value(QStringLiteral("server")).toString();
    } else if (entry.kind == QStringLiteral("webSearch")) {
        entry.kind = QStringLiteral("search");
        entry.title = entry.running ? QStringLiteral("Searching the web")
                                    : QStringLiteral("Searched the web");
        entry.body = item.value(QStringLiteral("query")).toString();
        entry.sources = searchSources(item);
    } else if (entry.kind == QStringLiteral("imageView")) {
        entry.kind = QStringLiteral("image");
        entry.title = QStringLiteral("Inspected image");
        entry.body = item.value(QStringLiteral("path")).toString();
    } else if (entry.kind == QStringLiteral("enteredReviewMode")
               || entry.kind == QStringLiteral("exitedReviewMode")) {
        // Review lifecycle markers accompany the normal agent message. Showing
        // them would expose protocol internals and duplicate the final review.
        entry.id.clear();
    } else if (entry.kind == QStringLiteral("contextCompaction")) {
        entry.kind = QStringLiteral("compaction");
        entry.title = entry.running ? QStringLiteral("Compacting context")
                                    : QStringLiteral("Context compacted");
        entry.body = entry.running
            ? QStringLiteral("Condensing earlier conversation context")
            : QStringLiteral("Earlier context was condensed for the next turn.");
    } else {
        entry.title = entry.kind.isEmpty() ? QStringLiteral("Activity") : entry.kind;
        entry.kind = QStringLiteral("activity");
        entry.body = item.value(QStringLiteral("text")).toString();
    }
    return entry;
}

CodexModelListModel::CodexModelListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CodexModelListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant CodexModelListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case ModelIdRole: return entry.id;
    case DisplayNameRole: return entry.displayName;
    case DescriptionRole: return entry.description;
    case DefaultEffortRole: return entry.defaultEffort;
    case EffortsRole: return entry.efforts;
    case SupportsFastRole: return entry.supportsFast;
    case DefaultModelRole: return entry.isDefault;
    case SupportsImagesRole: return entry.supportsImages;
    default: return {};
    }
}

QHash<int, QByteArray> CodexModelListModel::roleNames() const
{
    return {{ModelIdRole, "modelId"}, {DisplayNameRole, "displayName"},
            {DescriptionRole, "description"}, {DefaultEffortRole, "defaultEffort"},
            {EffortsRole, "efforts"}, {SupportsFastRole, "supportsFast"},
            {DefaultModelRole, "defaultModel"}, {SupportsImagesRole, "supportsImages"}};
}

void CodexModelListModel::replace(const QJsonArray &models)
{
    QVector<Entry> next;
    next.reserve(models.size());
    for (const QJsonValue &value : models) {
        const QJsonObject model = value.toObject();
        if (model.value(QStringLiteral("hidden")).toBool())
            continue;
        Entry entry;
        entry.id = model.value(QStringLiteral("id")).toString();
        if (entry.id.isEmpty())
            entry.id = model.value(QStringLiteral("model")).toString();
        if (entry.id.isEmpty())
            continue;
        entry.displayName = model.value(QStringLiteral("displayName")).toString(entry.id);
        entry.description = model.value(QStringLiteral("description")).toString();
        entry.defaultEffort = model.value(QStringLiteral("defaultReasoningEffort")).toString();
        entry.isDefault = model.value(QStringLiteral("isDefault")).toBool();
        for (const QJsonValue &effortValue : model.value(QStringLiteral("supportedReasoningEfforts")).toArray()) {
            const QJsonObject effort = effortValue.toObject();
            const QString name = effort.value(QStringLiteral("reasoningEffort")).toString();
            if (!name.isEmpty())
                entry.efforts.append(name);
        }
        for (const QJsonValue &tierValue : model.value(QStringLiteral("serviceTiers")).toArray()) {
            const QJsonObject tier = tierValue.toObject();
            const QString id = tier.value(QStringLiteral("id")).toString();
            if (id.compare(QStringLiteral("fast"), Qt::CaseInsensitive) == 0
                || id.compare(QStringLiteral("priority"), Qt::CaseInsensitive) == 0) {
                entry.supportsFast = true;
            }
        }
        const QJsonArray modalities = model.value(QStringLiteral("inputModalities")).toArray();
        if (!modalities.isEmpty()) {
            entry.supportsImages = std::any_of(modalities.begin(), modalities.end(),
                                               [](const QJsonValue &modality) {
                return modality.toString() == QStringLiteral("image");
            });
        }
        next.append(std::move(entry));
    }
    beginResetModel();
    m_entries = std::move(next);
    endResetModel();
}

QString CodexModelListModel::modelIdAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).id : QString();
}

QString CodexModelListModel::displayNameAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).displayName : QString();
}

QStringList CodexModelListModel::effortsAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).efforts : QStringList();
}

bool CodexModelListModel::supportsFastAt(int row) const
{
    return row >= 0 && row < m_entries.size() && m_entries.at(row).supportsFast;
}

int CodexModelListModel::rowForModel(const QString &modelId) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).id == modelId)
            return row;
    }
    return -1;
}

QString CodexModelListModel::defaultModel() const
{
    for (const Entry &entry : m_entries) {
        if (entry.isDefault)
            return entry.id;
    }
    return m_entries.isEmpty() ? QString() : m_entries.first().id;
}

QString CodexModelListModel::defaultEffortFor(const QString &modelId) const
{
    const int row = rowForModel(modelId);
    return row >= 0 ? m_entries.at(row).defaultEffort : QString();
}

bool CodexModelListModel::supportsFastFor(const QString &modelId) const
{
    const int row = rowForModel(modelId);
    return row >= 0 && m_entries.at(row).supportsFast;
}

CodexAttachmentModel::CodexAttachmentModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CodexAttachmentModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant CodexAttachmentModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case PathRole: return entry.path;
    case NameRole: return entry.name;
    case KindRole: return entry.kind;
    case PreviewUrlRole: return entry.previewUrl;
    default: return {};
    }
}

QHash<int, QByteArray> CodexAttachmentModel::roleNames() const
{
    return {{PathRole, "attachmentPath"}, {NameRole, "attachmentName"},
            {KindRole, "attachmentKind"}, {PreviewUrlRole, "previewUrl"}};
}

bool CodexAttachmentModel::addPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return false;
    const QString absolute = info.absoluteFilePath();
    for (const Entry &entry : m_entries) {
        if (entry.path.compare(absolute, Qt::CaseInsensitive) == 0)
            return true;
    }
    QMimeDatabase database;
    const QString mime = database.mimeTypeForFile(info).name();
    Entry entry;
    entry.path = absolute;
    entry.name = info.fileName();
    entry.kind = mime.startsWith(QStringLiteral("image/"))
        ? QStringLiteral("image")
        : (mime.startsWith(QStringLiteral("audio/")) ? QStringLiteral("audio")
                                                      : QStringLiteral("file"));
    if (entry.kind == QStringLiteral("image"))
        entry.previewUrl = QUrl::fromLocalFile(absolute).toString();
    beginInsertRows({}, m_entries.size(), m_entries.size());
    m_entries.append(std::move(entry));
    endInsertRows();
    return true;
}

void CodexAttachmentModel::removeAt(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
}

void CodexAttachmentModel::clear()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
}
