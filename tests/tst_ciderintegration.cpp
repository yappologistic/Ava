#include <QtTest>

#include "ciderintegration.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>

class MockCiderServer final : public QTcpServer {
public:
  explicit MockCiderServer(QObject *parent = nullptr) : QTcpServer(parent) {
    connect(this, &QTcpServer::newConnection, this, [this]() {
      while (QTcpSocket *socket = nextPendingConnection()) {
        const auto request = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request]() {
          request->append(socket->readAll());
          const qsizetype headerEnd = request->indexOf("\r\n\r\n");
          if (headerEnd < 0) {
            return;
          }

          const QByteArray headers = request->left(headerEnd + 4);
          const QRegularExpression contentLengthExpression(
              QStringLiteral("Content-Length:\\s*(\\d+)"),
              QRegularExpression::CaseInsensitiveOption);
          const QRegularExpressionMatch match =
              contentLengthExpression.match(QString::fromLatin1(headers));
          const qsizetype bodyLength =
              match.hasMatch() ? match.captured(1).toLongLong() : 0;
          if (request->size() < headerEnd + 4 + bodyLength) {
            return;
          }

          const QList<QByteArray> requestLine =
              headers.left(headers.indexOf("\r\n")).split(' ');
          const QByteArray method = requestLine.value(0);
          const QByteArray path = requestLine.value(1);
          const QByteArray body = request->mid(headerEnd + 4, bodyLength);
          requestedPaths.append(QString::fromLatin1(method + ' ' + path));

          const bool authorized = headers.contains("apptoken: test-token") ||
                                  headers.contains("apptoken: good-token") ||
                                  headers.contains("apptoken: unsynced-token");
          int status = authorized ? 200 : 403;
          QByteArray response;
          if (!authorized) {
            response = R"({"error":"UNAUTHORIZED_APP_TOKEN"})";
          } else if (path.startsWith("/api/v2/playback/now-playing")) {
            response =
                R"({"data":{"name":"Current","artistName":"Artist","playParams":{"catalogId":"123"}}})";
          } else if (path.startsWith("/api/v2/queue")) {
            if (path.contains("limit=1")) {
              response =
                  R"({"data":{"items":[{"track":{"id":"unrelated","attributes":{"name":"Earlier","artistName":"Artist","playParams":{"id":"unrelated"}}}}],"position":46},"meta":{"offset":0,"limit":1,"total":99}})";
            } else {
              response =
                  R"({"data":{"items":[{"track":{"id":"123","attributes":{"name":"Current","artistName":"Artist","playParams":{"id":"123"}}}},{"track":{"id":"456","attributes":{"name":"Next One","artistName":"First Artist","playParams":{"id":"456"}}}},{"track":{"id":"789","attributes":{"name":"Next Two","artistName":"Second Artist","playParams":{"id":"789"}}}},{"track":{"id":"101","attributes":{"name":"Next Three","artistName":"Third Artist","playParams":{"id":"101"}}}},{"track":{"id":"102","attributes":{"name":"Next Four","artistName":"Fourth Artist","playParams":{"id":"102"}}}},{"track":{"id":"103","attributes":{"name":"Next Five","artistName":"Fifth Artist","playParams":{"id":"103"}}}},{"track":{"id":"104","attributes":{"name":"Next Six","artistName":"Sixth Artist","playParams":{"id":"104"}}}}],"position":46},"meta":{"offset":46,"limit":7,"total":99}})";
            }
          } else if (path == "/api/v2/library/now-playing/status") {
            response = R"({"data":{"inLibrary":true,"rating":1}})";
          } else if (path == "/api/v1/amapi/run-v3") {
            if (body.contains("/v1/me/storefront")) {
              response = R"({"data":[{"id":"ca"}]})";
            } else {
              response =
                  headers.contains("apptoken: unsynced-token")
                      ? R"({"data":[{"attributes":{"ttml":"<tt><body><div><p>First static line</p><p>Second static line</p></div></body></tt>"}}]})"
                      : R"({"data":[{"attributes":{"ttml":"<tt><body><div><p begin=\"00:00:01.000\" end=\"00:00:02.000\">First line</p><p begin=\"00:00:02.000\" end=\"00:00:03.000\">Second line</p></div></body></tt>"}}]})";
            }
          } else {
            response = R"({"data":{}})";
          }

          const QByteArray reason = status == 200 ? "OK" : "Forbidden";
          const QByteArray reply =
              "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason +
              "\r\nContent-Type: application/json\r\nContent-Length: " +
              QByteArray::number(response.size()) +
              "\r\nConnection: close\r\n\r\n" + response;
          socket->write(reply);
          socket->disconnectFromHost();
        });
      }
    });
  }

  QUrl baseUrl() const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort()));
  }

  QStringList requestedPaths;
};

