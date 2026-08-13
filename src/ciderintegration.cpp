#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off: wincred.h depends on Windows base types.
#include <windows.h>
#include <wincred.h>
// clang-format on
#endif

#include "ciderintegration.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace {
constexpr auto CiderCredentialTarget = L"Ava/CiderIntegration";
constexpr qsizetype MaximumResponseBytes = 2 * 1024 * 1024;

QJsonValue unwrappedData(const QJsonDocument &document) {
  if (document.isArray()) {
    return document.array();
  }
  if (!document.isObject()) {
    return {};
  }

  const QJsonObject root = document.object();
  if (root.contains(QStringLiteral("data"))) {
    return root.value(QStringLiteral("data"));
  }
  if (root.contains(QStringLiteral("info"))) {
    return root.value(QStringLiteral("info"));
  }
  if (root.contains(QStringLiteral("value"))) {
    return root.value(QStringLiteral("value"));
  }
  return root;
}

QJsonObject mediaItem(const QJsonValue &value) {
  const QJsonObject item = value.toObject();
  const QJsonObject track = item.value(QStringLiteral("track")).toObject();
  return track.isEmpty() ? item : track;
}

QJsonObject itemAttributes(const QJsonValue &value) {
  const QJsonObject item = mediaItem(value);
  const QJsonObject attributes =
      item.value(QStringLiteral("attributes")).toObject();
  return attributes.isEmpty() ? item : attributes;
}

QString firstString(const QJsonObject &object, const QStringList &keys) {
  for (const QString &key : keys) {
    const QString value = object.value(key).toString().trimmed();
    if (!value.isEmpty()) {
      return value;
    }
  }
  return {};
}

QString itemIdentifier(const QJsonValue &value) {
  const QJsonObject item = mediaItem(value);
  const QJsonObject attributes = itemAttributes(value);
  const QJsonObject playParams =
      attributes.value(QStringLiteral("playParams")).toObject();
  const QString identifier = firstString(
      playParams, {QStringLiteral("catalogId"), QStringLiteral("id")});
  if (!identifier.isEmpty()) {
    return identifier;
  }
  return firstString(item, {QStringLiteral("id"), QStringLiteral("catalogId")});
}

bool isAuthenticationFailure(int statusCode) {
  return statusCode == 401 || statusCode == 403;
}
} // namespace

CiderIntegration::CiderIntegration(QObject *parent)
    : CiderIntegration(QUrl(QStringLiteral("http://127.0.0.1:10767")), true,
                       parent) {}

CiderIntegration::CiderIntegration(const QUrl &baseUrl,
                                   bool usePersistentCredentials,
                                   QObject *parent)
    : QObject(parent), m_baseUrl(baseUrl),
      m_usePersistentCredentials(usePersistentCredentials) {
  m_lyricBoundaryTimer.setSingleShot(true);
  m_lyricBoundaryTimer.setTimerType(Qt::PreciseTimer);
  connect(&m_lyricBoundaryTimer, &QTimer::timeout, this, [this]() {
    updateLyricsForPosition();
    scheduleNextLyricBoundary();
  });
  if (m_usePersistentCredentials) {
    m_token = loadCredential();
  }
}

void CiderIntegration::setMediaSession(
    const QString &source, const QString &title, const QString &artist,
    qint64 positionMilliseconds, qint64 durationMilliseconds, bool playing) {
  if (m_visualTestMode) {
    return;
  }
  const bool activeNow =
      source.contains(QStringLiteral("cider"), Qt::CaseInsensitive);
  const QString identity =
      activeNow ? title.trimmed() + QLatin1Char('\n') + artist.trimmed()
                : QString();
  const bool activeChanged = m_active != activeNow;
  const bool trackChanged = m_trackIdentity != identity;

  m_positionMilliseconds = std::max<qint64>(0, positionMilliseconds);
  m_durationMilliseconds = std::max<qint64>(0, durationMilliseconds);
  m_mediaPlaying = activeNow && playing;
  m_positionClock.start();

  if (!activeNow) {
    m_lyricBoundaryTimer.stop();
    if (!m_active && m_trackIdentity.isEmpty()) {
      return;
    }
    m_active = false;
    m_trackIdentity.clear();
    m_connected = false;
    m_pairingRequired = false;
    m_apiVersion = ApiVersion::Unknown;
    m_lastProbeKey.clear();
    ++m_generation;
    setBusy(false);
    clearContent();
    setStatus({});
    emit changed();
    return;
  }

  m_active = true;
  if (trackChanged) {
    m_trackIdentity = identity;
    ++m_generation;
    clearContent();
  }

  updateLyricsForPosition();
  scheduleNextLyricBoundary();
  const QString probeKey = m_trackIdentity + QLatin1Char('\n') + m_token;
  if (activeChanged || trackChanged ||
      (!m_connected && !m_busy && m_lastProbeKey != probeKey)) {
    refreshNowPlaying();
  }
}

