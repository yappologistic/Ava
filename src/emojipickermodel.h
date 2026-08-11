#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <array>

class EmojiPickerModel final : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultsChanged)
    Q_PROPERTY(int columnCount READ columnCount WRITE setColumnCount NOTIFY columnCountChanged)
    Q_PROPERTY(int defaultSkinTone READ defaultSkinTone WRITE setDefaultSkinTone NOTIFY defaultSkinToneChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum Role
    {
        GlyphRole = Qt::UserRole + 1,
        NameRole,
        CategoryRole,
        SubgroupRole,
        CodePointsRole,
        KeywordsRole,
        PinnedRole,
        SupportsSkinToneRole,
        SymbolRole
    };

    explicit EmojiPickerModel(QObject *parent = nullptr);
    EmojiPickerModel(const QString &emojiTestPath,
                     const QString &annotationsPath,
                     const QString &derivedAnnotationsPath,
                     QObject *parent = nullptr);
    ~EmojiPickerModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool active() const { return m_active; }
    bool loading() const { return m_loading; }
    QString query() const { return m_query; }
    QString category() const { return m_category; }
    QStringList categories() const { return m_categories; }
    int resultCount() const { return m_filteredIndices.size(); }
    int columnCount() const { return m_columnCount; }
    int defaultSkinTone() const { return m_defaultSkinTone; }
    QString errorMessage() const { return m_errorMessage; }

public slots:
    void setActive(bool active);
    void openPicker();
    void closePicker();
    void setQuery(const QString &query);
    void setCategory(const QString &category);
    void setColumnCount(int columnCount);
    void setDefaultSkinTone(int tone);

    void paste(int row, bool keepOpen, int toneIndex);
    void copy(int row, int toneIndex);
    void copyUnicode(int row);
    void togglePinned(int row);
    void setCustomKeywords(int row, const QString &keywords);

    Q_INVOKABLE QVariantMap itemAt(int row) const;
    Q_INVOKABLE QStringList skinToneVariants(int row) const;
    Q_INVOKABLE QString customKeywords(int row) const;

signals:
    void activeChanged();
    void loadingChanged();
    void queryChanged();
    void categoryChanged();
    void resultsChanged();
    void columnCountChanged();
    void defaultSkinToneChanged();
    void errorMessageChanged();
    void pasteRequested(const QString &text, bool keepOpen);
    void dismissRequested();
    void statusMessageRequested(const QString &message);

private:
    struct Entry
    {
        QString glyph;
        QString name;
        QString category;
        QString subgroup;
        QString codePoints;
        QString key;
        QStringList keywords;
        QString searchText;
        std::array<QString, 5> toneVariants;
        bool symbol = false;
        int catalogOrder = 0;
    };

    struct CatalogResult
    {
        QVector<Entry> entries;
        QString error;
    };

    void startLoading(const QString &emojiTestPath,
                      const QString &annotationsPath,
                      const QString &derivedAnnotationsPath);
    static CatalogResult loadCatalog(const QString &emojiTestPath,
                                     const QString &annotationsPath,
                                     const QString &derivedAnnotationsPath);
    void rebuildResults();
    void updateSearchText(Entry &entry);
    const Entry *entryAt(int row) const;
    Entry *entryAt(int row);
    QString glyphForTone(const Entry &entry, int toneIndex) const;
    void recordRecent(const Entry &entry);
    void loadPreferences();
    void savePinned() const;
    void saveRecents() const;
    void saveCustomKeywords() const;
    void setLoading(bool loading);
    void setErrorMessage(const QString &message);

    QVector<Entry> m_entries;
    QVector<int> m_filteredIndices;
    QStringList m_categories;
    QStringList m_pinnedKeys;
    QStringList m_recentKeys;
    QHash<QString, QString> m_customKeywords;
    QString m_query;
    QString m_category = QStringLiteral("All");
    QString m_errorMessage;
    bool m_active = false;
    bool m_loading = false;
    int m_columnCount = 8;
    int m_defaultSkinTone = 0;
    QTimer m_recentsSaveTimer;
};
