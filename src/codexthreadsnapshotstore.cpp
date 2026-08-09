#include "codexthreadsnapshotstore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {

constexpr int kFormatVersion = 1;
constexpr int kMaximumThreads = 12;
constexpr qint64 kMaximumStoreBytes = 12 * 1024 * 1024;
constexpr int kMaximumTextLength = 64 * 1024;
constexpr int kMaximumContainerEntries = 2048;
constexpr int kMaximumJsonDepth = 64;
constexpr int kMaximumBoundedNodes = 16384;
constexpr qint64 kMaximumThreadMetadataBytes = 256 * 1024;
constexpr qint64 kViewportMetadataReserve = 4 * 1024;
constexpr int kMaximumViewportItemIdLength = 512;

struct BoundedJson {
    QJsonValue value;
    qint64 bytes = 0;
    bool valid = false;
};

struct BoundContext {
    int nodesRemaining = kMaximumBoundedNodes;
};

qint64 compactValueSize(const QJsonValue &value)
{
    return QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).size() - 2;
}

BoundedJson boundedString(const QString &text, qint64 maximumBytes)
{
    if (maximumBytes < 2)
        return {};

    static const QString suffix = QStringLiteral("\n… cached output truncated …");
    const int initialLength = static_cast<int>(
        std::min(text.size(), qsizetype(kMaximumTextLength)));
    QString candidate = text.left(initialLength);
    const bool lengthTruncated = initialLength < text.size();
    if (lengthTruncated)
        candidate.append(suffix);
    qint64 bytes = compactValueSize(candidate);
    if (bytes <= maximumBytes)
        return {candidate, bytes, true};

    int low = 0;
    int high = initialLength;
    int best = -1;
    qint64 bestBytes = 0;
    while (low <= high) {
        const int middle = low + (high - low) / 2;
        const QString shortened = text.left(middle) + suffix;
        const qint64 shortenedBytes = compactValueSize(shortened);
        if (shortenedBytes <= maximumBytes) {
            best = middle;
            bestBytes = shortenedBytes;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    if (best >= 0)
        return {text.left(best) + suffix, bestBytes, true};

    const QString empty;
    bytes = compactValueSize(empty);
    return bytes <= maximumBytes ? BoundedJson{empty, bytes, true} : BoundedJson{};
}

BoundedJson boundedValue(const QJsonValue &value,
                         qint64 maximumBytes,
                         BoundContext &context,
                         int depth = 0,
                         bool preserveRecentArrayValues = false)
{
    if (maximumBytes <= 0 || context.nodesRemaining <= 0 || depth > kMaximumJsonDepth)
        return {};
    --context.nodesRemaining;

    if (value.isString())
        return boundedString(value.toString(), maximumBytes);
    if (!value.isArray() && !value.isObject()) {
        const qint64 bytes = compactValueSize(value);
        return bytes <= maximumBytes ? BoundedJson{value, bytes, true} : BoundedJson{};
    }

    if (value.isArray()) {
        QJsonArray result;
        QList<QJsonValue> recentValues;
        const QJsonArray values = value.toArray();
        qint64 bytes = 2;
        if (bytes > maximumBytes)
            return {};

        const int limit = static_cast<int>(
            std::min(values.size(), qsizetype(kMaximumContainerEntries)));
        for (int processed = 0; processed < limit; ++processed) {
            const int index = preserveRecentArrayValues
                ? values.size() - 1 - processed
                : processed;
            const bool isEmpty = preserveRecentArrayValues
                ? recentValues.isEmpty()
                : result.isEmpty();
            const qint64 separatorBytes = isEmpty ? 0 : 1;
            const qint64 remaining = maximumBytes - bytes - separatorBytes;
            if (remaining < 2)
                break;
            const BoundedJson child = boundedValue(values.at(index),
                                                   remaining,
                                                   context,
                                                   depth + 1);
            if (!child.valid)
                continue;
            if (preserveRecentArrayValues)
                recentValues.append(child.value);
            else
                result.append(child.value);
            bytes += separatorBytes + child.bytes;
        }
        for (auto iterator = recentValues.crbegin(); iterator != recentValues.crend(); ++iterator)
            result.append(*iterator);
        return {result, bytes, true};
    }

    QJsonObject result;
    const QJsonObject object = value.toObject();
    qint64 bytes = 2;
    if (bytes > maximumBytes)
        return {};

    int processed = 0;
    QSet<QString> handled;
    auto appendMember = [&](const QString &key) {
        if (!object.contains(key) || processed >= kMaximumContainerEntries)
            return;
        ++processed;
        handled.insert(key);
        const qint64 keyBytes = compactValueSize(key);
        const qint64 separatorBytes = result.isEmpty() ? 0 : 1;
        const qint64 remaining = maximumBytes - bytes - separatorBytes - keyBytes - 1;
        if (remaining < 2)
            return;
        const BoundedJson child = boundedValue(object.value(key),
                                               remaining,
                                               context,
                                               depth + 1,
                                               key == QStringLiteral("items")
                                                   || key == QStringLiteral("turns"));
        if (!child.valid)
            return;
        result.insert(key, child.value);
        bytes += separatorBytes + keyBytes + 1 + child.bytes;
    };

    static const QStringList priorityKeys{
        QStringLiteral("id"),
        QStringLiteral("type"),
        QStringLiteral("status"),
        QStringLiteral("role"),
        QStringLiteral("name"),
        QStringLiteral("text"),
        QStringLiteral("content"),
        QStringLiteral("items")
    };
    for (const QString &key : priorityKeys)
        appendMember(key);
    for (auto iterator = object.constBegin();
         iterator != object.constEnd() && processed < kMaximumContainerEntries;
         ++iterator) {
        if (!handled.contains(iterator.key()))
            appendMember(iterator.key());
    }
    return {result, bytes, true};
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

    const Entry previous = m_entries.value(threadId);
    QJsonObject source = thread;
    for (const QString &key : {QStringLiteral("_avaViewportItemId"),
                               QStringLiteral("_avaViewportOffset"),
                               QStringLiteral("_avaFollowLiveEdge")}) {
        if (!source.contains(key) && previous.thread.contains(key))
            source.insert(key, previous.thread.value(key));
    }
    QJsonObject bounded = boundedThread(source);
    if (bounded.value(QStringLiteral("id")).toString() != threadId)
        return;
    if (previous.thread == bounded)
        return;

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
    const QString boundedItemId = itemId.left(kMaximumViewportItemIdLength);
    if (iterator->thread.contains(QStringLiteral("_avaViewportItemId"))
        && iterator->thread.contains(QStringLiteral("_avaViewportOffset"))
        && iterator->thread.contains(QStringLiteral("_avaFollowLiveEdge"))
        && iterator->thread.value(QStringLiteral("_avaViewportItemId")).toString()
            == boundedItemId
        && iterator->thread.value(QStringLiteral("_avaViewportOffset")).toDouble() == offset
        && iterator->thread.value(QStringLiteral("_avaFollowLiveEdge")).toBool()
            == followLiveEdge) {
        return;
    }
    iterator->thread.insert(QStringLiteral("_avaViewportItemId"), boundedItemId);
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

    const QByteArray prefix = QByteArrayLiteral("{\"version\":")
        + QByteArray::number(kFormatVersion)
        + QByteArrayLiteral(",\"entries\":[");
    const QByteArray suffix = QByteArrayLiteral("]}");
    qint64 bytesWritten = prefix.size();
    QList<QString> persistedIds;

    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(prefix) != prefix.size())
        return false;

    for (const QString &id : std::as_const(ids)) {
        const Entry &entry = m_entries.value(id);
        const QByteArray encoded = QJsonDocument(QJsonObject{
            {QStringLiteral("savedAt"), entry.savedAt},
            {QStringLiteral("thread"), entry.thread}
        }).toJson(QJsonDocument::Compact);
        const qint64 separatorBytes = persistedIds.isEmpty() ? 0 : 1;
        if (bytesWritten + separatorBytes + encoded.size() + suffix.size()
            > kMaximumStoreBytes) {
            break;
        }
        if (separatorBytes > 0 && file.write(QByteArrayLiteral(",")) != 1)
            return false;
        if (file.write(encoded) != encoded.size())
            return false;
        bytesWritten += separatorBytes + encoded.size();
        persistedIds.append(id);
    }
    if (file.write(suffix) != suffix.size())
        return false;
    if (!file.commit())
        return false;

    QSet<QString> persisted;
    persisted.reserve(persistedIds.size());
    for (const QString &id : std::as_const(persistedIds))
        persisted.insert(id);
    for (auto iterator = m_entries.begin(); iterator != m_entries.end();) {
        if (!persisted.contains(iterator.key()))
            iterator = m_entries.erase(iterator);
        else
            ++iterator;
    }
    m_dirty = false;
    return true;
}

QJsonObject CodexThreadSnapshotStore::boundedThread(const QJsonObject &thread,
                                                     int maximumTurns,
                                                     qint64 maximumBytes)
{
    if (maximumBytes < 2)
        return {};

    QJsonObject metadata = thread;
    const QJsonArray sourceTurns = metadata.take(QStringLiteral("turns")).toArray();
    BoundContext context;
    const qint64 boundedMaximumBytes = maximumBytes > kViewportMetadataReserve + 2
        ? maximumBytes - kViewportMetadataReserve
        : maximumBytes;
    const qint64 metadataBudget = std::min(kMaximumThreadMetadataBytes,
                                           boundedMaximumBytes / 4);
    const BoundedJson boundedMetadata = boundedValue(metadata,
                                                     metadataBudget,
                                                     context);
    QJsonObject bounded = boundedMetadata.valid
        ? boundedMetadata.value.toObject()
        : QJsonObject{};
    QJsonArray turns;
    bounded.insert(QStringLiteral("turns"), turns);

    qint64 bytes = compactSize(bounded);
    if (bytes > boundedMaximumBytes) {
        bounded.remove(QStringLiteral("turns"));
        return compactSize(bounded) <= boundedMaximumBytes ? bounded : QJsonObject{};
    }

    const int turnLimit = std::max(0, maximumTurns);
    const int sourceTurnCount = static_cast<int>(sourceTurns.size());
    const int first = std::max(0, sourceTurnCount - turnLimit);
    for (int index = sourceTurnCount - 1; index >= first; --index) {
        const qint64 separatorBytes = turns.isEmpty() ? 0 : 1;
        const qint64 remaining = boundedMaximumBytes - bytes - separatorBytes;
        if (remaining < 2 || context.nodesRemaining <= 0)
            break;
        const BoundedJson turn = boundedValue(sourceTurns.at(index),
                                              remaining,
                                              context);
        if (!turn.valid || (!sourceTurns.at(index).toObject().isEmpty()
                            && turn.value.toObject().isEmpty())) {
            break;
        }
        turns.insert(0, turn.value);
        bytes += separatorBytes + turn.bytes;
    }
    bounded.insert(QStringLiteral("turns"), turns);
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
        entry.thread = boundedThread(thread);
        if (entry.thread.value(QStringLiteral("id")).toString() != threadId)
            continue;
        entry.savedAt = static_cast<qint64>(
            object.value(QStringLiteral("savedAt")).toDouble());
        m_entries.insert(threadId, std::move(entry));
        if (m_entries.value(threadId).thread != thread)
            m_dirty = true;
        if (m_entries.size() >= kMaximumThreads)
            break;
    }
}