void CiderIntegration::setVisualTestState(const QString &state) {
  if (state.isEmpty()) {
    return;
  }

  m_visualTestMode = true;
  m_lyricBoundaryTimer.stop();
  m_mediaPlaying = false;
  ++m_generation;
  m_active = true;
  m_connected = state != QStringLiteral("connect");
  m_pairingRequired = state == QStringLiteral("connect");
  m_busy = false;
  m_apiVersion = ApiVersion::V2;
  m_favoriteAvailable = m_connected;
  m_rating = state == QStringLiteral("lyrics") ? 1 : 0;
  m_queueAvailable = m_connected;
  m_lyricsSynchronized = false;
  m_queue.clear();
  m_lyrics.clear();
  m_currentLyric.clear();
  m_nextLyric.clear();
  m_upcomingLyrics.clear();

  if (state == QStringLiteral("queue")) {
    m_queue = {
        QVariantMap{{QStringLiteral("title"), QStringLiteral("Low Orbit")},
                    {QStringLiteral("artist"), QStringLiteral("Serein")},
                    {QStringLiteral("index"), 1}},
        QVariantMap{{QStringLiteral("title"), QStringLiteral("Violet Lines")},
                    {QStringLiteral("artist"), QStringLiteral("Common Static")},
                    {QStringLiteral("index"), 2}}};
  } else if (state == QStringLiteral("lyrics")) {
    m_lyrics = {
        {0, 8000, QStringLiteral("The city settles into light")},
        {8000, 16000, QStringLiteral("Signals moving through the night")},
        {16000, 24000, QStringLiteral("Every window holds a story")},
        {24000, 32000, QStringLiteral("Every shadow learns to glow")}};
    m_lyricsSynchronized = true;
    m_positionMilliseconds = 4000;
    updateLyricsForPosition();
  }

  m_statusMessage =
      state == QStringLiteral("connect")
          ? QStringLiteral("Copy a Cider API token, then select connect")
          : QString();
  emit changed();
}

void CiderIntegration::connectFromClipboard() {
  const QClipboard *clipboard = QGuiApplication::clipboard();
  connectWithToken(clipboard ? tokenFromPairingText(clipboard->text())
                             : QString());
}

void CiderIntegration::connectWithToken(const QString &token) {
  const QString normalized = tokenFromPairingText(token);
  if (normalized.isEmpty()) {
    m_pairingRequired = true;
    setStatus(QStringLiteral("Copy a Cider API token first"));
    emit changed();
    return;
  }

  m_token = normalized;
  m_lastProbeKey.clear();
  m_pendingCredentialSave = true;
  m_pairingRequired = false;
  m_connected = false;
  m_apiVersion = ApiVersion::Unknown;
  setStatus({});
  refreshNowPlaying();
}

void CiderIntegration::forgetConnection() {
  ++m_generation;
  m_token.clear();
  m_lastProbeKey.clear();
  m_pendingCredentialSave = false;
  m_connected = false;
  m_pairingRequired = false;
  m_apiVersion = ApiVersion::Unknown;
  if (m_usePersistentCredentials) {
    deleteCredential();
  }
  clearContent();
  setStatus(QStringLiteral("Cider connection removed"));
  emit changed();
  if (m_active) {
    refreshNowPlaying();
  }
}

