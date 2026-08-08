#pragma once

#include <QObject>
#include <QString>

class ChatTextStyler final : public QObject
{
    Q_OBJECT

public:
    explicit ChatTextStyler(QObject *parent = nullptr);

    Q_INVOKABLE QString renderMarkdown(const QString &markdown) const;
};
