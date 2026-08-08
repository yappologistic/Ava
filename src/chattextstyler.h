#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class ChatTextStyler final : public QObject
{
    Q_OBJECT

public:
    explicit ChatTextStyler(QObject *parent = nullptr);

    Q_INVOKABLE QString renderMarkdown(const QString &markdown) const;
    Q_INVOKABLE QVariantList renderSegments(const QString &markdown) const;
    Q_INVOKABLE void copyText(const QString &text) const;
};
