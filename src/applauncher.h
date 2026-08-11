#pragma once

#include <QAbstractListModel>
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QSet>
#include <QString>
#include <QVector>

#include <optional>

class AppLauncher final : public QAbstractListModel,
                          public QAbstractNativeEventFilter
{
    Q_OBJECT

    Q_PROPERTY(bool open READ isOpen WRITE setOpen NOTIFY openChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool shortcutRegistered READ shortcutRegistered NOTIFY shortcutRegisteredChanged)
    Q_PROPERTY(bool pasteDismissPending READ pasteDismissPending NOTIFY pasteDismissPendingChanged)

public:
    struct AppEntry
    {
        QString id;
        QString name;
        QString subtitle;
        QString launchTarget;
        QString iconSource;
        QString searchText;
        QByteArray itemIdList;
        qint64 lastLaunched = 0;
        int launchCount = 0;
    };

    enum Role
    {
        AppIdRole = Qt::UserRole + 1,
        AppNameRole,
        SubtitleRole,
        IconSourceRole
    };

    explicit AppLauncher(QObject *parent = nullptr);
    ~AppLauncher() override;

    static std::optional<AppEntry> directEntryForQuery(const QString &query);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool isOpen() const { return m_open; }
    bool loading() const { return m_loading; }
    QString query() const { return m_query; }
    int resultCount() const { return m_filteredIndices.size(); }
    QString errorMessage() const { return m_errorMessage; }
    bool shortcutRegistered() const { return m_shortcutRegistered; }
    bool pasteDismissPending() const { return m_pasteDismissPending; }

    void setWindowHandle(quintptr nativeHandle);
    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

public slots:
    void setOpen(bool open);
    void setQuery(const QString &query);
    void openLauncher();
    void closeLauncher();
    void toggleLauncher();
    void refresh();
    void requestIcon(const QString &appId);
    bool launch(int row);
    void pasteText(const QString &text, bool keepOpen);

signals:
    void openChanged();
    void loadingChanged();
    void queryChanged();
    void resultsChanged();
    void errorMessageChanged();
    void shortcutRegisteredChanged();
    void pasteDismissPendingChanged();
    void applicationLaunched(const QString &name);
    void launchFailed(const QString &name, const QString &reason);
    void emojiPickerRequested();

private:
    void applyEntries(QVector<AppEntry> entries);
    void rebuildResults();
    void setLoading(bool loading);
    void setErrorMessage(const QString &message);
    void setPasteDismissPending(bool pending);
    void closeInternal(bool restorePreviousWindow, bool preservePasteDismiss = false);
    void activateLauncherWindow();
    void deactivateLauncherWindow(bool restorePreviousWindow);
    void registerShortcut();
    void unregisterShortcut();
    int matchScore(const AppEntry &entry, const QString &normalizedQuery) const;
    void loadUsage(AppEntry &entry) const;
    void recordUsage(AppEntry &entry);

    QVector<AppEntry> m_entries;
    QVector<int> m_filteredIndices;
    std::optional<AppEntry> m_directEntry;
    QString m_query;
    QString m_errorMessage;
    bool m_open = false;
    bool m_loading = false;
    bool m_shortcutRegistered = false;
    bool m_refreshInFlight = false;
    bool m_pasteDismissPending = false;
    QSet<QString> m_iconRequests;
    quintptr m_windowHandle = 0;
    quintptr m_previousForegroundWindow = 0;
    qint64 m_ignoreFocusLossUntil = 0;
};