void CiderIntegration::refreshNowPlaying() {
  if (!m_active) {
    return;
  }

  const quint64 generation = ++m_generation;
  m_lastProbeKey = m_trackIdentity + QLatin1Char('\n') + m_token;
  setBusy(true);
  sendRequest(QStringLiteral("/api/v2/playback/now-playing"),
              [this, generation](NetworkResult result) {
                if (generation != m_generation || !m_active) {
                  return;
                }
                if (result.statusCode == 404) {
                  refreshNowPlayingV1(generation);
                  return;
                }
                if (isAuthenticationFailure(result.statusCode)) {
                  handleAuthenticationFailure(result);
                  return;
                }
                if (result.statusCode < 200 || result.statusCode >= 300) {
                  m_connected = false;
                  m_pairingRequired = false;
                  m_apiVersion = ApiVersion::Unknown;
                  setBusy(false);
                  setStatus(QStringLiteral("Cider's local API is unavailable"));
                  emit changed();
                  return;
                }

                m_apiVersion = ApiVersion::V2;
                m_connected = true;
                m_pairingRequired = false;
                setBusy(false);
                setStatus({});
                validateAndPersistPendingToken();

                const QJsonValue nowPlaying =
                    unwrappedData(parseJson(result.body));
                m_catalogId = itemIdentifier(nowPlaying);
                const double playbackSeconds =
                    itemAttributes(nowPlaying)
                        .value(QStringLiteral("currentPlaybackTime"))
                        .toDouble(-1.0);
                if (std::isfinite(playbackSeconds) && playbackSeconds >= 0.0) {
                  m_positionMilliseconds = qRound64(playbackSeconds * 1000.0);
                  m_positionClock.start();
                  updateLyricsForPosition();
                  scheduleNextLyricBoundary();
                }
                emit changed();
                refreshQueue();
                refreshFavorite();
                if (m_lyricsVisible) {
                  refreshLyrics();
                }
              });
}

void CiderIntegration::refreshNowPlayingV1(quint64 generation) {
  sendRequest(QStringLiteral("/api/v1/playback/now-playing"),
              [this, generation](NetworkResult result) {
                if (generation != m_generation || !m_active) {
                  return;
                }
                if (isAuthenticationFailure(result.statusCode)) {
                  handleAuthenticationFailure(result);
                  return;
                }
                if (result.statusCode < 200 || result.statusCode >= 300) {
                  m_connected = false;
                  m_pairingRequired = false;
                  m_apiVersion = ApiVersion::Unknown;
                  setBusy(false);
                  setStatus(QStringLiteral(
                      "This Cider version has no compatible local API"));
                  emit changed();
                  return;
                }

                m_apiVersion = ApiVersion::V1;
                m_connected = true;
                m_pairingRequired = false;
                setBusy(false);
                setStatus({});
                validateAndPersistPendingToken();

                const QJsonValue nowPlaying =
                    unwrappedData(parseJson(result.body));
                m_catalogId = itemIdentifier(nowPlaying);
                const double playbackSeconds =
                    itemAttributes(nowPlaying)
                        .value(QStringLiteral("currentPlaybackTime"))
                        .toDouble(-1.0);
                if (std::isfinite(playbackSeconds) && playbackSeconds >= 0.0) {
                  m_positionMilliseconds = qRound64(playbackSeconds * 1000.0);
                  m_positionClock.start();
                  updateLyricsForPosition();
                  scheduleNextLyricBoundary();
                }
                emit changed();
                refreshQueue();
                refreshFavorite();
                if (m_lyricsVisible) {
                  refreshLyrics();
                }
              });
}

void CiderIntegration::refreshQueue() {
  if (!m_active || !m_connected || m_apiVersion == ApiVersion::Unknown) {
    return;
  }

  const quint64 generation = m_generation;
  if (m_apiVersion == ApiVersion::V1) {
    sendRequest(QStringLiteral("/api/v1/playback/queue"),
                [this, generation](NetworkResult result) {
                  applyQueueResult(std::move(result), generation, 0, false);
                });
    return;
  }

  sendRequest(QStringLiteral("/api/v2/queue?offset=0&limit=1"),
              [this, generation](NetworkResult result) {
                if (generation != m_generation || !m_active) {
                  return;
                }
                if (isAuthenticationFailure(result.statusCode) ||
                    result.statusCode < 200 || result.statusCode >= 300) {
                  applyQueueResult(std::move(result), generation, 0, false);
                  return;
                }

                const QJsonObject payload =
                    unwrappedData(parseJson(result.body)).toObject();
                const int position =
                    payload.value(QStringLiteral("position")).toInt(-1);
                requestQueueWindow(generation, std::max(0, position),
                                   position >= 0);
              });
}

void CiderIntegration::requestQueueWindow(quint64 generation, int offset,
                                          bool skipCurrent) {
  const QString path = QStringLiteral("/api/v2/queue?offset=%1&limit=%2")
                           .arg(offset)
                           .arg(skipCurrent ? 3 : 2);
  sendRequest(
      path, [this, generation, offset, skipCurrent](NetworkResult result) {
        applyQueueResult(std::move(result), generation, offset, skipCurrent);
      });
}

