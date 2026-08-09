#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

class CodexThreadListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ThreadIdRole = Qt::UserRole + 1,
        TitleRole,
        PreviewRole,
        CwdRole,
        UpdatedAtRole,
        StatusRole,
        PinnedRole
    };

    struct Entry {
        QString id;
        QString title;
        QString preview;
        QString cwd;
        qint64 updatedAt = 0;
        QString status;
        bool pinned = false;
    };

    explicit CodexThreadListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replace(const QJsonArray &threads);
    void replaceSearchResults(const QJsonArray &results);
    void upsert(const QJsonObject &thread);
    void updateStatus(const QString &threadId, const QJsonValue &status);
    void removeById(const QString &threadId);
    Q_INVOKABLE QString threadIdAt(int row) const;
    Q_INVOKABLE QString cwdAt(int row) const;
    int rowForId(const QString &threadId) const;

private:
    static Entry fromJson(const QJsonObject &thread);
    QVector<Entry> m_entries;
};

class CodexTimelineModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ItemIdRole = Qt::UserRole + 1,
        KindRole,
        PhaseRole,
        TitleRole,
        BodyRole,
        DetailRole,
        StatusRole,
        CwdRole,
        TimestampRole,
        RunningRole,
        ErrorRole,
        FileChangesRole,
        AdditionsRole,
        DeletionsRole,
        ActivitiesRole,
        ElapsedRole
    };

    struct Entry {
        QString id;
        QString kind;
        QString phase;
        QString title;
        QString body;
        QString detail;
        QString status;
        QString cwd;
        QVariantList fileChanges;
        QVariantList activities;
        QVariantList sources;
        QString elapsed;
        QString turnId;
        QString clientId;
        int additions = 0;
        int deletions = 0;
        qint64 timestamp = 0;
        bool running = false;
        bool error = false;
    };

    explicit CodexTimelineModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void clear();
    void replaceFromThread(const QJsonObject &thread);
    void beginOptimisticTurn(const QString &clientMessageId,
                             const QString &text);
    void beginOptimisticSteer(const QString &clientMessageId,
                              const QString &text,
                              const QString &turnId);
    void acknowledgeOptimisticTurn(const QString &clientMessageId,
                                    const QString &turnId);
    void failOptimisticTurn(const QString &clientMessageId,
                            const QString &message);
    void upsertItem(const QJsonObject &item, bool completed,
                    const QString &turnId = {});
    void appendAgentDelta(const QString &itemId, const QString &delta);
    void appendWorkDelta(const QString &itemId, const QString &turnId,
                         const QString &kind, const QString &bodyDelta,
                         const QString &detailDelta = {});
    void updatePlan(const QString &turnId, const QJsonArray &plan);
    void completeWork(const QString &turnId, qint64 durationMs);
    void appendError(const QString &message);
    Q_INVOKABLE QString bodyAt(int row) const;
    Q_INVOKABLE int rowForItem(const QString &id) const;

private:
    int rowForId(const QString &id) const;
    QString workGroupId(const QString &turnId) const;
    void finishWorkGroup(const QString &groupId, qint64 durationMs = -1);
    void startHistoricalWorkSegment(const QString &turnId,
                                    const QString &messageId);
    void rebuildRowIndex();
    void removeEntryAt(int row);
    bool isDuplicateProtocolUserMessage(const Entry &entry) const;
    void insertOrReplace(Entry entry);
    void upsertWorkActivity(Entry activity, const QString &turnId);
    static bool belongsInWork(const Entry &entry);
    static QVariantMap activityPresentation(const Entry &entry);
    static Entry fromItem(const QJsonObject &item, bool completed);
    QVector<Entry> m_entries;
    QHash<QString, int> m_rowsById;
    QHash<QString, QString> m_activeWorkGroups;
    QHash<QString, QString> m_previousWorkGroups;
    QHash<QString, int> m_workSegmentCounts;
    bool m_rebuilding = false;
};

class CodexPromptNavigationModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ItemIdRole = Qt::UserRole + 1,
        SourceRowRole,
        PromptTextRole,
        ResponseTextRole
    };

    struct Entry {
        QString itemId;
        QString promptText;
        QString responseText;
        int sourceRow = -1;
    };

    explicit CodexPromptNavigationModel(CodexTimelineModel *source,
                                        QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int promptIndexForSourceRow(int sourceRow) const;
    Q_INVOKABLE int sourceRowAt(int promptIndex) const;

private:
    void rebuild();
    void updateChangedRows(const QModelIndex &topLeft,
                           const QModelIndex &bottomRight,
                           const QList<int> &roles);
    QString finalResponseForPrompt(int promptIndex) const;

    CodexTimelineModel *m_source = nullptr;
    QVector<Entry> m_entries;
};

class CodexModelListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ModelIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        DescriptionRole,
        DefaultEffortRole,
        EffortsRole,
        SupportsFastRole,
        DefaultModelRole,
        SupportsImagesRole
    };

    struct Entry {
        QString id;
        QString displayName;
        QString description;
        QString defaultEffort;
        QStringList efforts;
        bool supportsFast = false;
        bool isDefault = false;
        bool supportsImages = true;
    };

    explicit CodexModelListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replace(const QJsonArray &models);
    Q_INVOKABLE QString modelIdAt(int row) const;
    Q_INVOKABLE QString displayNameAt(int row) const;
    Q_INVOKABLE QStringList effortsAt(int row) const;
    Q_INVOKABLE bool supportsFastAt(int row) const;
    Q_INVOKABLE int rowForModel(const QString &modelId) const;
    QString defaultModel() const;
    QString defaultEffortFor(const QString &modelId) const;
    bool supportsFastFor(const QString &modelId) const;

private:
    QVector<Entry> m_entries;
};

class CodexAttachmentModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        PreviewUrlRole
    };

    struct Entry {
        QString path;
        QString name;
        QString kind;
        QString previewUrl;
    };

    explicit CodexAttachmentModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool addPath(const QString &path);
    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void clear();
    const QVector<Entry> &entries() const { return m_entries; }

private:
    QVector<Entry> m_entries;
};
