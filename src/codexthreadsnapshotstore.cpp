#include "codexthreadsnapshotstore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {

constexpr int kFormatVersion = 1;
constexpr int kMaximumThreads = 12;
constexpr qint64 kMaximumStoreBytes = 12 * 1024 * 1024;
constexpr int kMaximumTextLength = 64 * 1024;

QJsonValue boundedValue(const QJsonValue &value)
{
    if (value.isString()) {
        const QString text = value.toString();
        if (text.size() <= kMaximumTextLength)
            return value;
        return text.left(kMaximumTextLength)
            + QStringLiteral("\n… cached output truncated …");
    }
    if (value.isArray()) {
        QJsonArray result;
        const QJsonArray values = value.toArray();
        for (const QJsonValue &child : values)
            result.append(boundedValue(child));
        return result;
    }
    if (value.isObject()) {
        QJsonObject result;
        const QJsonObject object = value.toObject();
        for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator)
            result.insert(iterator.key(), boundedValue(iterator.value()));
        return result;
    }
    return value;
}

qint64 compactSize(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact).size();
}

} // namespace

CodexThreadSnapshotStore::CodexThreadSnapshotStore(QString storagePath)
    : m_storagePath(std::move(storagePath))
{
    if (m_storagePath.isEmpty()) {
        const QString overridePath = qEnvironmentVariable("AVA_CODEX_THREAD_CACHE_PATH");
        m_storagePath = overridePath.isEmpty()
            ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                  .filePath(QStringLiteral("codex-thread-snapshots.json"))
            : overridePath;
    }
    load();
}

CodexThreadSnapshotStore::~CodexThreadSnapshotStore()
{
    flush();
}

QJsonObject CodexThreadSnapshotStore::thread(const QString &threadId) const
{
    return m_entries.value(threadId).thread;
}

void CodexThreadSnapshotStore::putThread(const QJsonObject &thread)
{
    const QString threadId = thread.value(QStringLiteral("id")).toString();
    if (threadId.isEmpty())
        return;

    QJsonObject bounded = boundedThread(thread);
    const Entry previous = m_entries.value(threadId);
    for (const QString &key : {QStringLiteral("_avaViewportItemId"),
                               QStringLiteral("_avaViewportOffset"),
                               QStringLiteral("_avaFollowLiveEdge")}) {
        if (!bounded.contains(key) && previous.thread.contains(key))
            bounded.insert(key, previous.thread.value(key));
    }

    Entry entry;
    entry.thread = std::move(bounded);
    entry.savedAt = QDateTime::currentMSecsSinceEpoch();
    m_entries.insert(threadId, std::move(entry));

    if (m_entries.size() > kMaximumThreads) {
        QList<QString> ids = m_entries.keys();
        std::sort(ids.begin(), ids.end(), [this](const QString &left,
                                                  const QString &right) {
            return m_entries.value(left).savedAt > m_entries.value(right).savedAt;
        });
        while (ids.size() > kMaximumThreads)
            m_entries.remove(ids.takeLast());
    }
    m_dirty = true;
}

void CodexThreadSnapshotStore::updateViewport(const QString &threadId,
                                               const QString &itemId,
                                               qreal offset,
                                               bool followLiveEdge)
{
    auto iterator = m_entries.find(threadId);
    if (iterator == m_entries.end())
        return;
    iterator->thread.insert(QStringLiteral("_avaViewportItemId"), itemId);
    iterator->thread.insert(QStringLiteral("_avaViewportOffset"), offset);
    iterator->thread.insert(QStringLiteral("_avaFollowLiveEdge"), followLiveEdge);
    iterator->savedAt = QDateTime::currentMSecsSinceEpoch();
    m_dirty = true;
}

void CodexThreadSnapshotStore::removeThread(const QString &threadId)
{
    if (m_entries.remove(threadId))
        m_dirty = true;
}

bool CodexThreadSnapshotStore::flush()
{
    if (!m_dirty)
        return true;

    const QFileInfo info(m_storagePath);
    if (!QDir().mkpath(info.absolutePath()))
        return false;

    QList<QString> ids = m_entries.keys();
    std::sort(ids.begin(), ids.end(), [this](const QString &left,
                                              const QString &right) {
        return m_entries.value(left).savedAt > m_entries.value(right).savedAt;
    });

    QJsonArray entries;
    for (const QString &id : std::as_const(ids)) {
        const Entry &entry = m_entries.value(id);
        entries.append(QJsonObject{{QStringLiteral("savedAt"), entry.savedAt},
                                   {QStringLiteral("thread"), entry.thread}});
    }
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("entries"), entries}
    });

    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(document.toJson(QJsonDocument::Compact)) < 0)
        return false;
    if (!file.commit())
        return false;
    m_dirty = false;
    return true;
}

QJsonObject CodexThreadSnapshotStore::boundedThread(const QJsonObject &thread,
                                                     int maximumTurns,
                                                     qint64 maximumBytes)
{
    QJsonObject bounded = boundedValue(thread).toObject();
    QJsonArray turns = bounded.value(QStringLiteral("turns")).toArray();
    if (turns.size() > maximumTurns) {
        QJsonArray recent;
        const int first = turns.size() - maximumTurns;
        for (int index = first; index < turns.size(); ++index)
            recent.append(turns.at(index));
        turns = recent;
        bounded.insert(QStringLiteral("turns"), turns);
    }

    while (turns.size() > 1 && compactSize(bounded) > maximumBytes) {
        turns.removeFirst();
        bounded.insert(QStringLiteral("turns"), turns);
    }
    return bounded;
}

void CodexThreadSnapshotStore::load()
{
    QFile file(m_storagePath);
    if (!file.exists() || file.size() <= 0 || file.size() > kMaximumStoreBytes
        || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != kFormatVersion)
        return;

    const QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        const QJsonObject thread = object.value(QStringLiteral("thread")).toObject();
        const QString threadId = thread.value(QStringLiteral("id")).toString();
        if (threadId.isEmpty())
            continue;
        Entry entry;
        entry.thread = thread;
        entry.savedAt = static_cast<qint64>(
            object.value(QStringLiteral("savedAt")).toDouble());
        m_entries.insert(threadId, std::move(entry));
        if (m_entries.size() >= kMaximumThreads)
            break;
    }
}