void CiderIntegration::applyQueueResult(NetworkResult result,
                                        quint64 generation, int offset,
                                        bool skipCurrent) {
  if (generation != m_generation || !m_active) {
    return;
  }
  if (isAuthenticationFailure(result.statusCode)) {
    m_queueAvailable = false;
    setStatus(m_apiVersion == ApiVersion::V2
                  ? QStringLiteral("The Cider token needs queue access")
                  : QStringLiteral("Cider queue access was rejected"));
    emit changed();
    return;
  }
  if (result.statusCode < 200 || result.statusCode >= 300) {
    m_queueAvailable = false;
    emit changed();
    return;
  }

  const QJsonValue payload = unwrappedData(parseJson(result.body));
  QJsonArray items;
  int position = -1;
  if (payload.isArray()) {
    items = payload.toArray();
  } else {
    const QJsonObject object = payload.toObject();
    items = object.value(QStringLiteral("items")).toArray();
    if (items.isEmpty()) {
      items = object.value(QStringLiteral("queue")).toArray();
    }
    position = object.value(QStringLiteral("position")).toInt(-1);
  }

  int start = skipCurrent && !items.isEmpty() ? 1 : 0;
  if (!skipCurrent) {
    if (position < 0 && !m_catalogId.isEmpty()) {
      for (qsizetype index = 0; index < items.size(); ++index) {
        if (itemIdentifier(items.at(index)) == m_catalogId) {
          position = static_cast<int>(index);
          break;
        }
      }
    }
    if (position >= 0 && position < items.size()) {
      start = position + 1;
    }
  }

  QVariantList queue;
  for (int index = start; index < items.size() && queue.size() < 2; ++index) {
    const QJsonObject attributes = itemAttributes(items.at(index));
    const QString title = normalizedText(firstString(
        attributes, {QStringLiteral("name"), QStringLiteral("title")}));
    if (title.isEmpty()) {
      continue;
    }
    const QString artist = normalizedText(firstString(
        attributes, {QStringLiteral("artistName"), QStringLiteral("artist")}));
    queue.append(QVariantMap{{QStringLiteral("title"), title},
                             {QStringLiteral("artist"), artist},
                             {QStringLiteral("index"), offset + index}});
  }
  m_queue = queue;
  m_queueAvailable = true;
  emit changed();
}

void CiderIntegration::refreshFavorite() {
  if (!m_active || !m_connected || m_apiVersion == ApiVersion::Unknown) {
    return;
  }

  const quint64 generation = m_generation;
  const QString path =
      m_apiVersion == ApiVersion::V2
          ? QStringLiteral("/api/v2/library/now-playing/status")
          : QStringLiteral("/api/v1/playback/library-status");
  sendRequest(path, [this, generation](NetworkResult result) {
    if (generation != m_generation || !m_active) {
      return;
    }
    if (isAuthenticationFailure(result.statusCode)) {
      m_favoriteAvailable = false;
      setStatus(m_apiVersion == ApiVersion::V2
                    ? QStringLiteral("The Cider token needs library access")
                    : QStringLiteral("Cider favorite access was rejected"));
      emit changed();
      return;
    }
    if (result.statusCode < 200 || result.statusCode >= 300) {
      m_favoriteAvailable = false;
      emit changed();
      return;
    }

    const QJsonObject status = unwrappedData(parseJson(result.body)).toObject();
    m_rating = status.value(QStringLiteral("rating")).toInt(0);
    m_favoriteAvailable = true;
    emit changed();
  });
}

void CiderIntegration::toggleFavorite() {
  if (!m_active || !m_connected || !m_favoriteAvailable) {
    return;
  }

  const int previousRating = m_rating;
  const bool removing = m_rating == 1;
  m_rating = removing ? 0 : 1;
  emit changed();

  QString path;
  QByteArray method = "POST";
  QJsonDocument body;
  if (m_apiVersion == ApiVersion::V2) {
    path = removing ? QStringLiteral("/api/v2/library/now-playing/rating")
                    : QStringLiteral("/api/v2/library/now-playing/love");
    method = removing ? QByteArrayLiteral("DELETE") : QByteArrayLiteral("POST");
  } else {
    path = QStringLiteral("/api/v1/playback/set-rating");
    body = QJsonDocument(
        QJsonObject{{QStringLiteral("rating"), removing ? 0 : 1}});
  }

  const quint64 generation = m_generation;
  sendRequest(path, method, body,
              [this, generation, previousRating](NetworkResult result) {
                if (generation != m_generation) {
                  return;
                }
                if (result.statusCode < 200 || result.statusCode >= 300) {
                  m_rating = previousRating;
                  setStatus(
                      QStringLiteral("Cider could not update the favorite"));
                  emit changed();
                }
              });
}

