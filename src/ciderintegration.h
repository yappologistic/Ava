#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVector>

#include <functional>
#include <memory>

class CiderIntegration final : public QObject {
  Q_OBJECT

  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(bool connected READ connected NOTIFY changed)
  Q_PROPERTY(bool pairingRequired READ pairingRequired NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool favorite READ favorite NOTIFY changed)
  Q_PROPERTY(bool favoriteAvailable READ favoriteAvailable NOTIFY changed)
  Q_PROPERTY(bool queueAvailable READ queueAvailable NOTIFY changed)
  Q_PROPERTY(bool queueEditable READ queueEditable NOTIFY changed)
  Q_PROPERTY(QVariantList queue READ queue NOTIFY changed)
  Q_PROPERTY(QVariantList playlists READ playlists NOTIFY changed)
  Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY changed)
  Q_PROPERTY(QVariantList recentlyPlayed READ recentlyPlayed NOTIFY changed)
  Q_PROPERTY(bool browserBusy READ browserBusy NOTIFY changed)
  Q_PROPERTY(QString browserMessage READ browserMessage NOTIFY changed)
  Q_PROPERTY(QString lastAddedPlaylistId READ lastAddedPlaylistId NOTIFY changed)
  Q_PROPERTY(qreal audioLevel READ audioLevel NOTIFY audioLevelChanged)
  Q_PROPERTY(bool lyricsAvailable READ lyricsAvailable NOTIFY changed)
  Q_PROPERTY(bool lyricsSynchronized READ lyricsSynchronized NOTIFY changed)
  Q_PROPERTY(QString previousLyric READ previousLyric NOTIFY changed)
  Q_PROPERTY(QString currentLyric READ currentLyric NOTIFY changed)
  Q_PROPERTY(QString nextLyric READ nextLyric NOTIFY changed)
  Q_PROPERTY(QStringList upcomingLyrics READ upcomingLyrics NOTIFY changed)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)

public:
  struct LyricLine {
    qint64 startMilliseconds = -1;
    qint64 endMilliseconds = -1;
    QString text;

    bool operator==(const LyricLine &) const = default;
  };

  explicit CiderIntegration(QObject *parent = nullptr);
  ~CiderIntegration() override;
  CiderIntegration(const QUrl &baseUrl, bool usePersistentCredentials,
                   QObject *parent = nullptr);
  CiderIntegration(const QUrl &baseUrl, bool usePersistentCredentials,
                   const QUrl &appleMusicBaseUrl, QObject *parent = nullptr);

  bool active() const { return m_active; }
  bool connected() const { return m_connected; }
  bool pairingRequired() const { return m_pairingRequired; }
  bool busy() const { return m_busy; }
  bool favorite() const { return m_rating == 1; }
  bool favoriteAvailable() const { return m_favoriteAvailable; }
  bool queueAvailable() const { return m_queueAvailable; }
  bool queueEditable() const {
    return m_queueAvailable && m_apiVersion == ApiVersion::V2;
  }
  QVariantList queue() const { return m_queue; }
  QVariantList playlists() const { return m_playlists; }
  QVariantList searchResults() const { return m_searchResults; }
  QVariantList recentlyPlayed() const { return m_recentlyPlayed; }
  bool browserBusy() const { return m_browserBusy; }
  QString browserMessage() const { return m_browserMessage; }
  QString lastAddedPlaylistId() const { return m_lastAddedPlaylistId; }
  qreal audioLevel() const { return m_audioLevel; }
  bool lyricsAvailable() const { return !m_lyrics.isEmpty(); }
  bool lyricsSynchronized() const { return m_lyricsSynchronized; }
  QString previousLyric() const { return m_previousLyric; }
  QString currentLyric() const { return m_currentLyric; }
  QString nextLyric() const { return m_nextLyric; }
  QStringList upcomingLyrics() const { return m_upcomingLyrics; }
  QString statusMessage() const { return m_statusMessage; }

  void setMediaSession(const QString &source, const QString &title,
                       const QString &artist, qint64 positionMilliseconds,
                       qint64 durationMilliseconds, bool playing = true);
  void setVisualTestState(const QString &state);
#ifdef AVA_TESTING
  bool audioMeterActiveForTest();
#endif

  static QVector<LyricLine> parseTtml(const QByteArray &ttml);

public slots:
  void connectFromClipboard();
  void connectWithToken(const QString &token);
  void forgetConnection();
  void refreshQueue();
  void removeQueueIndex(int index);
  void moveQueueIndex(int from, int to);
  void refreshPlaylists();
  void addCurrentTrackToPlaylist(const QString &playlistId);
  void search(const QString &query);
  void refreshRecentlyPlayed();
  void playMediaItem(const QString &type, const QString &id,
                     const QString &href = {},
                     const QString &resourceType = {});
  void refreshLyrics();
  void setLyricsVisible(bool visible);
  void setAudioPulseVisible(bool visible);
  void setPanelVisible(bool visible);
  void toggleFavorite();
  void playQueueIndex(int index);

