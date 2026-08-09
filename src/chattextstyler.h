#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>

class ChatTextStyler final : public QObject
{
    Q_OBJECT

public:
    explicit ChatTextStyler(QObject *parent = nullptr);

    Q_INVOKABLE QString renderMarkdown(const QString &markdown) const;
    Q_INVOKABLE QVariantList renderSegments(const QString &markdown) const;
    Q_INVOKABLE void copyText(const QString &text) const;

private:
    struct CacheEntry {
        QVariant value;
        qsizetype bytes = 0;
    };

    QVariant cachedRender(const QString &key) const;
    void cacheRender(const QString &key, const QVariant &value) const;

    static constexpr qsizetype kMaximumCacheEntries = 192;
    static constexpr qsizetype kMaximumCacheBytes = 8 * 1024 * 1024;

    mutable QHash<QString, CacheEntry> m_renderCache;
    mutable QStringList m_cacheRecency;
    mutable qsizetype m_renderCacheBytes = 0;
    mutable quint64 m_renderCacheHits = 0;
    mutable quint64 m_renderCacheMisses = 0;

    friend class ChatTextStylerTest;
};