void CiderIntegration::playQueueIndex(int index) {
  if (!m_active || !m_connected || !m_queueAvailable || index < 0) {
    return;
  }

  const QString path =
      m_apiVersion == ApiVersion::V2
          ? QStringLiteral("/api/v2/queue/jump")
          : QStringLiteral("/api/v1/playback/queue/change-to-index");
  const quint64 generation = m_generation;
  sendRequest(path, QByteArrayLiteral("POST"),
              QJsonDocument(QJsonObject{{QStringLiteral("index"), index}}),
              [this, generation](NetworkResult result) {
                if (generation != m_generation) {
                  return;
                }
                if (result.statusCode < 200 || result.statusCode >= 300) {
                  setStatus(
                      QStringLiteral("Cider could not switch queue items"));
                  emit changed();
                }
              });
}

void CiderIntegration::refreshLyrics() {
  if (!m_active || !m_connected || m_catalogId.isEmpty()) {
    return;
  }

  const quint64 generation = m_generation;
  if (m_storefront.isEmpty()) {
    requestStorefront(generation);
  } else {
    requestLyrics(generation);
  }
}

void CiderIntegration::setLyricsVisible(bool visible) {
  if (m_lyricsVisible == visible) {
    if (visible) {
      updateLyricsForPosition();
      scheduleNextLyricBoundary();
    }
    return;
  }

  m_lyricsVisible = visible;
  if (!visible) {
    m_lyricBoundaryTimer.stop();
    return;
  }

  updateLyricsForPosition();
  scheduleNextLyricBoundary();
  if (!m_visualTestMode) {
    refreshLyrics();
  }
}

void CiderIntegration::requestStorefront(quint64 generation) {
  const QJsonDocument body(QJsonObject{
      {QStringLiteral("path"), QStringLiteral("/v1/me/storefront?limit=1")}});
  sendRequest(QStringLiteral("/api/v1/amapi/run-v3"), QByteArrayLiteral("POST"),
              body, [this, generation](NetworkResult result) {
                if (generation != m_generation || result.statusCode < 200 ||
                    result.statusCode >= 300) {
                  return;
                }
                m_storefront = findString(
                    unwrappedData(parseJson(result.body)),
                    {QStringLiteral("storefront"), QStringLiteral("id")});
                if (!m_storefront.isEmpty()) {
                  requestLyrics(generation);
                }
              });
}

void CiderIntegration::requestLyrics(quint64 generation) {
  static const QRegularExpression safeIdentifier(
      QStringLiteral("^[A-Za-z0-9._-]+$"));
  if (!safeIdentifier.match(m_storefront).hasMatch() ||
      !safeIdentifier.match(m_catalogId).hasMatch()) {
    return;
  }

  const QString path =
      QStringLiteral("/v1/catalog/%1/songs/%2/lyrics?l=en-US&platform=web")
          .arg(m_storefront, m_catalogId);
  const QJsonDocument body(QJsonObject{{QStringLiteral("path"), path}});
  sendRequest(QStringLiteral("/api/v1/amapi/run-v3"), QByteArrayLiteral("POST"),
              body, [this, generation](NetworkResult result) {
                if (generation != m_generation) {
                  return;
                }
                if (result.statusCode < 200 || result.statusCode >= 300) {
                  if (isAuthenticationFailure(result.statusCode)) {
                    setStatus(QStringLiteral("Cider rejected lyrics access"));
                    emit changed();
                  }
                  return;
                }

                const QString ttml =
                    findTtml(unwrappedData(parseJson(result.body)));
                const QVector<LyricLine> lines = parseTtml(ttml.toUtf8());
                const bool synchronized = std::any_of(
                    lines.cbegin(), lines.cend(), [](const LyricLine &line) {
                      return line.startMilliseconds >= 0;
                    });
                if (m_lyrics == lines && m_lyricsSynchronized == synchronized) {
                  return;
                }
                m_lyrics = lines;
                m_lyricsSynchronized = synchronized;
                updateLyricsForPosition();
                scheduleNextLyricBoundary();
                emit changed();
              });
}

