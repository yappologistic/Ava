#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QVector>

class QLocalSocket;

class ChatIpcServer final : public QObject
{
    Q_OBJECT

public:
    explicit ChatIpcServer(QObject *parent = nullptr);
    ~ChatIpcServer() override;

    bool startPrimary();
    static bool sendToExisting(const QJsonObject &message, int timeoutMs = 900);
    void broadcast(const QJsonObject &message);
    static QString serverName();

signals:
    void activateRequested();
    void newChatRequested(const QString &workspacePath, bool useWorktree);

private:
    void acceptConnections();
    void consumeSocket(QLocalSocket *socket);
    void handleMessage(const QJsonObject &message);

    QLocalServer m_server;
    QVector<QPointer<QLocalSocket>> m_clients;
    QJsonObject m_lastState;
};
