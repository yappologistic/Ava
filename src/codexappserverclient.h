#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QStringList>

class CodexAppServerClient final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString executablePath READ executablePath NOTIFY executableChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

public:
    explicit CodexAppServerClient(QObject *parent = nullptr);
    ~CodexAppServerClient() override;

    bool available() const { return !m_candidates.isEmpty(); }
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    bool ready() const { return m_ready; }
    QString executablePath() const { return m_executablePath; }
    QString errorMessage() const { return m_errorMessage; }

    qint64 request(const QString &method, const QJsonObject &params = {});
    void notify(const QString &method, const QJsonObject &params = {});
    void respond(const QJsonValue &id, const QJsonObject &result);
    void respondError(const QJsonValue &id, int code, const QString &message);

    static QStringList discoverExecutables();

public slots:
    void start();
    void stop();
    void restart();

signals:
    void availabilityChanged();
    void runningChanged();
    void readyChanged();
    void executableChanged();
    void errorChanged();
    void responseReceived(qint64 id,
                          const QString &method,
                          const QJsonObject &result,
                          const QJsonObject &error);
    void notificationReceived(const QString &method, const QJsonObject &params);
    void serverRequestReceived(const QJsonValue &id,
                               const QString &method,
                               const QJsonObject &params);
    void standardErrorReceived(const QString &text);
    void protocolWarning(const QString &message);

private:
    void startCandidate(int index);
    void sendObject(const QJsonObject &object);
    void consumeStandardOutput();
    void consumeLine(const QByteArray &line);
    void processPendingLines();
    void resetParsingState();
    void dispatchDocument(const QJsonDocument &document);
    void setReady(bool ready);
    void setError(const QString &message);
    void clearError();

    QProcess m_process;
    QByteArray m_outputBuffer;
    QQueue<QByteArray> m_pendingLines;
    QString m_standardErrorTail;
    QHash<qint64, QString> m_pendingMethods;
    QStringList m_candidates;
    QString m_executablePath;
    QString m_errorMessage;
    qint64 m_nextRequestId = 1;
    qint64 m_initializeRequestId = 0;
    quint64 m_parseGeneration = 0;
    int m_candidateIndex = -1;
    bool m_ready = false;
    bool m_stopping = false;
    bool m_seenProcessStart = false;
    bool m_largeParseInFlight = false;
    int m_backgroundParsesInFlight = 0;

    friend class CodexModelsTest;
};