void CiderIntegration::updateLyricsForPosition() {
  const qint64 playbackPosition = effectivePositionMilliseconds();
  QString current;
  QString next;
  QStringList upcoming;
  if (!m_lyrics.isEmpty()) {
    if (!m_lyricsSynchronized) {
      current = m_lyrics.constFirst().text;
      if (m_lyrics.size() > 1) {
        next = m_lyrics.at(1).text;
      }
    } else {
      qsizetype activeIndex = -1;
      for (qsizetype index = 0; index < m_lyrics.size(); ++index) {
        const LyricLine &line = m_lyrics.at(index);
        if (line.startMilliseconds < 0) {
          continue;
        }
        if (playbackPosition >= line.startMilliseconds &&
            (line.endMilliseconds < 0 ||
             playbackPosition < line.endMilliseconds)) {
          activeIndex = index;
          break;
        }
        if (playbackPosition >= line.startMilliseconds) {
          activeIndex = index;
        }
      }
      if (activeIndex >= 0) {
        current = m_lyrics.at(activeIndex).text;
        for (qsizetype index = activeIndex + 1;
             index < m_lyrics.size() && upcoming.size() < 3; ++index) {
          upcoming.append(m_lyrics.at(index).text);
        }
      } else {
        for (qsizetype index = 0;
             index < m_lyrics.size() && upcoming.size() < 3; ++index) {
          upcoming.append(m_lyrics.at(index).text);
        }
      }
      next = upcoming.value(0);
    }
  }

  if (m_currentLyric == current && m_nextLyric == next &&
      m_upcomingLyrics == upcoming) {
    return;
  }
  m_currentLyric = current;
  m_nextLyric = next;
  m_upcomingLyrics = upcoming;
  emit changed();
}

void CiderIntegration::scheduleNextLyricBoundary() {
  m_lyricBoundaryTimer.stop();
  if (!m_active || !m_mediaPlaying || !m_lyricsVisible ||
      !m_lyricsSynchronized || m_lyrics.isEmpty()) {
    return;
  }

  const qint64 playbackPosition = effectivePositionMilliseconds();
  qint64 nextBoundary = -1;
  for (const LyricLine &line : std::as_const(m_lyrics)) {
    if (line.startMilliseconds > playbackPosition &&
        (nextBoundary < 0 || line.startMilliseconds < nextBoundary)) {
      nextBoundary = line.startMilliseconds;
    }
  }
  if (nextBoundary < 0) {
    return;
  }

  const qint64 delay = std::max<qint64>(1, nextBoundary - playbackPosition);
  m_lyricBoundaryTimer.start(static_cast<int>(
      std::min<qint64>(delay, std::numeric_limits<int>::max())));
}

qint64 CiderIntegration::effectivePositionMilliseconds() const {
  qint64 position = m_positionMilliseconds;
  if (m_mediaPlaying && m_positionClock.isValid()) {
    position += m_positionClock.elapsed();
  }
  if (m_durationMilliseconds > 0) {
    position = std::min(position, m_durationMilliseconds);
  }
  return std::max<qint64>(0, position);
}

void CiderIntegration::clearContent() {
  m_lyricBoundaryTimer.stop();
  m_queueAvailable = false;
  m_favoriteAvailable = false;
  m_rating = 0;
  m_catalogId.clear();
  m_queue.clear();
  m_lyrics.clear();
  m_lyricsSynchronized = false;
  m_currentLyric.clear();
  m_nextLyric.clear();
  m_upcomingLyrics.clear();
}

void CiderIntegration::setStatus(const QString &message) {
  if (m_statusMessage == message) {
    return;
  }
  m_statusMessage = message;
  emit changed();
}

void CiderIntegration::setBusy(bool busy) {
  if (m_busy == busy) {
    return;
  }
  m_busy = busy;
  emit changed();
}

void CiderIntegration::handleAuthenticationFailure(const NetworkResult &) {
  m_connected = false;
  m_pairingRequired = true;
  m_apiVersion = ApiVersion::Unknown;
  m_pendingCredentialSave = false;
  setBusy(false);
  clearContent();
  setStatus(QStringLiteral("Copy a scoped Cider API token to connect"));
  emit changed();
}

void CiderIntegration::validateAndPersistPendingToken() {
  if (!m_pendingCredentialSave) {
    return;
  }
  m_pendingCredentialSave = false;
  if (m_usePersistentCredentials && !saveCredential(m_token)) {
    setStatus(
        QStringLiteral("Connected, but Windows could not save the token"));
  }
}

void CiderIntegration::sendRequest(const QString &path,
                                   NetworkHandler handler) {
  sendRequest(path, QByteArrayLiteral("GET"), {}, std::move(handler));
}

