#include "chatipcserver.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocalSocket>

ChatIpcServer::ChatIpcServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection,
            this, &ChatIpcServer::acceptConnections);
}

ChatIpcServer::~ChatIpcServer()
{
    m_server.close();
}

bool ChatIpcServer::startPrimary()
{
    if (m_server.listen(serverName()))
        return true;

    // Only remove a stale endpoint after proving that no live process answers it.
    if (sendToExisting(QJsonObject{{QStringLiteral("action"), QStringLiteral("ping")}}, 250))
        return false;
    QLocalServer::removeServer(serverName());
    return m_server.listen(serverName());
}

bool ChatIpcServer::sendToExisting(const QJsonObject &message, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(serverName(), QIODevice::WriteOnly);
    if (!socket.waitForConnected(timeoutMs))
        return false;
    QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (socket.write(payload) != payload.size())
        return false;
    socket.flush();
    return socket.waitForBytesWritten(timeoutMs);
}

void ChatIpcServer::broadcast(const QJsonObject &message)
{
    m_lastState = message;
    QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    payload.append('\n');
    for (int index = m_clients.size() - 1; index >= 0; --index) {
        QLocalSocket *client = m_clients.at(index);
        if (!client || client->state() != QLocalSocket::ConnectedState) {
            m_clients.removeAt(index);
            continue;
        }
        client->write(payload);
        client->flush();
    }
}

QString ChatIpcServer::serverName()
{
    return QStringLiteral("Ava.CodexChat.v1");
}

void ChatIpcServer::acceptConnections()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        m_clients.append(socket);
        connect(socket, &QLocalSocket::readyRead, this,
                [this, socket]() { consumeSocket(socket); });
        connect(socket, &QLocalSocket::disconnected,
                socket, &QObject::deleteLater);
        if (!m_lastState.isEmpty()) {
            QByteArray payload = QJsonDocument(m_lastState).toJson(QJsonDocument::Compact);
            payload.append('\n');
            socket->write(payload);
            socket->flush();
        }
    }
}

void ChatIpcServer::consumeSocket(QLocalSocket *socket)
{
    while (socket && socket->canReadLine()) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(socket->readLine().trimmed(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
            handleMessage(document.object());
    }
}

void ChatIpcServer::handleMessage(const QJsonObject &message)
{
    const QString action = message.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("activate")) {
        emit activateRequested();
    } else if (action == QStringLiteral("newChat")) {
        emit newChatRequested(message.value(QStringLiteral("workspacePath")).toString(),
                              message.value(QStringLiteral("useWorktree")).toBool());
    }
}