signals:
  void changed();
  void audioLevelChanged();

private:
  enum class ApiVersion { Unknown, V1, V2 };

  struct NetworkResult {
    int statusCode = 0;
    QByteArray body;
    QString error;
    bool tooLarge = false;
  };

  using NetworkHandler = std::function<void(NetworkResult)>;

  void refreshNowPlaying();
  void refreshNowPlayingV1(quint64 generation);
  void refreshFavorite();
  void requestQueueWindow(quint64 generation, int offset, bool skipCurrent);
  void applyQueueResult(NetworkResult result, quint64 generation, int offset,
                        bool skipCurrent);
  void ensureAppleMusicCredentials(quint64 generation,
                                   std::function<void(bool)> continuation);
  void requestLyrics(quint64 generation);
  void performSearch(const QString &query, quint64 generation);
  void sendAppleMusicRequest(const QString &path, const QByteArray &method,
                             const QJsonDocument &body,
                             const QByteArray &developerToken,
                             const QByteArray &userToken,
                             NetworkHandler handler);
  void sendNetworkRequest(QNetworkRequest request, const QByteArray &method,
                          const QJsonDocument &body, NetworkHandler handler);
  void setBrowserBusy(bool busy);
  void setBrowserMessage(const QString &message);
  void updateAudioMeterState();
  void probeClientAvailability();
  void updateLyricsForPosition();
  void scheduleNextLyricBoundary();
  qint64 effectivePositionMilliseconds() const;
  void clearContent();
  void setStatus(const QString &message);
  void setBusy(bool busy);
  void handleAuthenticationFailure(const NetworkResult &result);
  void validateAndPersistPendingToken();
  void releaseNetworkIfIdle();
  void sendRequest(const QString &path, const QByteArray &method,
                   const QJsonDocument &body, NetworkHandler handler);
  void sendRequest(const QString &path, NetworkHandler handler);
  QUrl endpoint(const QString &path) const;

  static QString tokenFromPairingText(const QString &text);
  static QJsonDocument parseJson(const QByteArray &payload);
  static QString findString(const QJsonValue &value,
                            const QStringList &candidateKeys);
  static QString findTtml(const QJsonValue &value);
  static qint64 parseTtmlTime(const QString &value);
  static QString normalizedText(const QString &value);
  static QString loadCredential();
  static bool saveCredential(const QString &token);
  static void deleteCredential();

  QUrl m_baseUrl;
  QUrl m_appleMusicBaseUrl;
  std::unique_ptr<QNetworkAccessManager> m_network;
  std::unique_ptr<class CiderAudioMeter> m_audioMeter;
  QTimer m_lyricBoundaryTimer;
  QTimer m_idleProbeTimer;
  QElapsedTimer m_positionClock;
  int m_inFlightRequests = 0;
  bool m_usePersistentCredentials = true;
  bool m_visualTestMode = false;
  bool m_active = false;
  bool m_connected = false;
  bool m_pairingRequired = false;
  bool m_busy = false;
  bool m_mediaPlaying = false;
  bool m_lyricsVisible = false;
  bool m_pendingCredentialSave = false;
  bool m_queueAvailable = false;
  bool m_browserBusy = false;
  bool m_audioPulseVisible = false;
  bool m_panelVisible = false;
  bool m_ciderMediaActive = false;
  bool m_foreignMediaActive = false;
  bool m_idleProbeInFlight = false;
  bool m_favoriteAvailable = false;
  bool m_lyricsSynchronized = false;
  int m_rating = 0;
  ApiVersion m_apiVersion = ApiVersion::Unknown;
  QString m_token;
  QString m_trackIdentity;
  QString m_lastProbeKey;
  QString m_catalogId;
  QString m_storefront;
  QByteArray m_appleDeveloperToken;
  QByteArray m_appleUserToken;
  QString m_statusMessage;
  QString m_browserMessage;
  QString m_lastAddedPlaylistId;
  QVariantList m_queue;
  QVariantList m_playlists;
  QVariantList m_searchResults;
  QVariantList m_recentlyPlayed;
  QVector<LyricLine> m_lyrics;
  QString m_previousLyric;
  QString m_currentLyric;
  QString m_nextLyric;
  QStringList m_upcomingLyrics;
  qint64 m_positionMilliseconds = 0;
  qint64 m_durationMilliseconds = 0;
  qreal m_audioLevel = 0.0;
  qint64 m_appleCredentialsExpireAt = 0;
  quint64 m_generation = 0;
  quint64 m_searchGeneration = 0;
};