void CiderIntegration::sendRequest(const QString &path,
                                   const QByteArray &method,
                                   const QJsonDocument &body,
                                   NetworkHandler handler) {
  if (!m_network) {
    m_network = std::make_unique<QNetworkAccessManager>(this);
  }

  QNetworkRequest request(endpoint(path));
  request.setRawHeader("Accept", "application/json");
  request.setTransferTimeout(2500);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  if (!m_token.isEmpty()) {
    request.setRawHeader("apptoken", m_token.toUtf8());
  }

  QByteArray payload;
  if (!body.isNull()) {
    payload = body.toJson(QJsonDocument::Compact);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
  }

  QNetworkReply *reply = nullptr;
  if (method == QByteArrayLiteral("GET")) {
    reply = m_network->get(request);
  } else if (method == QByteArrayLiteral("POST")) {
    reply = m_network->post(request, payload);
  } else {
    reply = m_network->sendCustomRequest(request, method, payload);
  }
  ++m_inFlightRequests;

  struct ResponseBuffer {
    QByteArray bytes;
    bool tooLarge = false;
  };
  const auto buffer = std::make_shared<ResponseBuffer>();
  const auto collect = [reply, buffer]() {
    const QByteArray chunk = reply->readAll();
    if (buffer->tooLarge) {
      return;
    }
    if (buffer->bytes.size() + chunk.size() > MaximumResponseBytes) {
      buffer->tooLarge = true;
      buffer->bytes.clear();
      reply->abort();
      return;
    }
    buffer->bytes.append(chunk);
  };
  connect(reply, &QNetworkReply::readyRead, this, collect);
  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, buffer, collect, handler = std::move(handler)]() mutable {
        collect();
        NetworkResult result;
        result.statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.body = std::move(buffer->bytes);
        result.tooLarge = buffer->tooLarge;
        if (buffer->tooLarge) {
          result.error =
              QStringLiteral("Cider response exceeded the safety limit");
        } else if (reply->error() != QNetworkReply::NoError) {
          result.error = reply->errorString();
        }
        reply->deleteLater();
        handler(std::move(result));
        m_inFlightRequests = std::max(0, m_inFlightRequests - 1);
        releaseNetworkIfIdle();
      });
}

void CiderIntegration::releaseNetworkIfIdle() {
  if (!m_network || m_inFlightRequests != 0) {
    return;
  }
  QTimer::singleShot(0, this, [this]() {
    if (!m_network || m_inFlightRequests != 0) {
      return;
    }
    m_network->clearConnectionCache();
    if (!m_connected) {
      m_network.reset();
    }
  });
}

QUrl CiderIntegration::endpoint(const QString &path) const {
  QUrl url = m_baseUrl;
  const int queryIndex = path.indexOf(QLatin1Char('?'));
  url.setPath(queryIndex >= 0 ? path.left(queryIndex) : path);
  if (queryIndex >= 0) {
    url.setQuery(path.mid(queryIndex + 1));
  }
  return url;
}

QString CiderIntegration::tokenFromPairingText(const QString &text) {
  QString value = text.trimmed();
  if (value.size() > 4096) {
    return {};
  }

  QJsonParseError error;
  const QJsonDocument document =
      QJsonDocument::fromJson(value.toUtf8(), &error);
  if (error.error == QJsonParseError::NoError && document.isObject()) {
    const QJsonObject object = document.object();
    value = firstString(object,
                        {QStringLiteral("token"), QStringLiteral("apptoken"),
                         QStringLiteral("appToken")});
  }

  value = value.trimmed();
  if (value.size() < 6 || value.size() > 2048 ||
      value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('\n'))) {
    return {};
  }
  return value;
}

QJsonDocument CiderIntegration::parseJson(const QByteArray &payload) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
  return error.error == QJsonParseError::NoError ? document : QJsonDocument();
}

QString CiderIntegration::findString(const QJsonValue &value,
                                     const QStringList &candidateKeys) {
  if (value.isObject()) {
    const QJsonObject object = value.toObject();
    for (const QString &key : candidateKeys) {
      const QString found = object.value(key).toString().trimmed();
      if (!found.isEmpty()) {
        return found;
      }
    }
    for (auto iterator = object.constBegin(); iterator != object.constEnd();
         ++iterator) {
      const QString found = findString(iterator.value(), candidateKeys);
      if (!found.isEmpty()) {
        return found;
      }
    }
  } else if (value.isArray()) {
    for (const QJsonValue &child : value.toArray()) {
      const QString found = findString(child, candidateKeys);
      if (!found.isEmpty()) {
        return found;
      }
    }
  }
  return {};
}

