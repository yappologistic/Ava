#include "codexmodels.h"

#include <QFileInfo>
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
            {AdditionsRole, "additions"}, {DeletionsRole, "deletions"}};
}

void CodexTimelineModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

void CodexTimelineModel::replaceFromThread(const QJsonObject &thread)
{
    QVector<Entry> next;
    const QJsonArray turns = thread.value(QStringLiteral("turns")).toArray();
    for (const QJsonValue &turnValue : turns) {
        const QJsonObject turn = turnValue.toObject();
        for (const QJsonValue &itemValue : turn.value(QStringLiteral("items")).toArray()) {
            Entry entry = fromItem(itemValue.toObject(), true);
            if (!entry.id.isEmpty())
                next.append(std::move(entry));
        }
    }
    beginResetModel();
    m_entries = std::move(next);
    endResetModel();
}

void CodexTimelineModel::upsertItem(const QJsonObject &item, bool completed)
{
    Entry entry = fromItem(item, completed);
    if (!entry.id.isEmpty())
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
        endInsertRows();
        row = m_entries.size() - 1;
    }
    m_entries[row].body.append(delta);
    m_entries[row].running = true;
    emit dataChanged(index(row), index(row), {BodyRole, RunningRole});
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
    insertOrReplace(std::move(entry));
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
    endInsertRows();
}

QString CodexTimelineModel::bodyAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).body : QString();
}

int CodexTimelineModel::rowForId(const QString &id) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).id == id)
            return row;
    }
    return -1;
}

void CodexTimelineModel::insertOrReplace(Entry entry)
{
    const int row = rowForId(entry.id);
    if (row < 0) {
        beginInsertRows({}, m_entries.size(), m_entries.size());
        m_entries.append(std::move(entry));
        endInsertRows();
        return;
    }
    const QString streamedBody = m_entries.at(row).body;
    if (entry.body.isEmpty() && !streamedBody.isEmpty())
        entry.body = streamedBody;
    m_entries[row] = std::move(entry);
    emit dataChanged(index(row), index(row));
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
        entry.kind = QStringLiteral("user");
        entry.title = QStringLiteral("You");
        entry.body = textFromUserContent(item.value(QStringLiteral("content")).toArray());
        entry.running = false;
    } else if (entry.kind == QStringLiteral("agentMessage")) {
        entry.kind = QStringLiteral("agent");
        entry.title = QStringLiteral("Codex");
        entry.body = item.value(QStringLiteral("text")).toString();
    } else if (entry.kind == QStringLiteral("reasoning")) {
        entry.title = QStringLiteral("Reasoning");
        entry.body = reasoningText(item.value(QStringLiteral("summary")));
        if (entry.body.isEmpty())
            entry.body = QStringLiteral("Working through the details");
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
        entry.title = QStringLiteral("Web search");
        entry.body = item.value(QStringLiteral("query")).toString();
    } else if (entry.kind == QStringLiteral("imageView")) {
        entry.kind = QStringLiteral("image");
        entry.title = QStringLiteral("Inspected image");
        entry.body = item.value(QStringLiteral("path")).toString();
    } else if (entry.kind == QStringLiteral("contextCompaction")) {
        entry.kind = QStringLiteral("activity");
        entry.title = QStringLiteral("Conversation compacted");
        entry.body = QStringLiteral("Codex condensed earlier context to keep working.");
        entry.running = false;
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
