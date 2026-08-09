#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

class CodexThreadSnapshotStore final
{
public:
    explicit CodexThreadSnapshotStore(QString storagePath = {});
    ~CodexThreadSnapshotStore();

    QJsonObject thread(const QString &threadId) const;
    void putThread(const QJsonObject &thread);
    void updateViewport(const QString &threadId,
                        const QString &itemId,
                        qreal offset,
                        bool followLiveEdge);
    void removeThread(const QString &threadId);
    bool flush();

    QString storagePath() const { return m_storagePath; }
    int count() const { return m_entries.size(); }

    static QJsonObject boundedThread(const QJsonObject &thread,
                                     int maximumTurns = 48,
                                     qint64 maximumBytes = 2 * 1024 * 1024);

private:
    struct Entry {
        QJsonObject thread;
        qint64 savedAt = 0;
    };

    void load();

    QString m_storagePath;
    QHash<QString, Entry> m_entries;
    bool m_dirty = false;

    friend class CodexModelsTest;
};