QString CiderIntegration::findTtml(const QJsonValue &value) {
  return findString(value, {QStringLiteral("ttml")});
}

QVector<CiderIntegration::LyricLine>
CiderIntegration::parseTtml(const QByteArray &ttml) {
  QVector<LyricLine> lines;
  QXmlStreamReader reader(ttml);
  bool insideLine = false;
  LyricLine line;
  QString text;

  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement() && reader.name() == QStringLiteral("p")) {
      insideLine = true;
      text.clear();
      line = {};
      line.startMilliseconds = parseTtmlTime(
          reader.attributes().value(QStringLiteral("begin")).toString());
      line.endMilliseconds = parseTtmlTime(
          reader.attributes().value(QStringLiteral("end")).toString());
    } else if (insideLine && reader.isCharacters()) {
      text.append(reader.text());
    } else if (insideLine && reader.isEndElement() &&
               reader.name() == QStringLiteral("p")) {
      insideLine = false;
      line.text = normalizedText(text);
      if (!line.text.isEmpty()) {
        lines.append(line);
      }
    }
  }

  for (qsizetype index = 0; index < lines.size(); ++index) {
    if (lines[index].startMilliseconds < 0 ||
        lines[index].endMilliseconds >= 0) {
      continue;
    }
    lines[index].endMilliseconds =
        index + 1 < lines.size() && lines[index + 1].startMilliseconds >= 0
            ? lines[index + 1].startMilliseconds
            : lines[index].startMilliseconds + 5000;
  }
  return lines;
}

qint64 CiderIntegration::parseTtmlTime(const QString &value) {
  const QString trimmed = value.trimmed();
  if (trimmed.isEmpty()) {
    return -1;
  }

  bool valid = false;
  if (trimmed.endsWith(QLatin1Char('s'))) {
    const double seconds = trimmed.left(trimmed.size() - 1).toDouble(&valid);
    return valid ? qRound64(seconds * 1000.0) : -1;
  }

  const QStringList parts = trimmed.split(QLatin1Char(':'));
  if (parts.size() < 2 || parts.size() > 3) {
    return -1;
  }
  double seconds = parts.constLast().toDouble(&valid);
  if (!valid) {
    return -1;
  }
  const int minutes = parts.at(parts.size() - 2).toInt(&valid);
  if (!valid) {
    return -1;
  }
  int hours = 0;
  if (parts.size() == 3) {
    hours = parts.constFirst().toInt(&valid);
    if (!valid) {
      return -1;
    }
  }
  seconds += minutes * 60.0 + hours * 3600.0;
  return qRound64(seconds * 1000.0);
}

QString CiderIntegration::normalizedText(const QString &value) {
  QString text = value;
  text.replace(QChar(0x00a0), QLatin1Char(' '));
  return text.simplified().left(240);
}

QString CiderIntegration::loadCredential() {
#ifdef Q_OS_WIN
  PCREDENTIALW credential = nullptr;
  if (!CredReadW(CiderCredentialTarget, CRED_TYPE_GENERIC, 0, &credential) ||
      !credential) {
    return {};
  }
  const QByteArray bytes(
      reinterpret_cast<const char *>(credential->CredentialBlob),
      static_cast<qsizetype>(credential->CredentialBlobSize));
  CredFree(credential);
  return QString::fromUtf8(bytes).trimmed();
#else
  return {};
#endif
}

bool CiderIntegration::saveCredential(const QString &token) {
#ifdef Q_OS_WIN
  const QByteArray bytes = token.toUtf8();
  if (bytes.isEmpty() || bytes.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
    return false;
  }
  CREDENTIALW credential{};
  credential.Type = CRED_TYPE_GENERIC;
  credential.TargetName = const_cast<wchar_t *>(CiderCredentialTarget);
  credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
  credential.CredentialBlob =
      reinterpret_cast<LPBYTE>(const_cast<char *>(bytes.constData()));
  credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
  credential.UserName = const_cast<wchar_t *>(L"Ava");
  return CredWriteW(&credential, 0) == TRUE;
#else
  Q_UNUSED(token)
  return false;
#endif
}

void CiderIntegration::deleteCredential() {
#ifdef Q_OS_WIN
  CredDeleteW(CiderCredentialTarget, CRED_TYPE_GENERIC, 0);
#endif
}