class CiderIntegrationTest final : public QObject {
  Q_OBJECT

private slots:
  void parsesTimedLyrics() {
    const QByteArray ttml = R"(<tt><body><div>
            <p begin="1.25s" end="00:00:03.500"><span> First </span> line </p>
            <p begin="00:00:03.500">Second line</p>
        </div></body></tt>)";
    const QVector<CiderIntegration::LyricLine> lines =
        CiderIntegration::parseTtml(ttml);

    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0).startMilliseconds, 1250);
    QCOMPARE(lines.at(0).endMilliseconds, 3500);
    QCOMPARE(lines.at(0).text, QStringLiteral("First line"));
    QCOMPARE(lines.at(1).startMilliseconds, 3500);
    QCOMPARE(lines.at(1).endMilliseconds, 8500);
  }

  void preservesUnsynchronizedLyrics() {
    const QVector<CiderIntegration::LyricLine> lines = CiderIntegration::parseTtml(
        R"(<tt><body><div><p>First line</p><p>Second line</p></div></body></tt>)");

    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0).startMilliseconds, -1);
    QCOMPARE(lines.at(0).endMilliseconds, -1);
    QCOMPARE(lines.at(1).startMilliseconds, -1);
    QCOMPARE(lines.at(1).endMilliseconds, -1);
  }

  void ignoresOtherMediaProviders() {
    CiderIntegration integration(QUrl(QStringLiteral("http://127.0.0.1:9")),
                                 false);
    integration.setMediaSession(QStringLiteral("Spotify.exe"),
                                QStringLiteral("Track"),
                                QStringLiteral("Artist"), 0, 10000);
    QVERIFY(!integration.active());
    QVERIFY(!integration.connected());
  }

  void cancelsBusyStateWhenCiderStopsBeingActive() {
    CiderIntegration integration(QUrl(QStringLiteral("http://127.0.0.1:9")),
                                 false);
    integration.setMediaSession(QStringLiteral("Cider.exe"),
                                QStringLiteral("Track"),
                                QStringLiteral("Artist"), 0, 10000);
    QVERIFY(integration.busy());

    integration.setMediaSession(QStringLiteral("Spotify.exe"),
                                QStringLiteral("Other track"),
                                QStringLiteral("Other artist"), 0, 10000);
    QVERIFY(!integration.active());
    QVERIFY(!integration.busy());
  }

  void loadsScopedCiderContext() {
    MockCiderServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    CiderIntegration integration(server.baseUrl(), false);

    integration.connectWithToken(QStringLiteral("test-token"));
    integration.setMediaSession(QStringLiteral("CiderCollective.Cider"),
                                QStringLiteral("Current"),
                                QStringLiteral("Artist"), 1500, 3000);

    QTRY_VERIFY_WITH_TIMEOUT(integration.connected(), 3000);
    integration.setLyricsVisible(true);
    QTRY_VERIFY_WITH_TIMEOUT(integration.favoriteAvailable(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(integration.queueAvailable(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(integration.queue().size(), 6, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(integration.lyricsAvailable(), 3000);
    QVERIFY(integration.lyricsSynchronized());

    QVERIFY(integration.favorite());
    QCOMPARE(integration.queue()
                 .at(0)
                 .toMap()
                 .value(QStringLiteral("title"))
                 .toString(),
             QStringLiteral("Next One"));
    QCOMPARE(integration.queue()
                 .at(0)
                 .toMap()
                 .value(QStringLiteral("index"))
                 .toInt(),
             47);
    QCOMPARE(integration.queue()
                 .at(5)
                 .toMap()
                 .value(QStringLiteral("title"))
                 .toString(),
             QStringLiteral("Next Six"));
    QCOMPARE(integration.queue()
                 .at(5)
                 .toMap()
                 .value(QStringLiteral("index"))
                 .toInt(),
             52);
    QCOMPARE(integration.currentLyric(), QStringLiteral("First line"));
    QVERIFY(integration.previousLyric().isEmpty());
    QCOMPARE(integration.nextLyric(), QStringLiteral("Second line"));
    QCOMPARE(integration.upcomingLyrics(),
             QStringList{QStringLiteral("Second line")});
    QTRY_COMPARE_WITH_TIMEOUT(integration.currentLyric(),
                              QStringLiteral("Second line"), 1200);
    QCOMPARE(integration.previousLyric(), QStringLiteral("First line"));
    QVERIFY(integration.upcomingLyrics().isEmpty());

    integration.setMediaSession(QStringLiteral("CiderCollective.Cider"),
                                QStringLiteral("Current"),
                                QStringLiteral("Artist"), 1500, 3000, false);
    QCOMPARE(integration.currentLyric(), QStringLiteral("First line"));
    QTest::qWait(600);
    QCOMPARE(integration.currentLyric(), QStringLiteral("First line"));
    QVERIFY(server.requestedPaths.contains(
        QStringLiteral("GET /api/v2/queue?offset=0&limit=1")));
    QVERIFY(server.requestedPaths.contains(
        QStringLiteral("GET /api/v2/queue?offset=46&limit=7")));

    integration.playQueueIndex(47);
    integration.toggleFavorite();
    QTRY_VERIFY_WITH_TIMEOUT(server.requestedPaths.contains(
                                 QStringLiteral("POST /api/v2/queue/jump")),
                             3000);
    QTRY_VERIFY_WITH_TIMEOUT(server.requestedPaths.contains(QStringLiteral(
                                 "DELETE /api/v2/library/now-playing/rating")),
                             3000);
  }

  void reportsPairingRequirement() {
    MockCiderServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    CiderIntegration integration(server.baseUrl(), false);

    integration.connectWithToken(QStringLiteral("wrong-token"));
    integration.setMediaSession(QStringLiteral("Cider.exe"),
                                QStringLiteral("Current"),
                                QStringLiteral("Artist"), 0, 3000);

    QTRY_VERIFY_WITH_TIMEOUT(integration.pairingRequired(), 3000);
    QVERIFY(!integration.connected());
    QVERIFY(!integration.statusMessage().isEmpty());

    const qsizetype requestCount = server.requestedPaths.size();
    for (int position = 0; position < 1000; ++position) {
      integration.setMediaSession(QStringLiteral("Cider.exe"),
                                  QStringLiteral("Current"),
                                  QStringLiteral("Artist"), position, 3000);
    }
    QCoreApplication::processEvents();
    QCOMPARE(server.requestedPaths.size(), requestCount);
  }

  void distinguishesUnsynchronizedLyrics() {
    MockCiderServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    CiderIntegration integration(server.baseUrl(), false);

    integration.connectWithToken(QStringLiteral("unsynced-token"));
    integration.setMediaSession(QStringLiteral("Cider.exe"),
                                QStringLiteral("Current"),
                                QStringLiteral("Artist"), 1500, 3000);

    QTRY_VERIFY_WITH_TIMEOUT(integration.connected(), 3000);
    integration.setLyricsVisible(true);
    QTRY_VERIFY_WITH_TIMEOUT(integration.lyricsAvailable(), 3000);
    QVERIFY(!integration.lyricsSynchronized());
    QCOMPARE(integration.currentLyric(), QStringLiteral("First static line"));
    QVERIFY(integration.previousLyric().isEmpty());
    QCOMPARE(integration.nextLyric(), QStringLiteral("Second static line"));
    QVERIFY(integration.upcomingLyrics().isEmpty());
  }
};

QTEST_GUILESS_MAIN(CiderIntegrationTest)

#include "tst_ciderintegration.moc"
