#pragma once

#include <QAbstractListModel>
#include <QDateTime>
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
    void upsert(const QJsonObject &thread);
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
        DeletionsRole
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
    void upsertItem(const QJsonObject &item, bool completed);
    void appendAgentDelta(const QString &itemId, const QString &delta);
    void updatePlan(const QString &turnId, const QJsonArray &plan);
    void appendError(const QString &message);
    Q_INVOKABLE QString bodyAt(int row) const;

private:
    int rowForId(const QString &id) const;
    void insertOrReplace(Entry entry);
    static Entry fromItem(const QJsonObject &item, bool completed);
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
