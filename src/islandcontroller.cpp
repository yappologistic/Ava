#include "islandcontroller.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QtGlobal>

#include <atomic>
#include <mutex>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace {

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;
namespace MediaControl = winrt::Windows::Media::Control;
namespace Connectivity = winrt::Windows::Networking::Connectivity;
namespace StorageStreams = winrt::Windows::Storage::Streams;

struct __declspec(uuid("905a0fef-bc53-11df-8c49-001e4fc686da")) IBufferByteAccess
    : IUnknown
{
    virtual HRESULT __stdcall Buffer(unsigned char **value) = 0;
};

QString toQString(const winrt::hstring &value)
{
    return QString::fromWCharArray(value.c_str(), static_cast<qsizetype>(value.size()));
}

bool readDefaultAudioEndpoint(int *volume, bool *muted)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                nullptr,
                                CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator)))) {
        return false;
    }

    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
        return false;
    }

    ComPtr<IAudioEndpointVolume> endpoint;
    if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume),
                                CLSCTX_ALL,
                                nullptr,
                                reinterpret_cast<void **>(endpoint.GetAddressOf())))) {
        return false;
    }

    float scalar = 0.0f;
    BOOL endpointMuted = FALSE;
    if (FAILED(endpoint->GetMasterVolumeLevelScalar(&scalar))
        || FAILED(endpoint->GetMute(&endpointMuted))) {
        return false;
    }

    *volume = qBound(0, qRound(scalar * 100.0f), 100);
    *muted = endpointMuted == TRUE;
    return true;
}

bool writeDefaultAudioVolume(int volume)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> endpoint;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator)))
        || FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))
        || FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void **>(endpoint.GetAddressOf())))) {
        return false;
    }
    return SUCCEEDED(endpoint->SetMasterVolumeLevelScalar(qBound(0, volume, 100) / 100.0f,
                                                           nullptr));
}

bool toggleDefaultAudioMute()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> endpoint;
    BOOL muted = FALSE;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator)))
        || FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))
        || FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void **>(endpoint.GetAddressOf())))
        || FAILED(endpoint->GetMute(&muted))) {
        return false;
    }
    return SUCCEEDED(endpoint->SetMute(!muted, nullptr));
}
#endif

} // namespace

struct IslandController::PlatformState
{
#ifdef Q_OS_WIN
    MediaControl::GlobalSystemMediaTransportControlsSessionManager mediaManager{nullptr};
    MediaControl::GlobalSystemMediaTransportControlsSession mediaSession{nullptr};
    std::mutex mediaMutex;
    std::atomic_bool mediaRefreshInFlight{false};
#endif
};

IslandController::IslandController(QObject *parent)
    : QObject(parent), m_platform(std::make_shared<PlatformState>())
{
#ifdef Q_OS_WIN
    BOOL animationsEnabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0)) {
        m_reducedMotion = animationsEnabled == FALSE;
    }
#endif

    connect(&m_timer, &QTimer::timeout, this, &IslandController::tick);
    m_countdownTimer.setTimerType(Qt::PreciseTimer);
    m_countdownTimer.setInterval(33);
    connect(&m_countdownTimer, &QTimer::timeout, this, &IslandController::updateTimer);
    m_alarmTimer.setTimerType(Qt::CoarseTimer);
    m_alarmTimer.setInterval(1100);
    connect(&m_alarmTimer, &QTimer::timeout, this, &IslandController::soundTimerAlert);
    updateClock();
    refreshSystemState();
    QTimer::singleShot(0, this, &IslandController::refreshMedia);
    m_timer.start(1000);
}

IslandController::~IslandController() = default;

QString IslandController::lastDroppedFile() const
{
    return m_droppedFiles.isEmpty() ? QString() : QFileInfo(m_droppedFiles.constLast()).fileName();
}

void IslandController::setExpanded(bool expanded)
{
    if (m_expanded == expanded) {
        return;
    }
    m_expanded = expanded;
    emit expandedChanged();
}

void IslandController::toggleExpanded()
{
    setExpanded(!m_expanded);
}

void IslandController::setPinned(bool pinned)
{
    if (m_pinned == pinned) {
        return;
    }
    m_pinned = pinned;
    if (m_pinned) {
        setExpanded(true);
    }
    emit pinnedChanged();
}

void IslandController::togglePinned()
{
    setPinned(!m_pinned);
}

void IslandController::openTimer()
{
    if (m_timerPanelOpen) {
        return;
    }
    m_timerPanelOpen = true;
    setExpanded(true);
    emit timerChanged();
}

void IslandController::closeTimer()
{
    if (!m_timerPanelOpen) {
        return;
    }
    m_timerPanelOpen = false;
    emit timerChanged();
}

void IslandController::startTimer(int durationSeconds)
{
    durationSeconds = qBound(1, durationSeconds, 24 * 60 * 60);
    m_alarmTimer.stop();
    m_timerDurationSeconds = durationSeconds;
    m_timerPausedRemainingMs = static_cast<qint64>(durationSeconds) * 1000;
    m_timerDeadlineMs = QDateTime::currentMSecsSinceEpoch() + m_timerPausedRemainingMs;
    m_timerRemainingSeconds = durationSeconds;
    m_timerRemainingText = formatTimerDuration(m_timerPausedRemainingMs);
    m_timerProgress = 1.0;
    m_timerActive = true;
    m_timerPaused = false;
    m_timerRinging = false;
    m_timerPanelOpen = true;
    m_countdownTimer.start();
    emit timerChanged();
}

void IslandController::toggleTimerPaused()
{
    if (!m_timerActive) {
        return;
    }

    if (m_timerPaused) {
        m_timerDeadlineMs = QDateTime::currentMSecsSinceEpoch() + m_timerPausedRemainingMs;
        m_timerPaused = false;
        m_countdownTimer.start();
    } else {
        m_timerPausedRemainingMs = qMax<qint64>(0, m_timerDeadlineMs
                                                    - QDateTime::currentMSecsSinceEpoch());
        m_timerPaused = true;
        m_countdownTimer.stop();
    }
    emit timerChanged();
}

void IslandController::addTimerMinute()
{
    if (!m_timerActive) {
        return;
    }
    constexpr qint64 minuteMs = 60 * 1000;
    m_timerDurationSeconds = qMin(24 * 60 * 60, m_timerDurationSeconds + 60);
    if (m_timerPaused) {
        m_timerPausedRemainingMs = qMin<qint64>(24 * 60 * 60 * 1000,
                                                m_timerPausedRemainingMs + minuteMs);
    } else {
        m_timerDeadlineMs += minuteMs;
    }
    updateTimer();
}

void IslandController::cancelTimer()
{
    if (!m_timerActive && !m_timerRinging) {
        return;
    }
    m_countdownTimer.stop();
    m_alarmTimer.stop();
    m_timerActive = false;
    m_timerPaused = false;
    m_timerRinging = false;
    m_timerDurationSeconds = 0;
    m_timerRemainingSeconds = 0;
    m_timerPausedRemainingMs = 0;
    m_timerRemainingText = QStringLiteral("0:00");
    m_timerProgress = 0.0;
    emit timerChanged();
}

void IslandController::dismissTimer()
{
    cancelTimer();
    closeTimer();
}

void IslandController::togglePlayback()
{
#ifdef Q_OS_WIN
    try {
        MediaControl::GlobalSystemMediaTransportControlsSession session{nullptr};
        {
            std::scoped_lock lock(m_platform->mediaMutex);
            session = m_platform->mediaSession;
        }
        if (session) {
            session.TryTogglePlayPauseAsync();
        }
    } catch (...) {
    }
#endif
}

void IslandController::previousTrack()
{
#ifdef Q_OS_WIN
    try {
        MediaControl::GlobalSystemMediaTransportControlsSession session{nullptr};
        {
            std::scoped_lock lock(m_platform->mediaMutex);
            session = m_platform->mediaSession;
        }
        if (session) {
            session.TrySkipPreviousAsync();
        }
    } catch (...) {
    }
#endif
}

void IslandController::nextTrack()
{
#ifdef Q_OS_WIN
    try {
        MediaControl::GlobalSystemMediaTransportControlsSession session{nullptr};
        {
            std::scoped_lock lock(m_platform->mediaMutex);
            session = m_platform->mediaSession;
        }
        if (session) {
            session.TrySkipNextAsync();
        }
    } catch (...) {
    }
#endif
}

void IslandController::setVolume(int volume)
{
#ifdef Q_OS_WIN
    writeDefaultAudioVolume(volume);
#else
    Q_UNUSED(volume)
#endif
    refreshSystemState();
}

void IslandController::toggleMute()
{
#ifdef Q_OS_WIN
    toggleDefaultAudioMute();
#endif
    refreshSystemState();
}

void IslandController::handleDrop(const QVariantList &urls)
{
    bool changed = false;
    for (const QVariant &entry : urls) {
        const QUrl url = entry.toUrl();
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = QDir::cleanPath(url.toLocalFile());
        if (QFileInfo::exists(path) && !m_droppedFiles.contains(path)) {
            m_droppedFiles.append(path);
            changed = true;
        }
    }
    if (changed) {
        emit droppedFilesChanged();
        setExpanded(true);
    }
}

void IslandController::clearDroppedFiles()
{
    if (m_droppedFiles.isEmpty()) {
        return;
    }
    m_droppedFiles.clear();
    emit droppedFilesChanged();
}

void IslandController::revealLastDroppedFile()
{
    if (m_droppedFiles.isEmpty()) {
        return;
    }
    const QFileInfo fileInfo(m_droppedFiles.constLast());
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
}

void IslandController::tick()
{
    updateClock();
    refreshMedia();
    if (++m_slowRefreshCounter >= 3) {
        m_slowRefreshCounter = 0;
        refreshSystemState();
    }
}

void IslandController::updateClock()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString nextTime = now.toString(QStringLiteral("h:mm"));
    const QString nextMeridiem = now.toString(QStringLiteral("AP"));
    const QString nextCompactDate = now.toString(QStringLiteral("ddd, MMM d")).toUpper();
    const QString nextDate = now.toString(QStringLiteral("dddd, MMMM d"));
    if (m_timeText == nextTime && m_meridiemText == nextMeridiem
        && m_compactDateText == nextCompactDate && m_dateText == nextDate) {
        return;
    }
    m_timeText = nextTime;
    m_meridiemText = nextMeridiem;
    m_compactDateText = nextCompactDate;
    m_dateText = nextDate;
    emit clockChanged();
}

QString IslandController::formatTimerDuration(qint64 totalMilliseconds) const
{
    const qint64 totalSeconds = qMax<qint64>(0, (totalMilliseconds + 999) / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

void IslandController::updateTimer()
{
    if (!m_timerActive) {
        return;
    }

    const qint64 remainingMs = m_timerPaused
        ? m_timerPausedRemainingMs
        : qMax<qint64>(0, m_timerDeadlineMs - QDateTime::currentMSecsSinceEpoch());
    if (remainingMs <= 0) {
        finishTimer();
        return;
    }

    const int nextRemainingSeconds = static_cast<int>((remainingMs + 999) / 1000);
    const QString nextText = formatTimerDuration(remainingMs);
    const double nextProgress = m_timerDurationSeconds > 0
        ? qBound(0.0, static_cast<double>(remainingMs)
                          / (static_cast<double>(m_timerDurationSeconds) * 1000.0), 1.0)
        : 0.0;

    const bool secondChanged = nextRemainingSeconds != m_timerRemainingSeconds;
    const bool progressChanged = qAbs(nextProgress - m_timerProgress) >= 0.0005;
    if (!secondChanged && !progressChanged) {
        return;
    }
    m_timerRemainingSeconds = nextRemainingSeconds;
    m_timerRemainingText = nextText;
    m_timerProgress = nextProgress;
    emit timerChanged();
}

void IslandController::finishTimer()
{
    m_countdownTimer.stop();
    m_timerActive = false;
    m_timerPaused = false;
    m_timerRinging = true;
    m_timerRemainingSeconds = 0;
    m_timerPausedRemainingMs = 0;
    m_timerRemainingText = QStringLiteral("0:00");
    m_timerProgress = 0.0;
    m_timerPanelOpen = true;
    setExpanded(true);
    soundTimerAlert();
    m_alarmTimer.start();
    emit timerChanged();
}

void IslandController::soundTimerAlert()
{
#ifdef Q_OS_WIN
    MessageBeep(MB_ICONEXCLAMATION);
#endif
}

void IslandController::refreshMedia()
{
#ifdef Q_OS_WIN
    if (m_platform->mediaRefreshInFlight.exchange(true)) {
        return;
    }

    struct MediaSnapshot {
        bool available = false;
        QString title;
        QString artist;
        QString source;
        QString artworkUrl;
        QString identity;
        bool playing = false;
        bool canPrevious = false;
        bool canNext = false;
        double progress = 0.0;
        QString positionText = QStringLiteral("0:00");
        QString durationText = QStringLiteral("0:00");
    };

    const auto platform = m_platform;
    const QString previousIdentity = m_mediaIdentity;
    const QString previousArtworkUrl = m_mediaArtworkUrl;
    const QPointer<IslandController> self(this);

    QThreadPool::globalInstance()->start([platform, previousIdentity, previousArtworkUrl, self]() {
        MediaSnapshot snapshot;
        bool apartmentInitialized = false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartmentInitialized = true;

            MediaControl::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
            {
                std::scoped_lock lock(platform->mediaMutex);
                manager = platform->mediaManager;
            }
            if (!manager) {
                manager = MediaControl::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                std::scoped_lock lock(platform->mediaMutex);
                platform->mediaManager = manager;
            }

            const auto session = manager.GetCurrentSession();
            {
                std::scoped_lock lock(platform->mediaMutex);
                platform->mediaSession = session;
            }
            if (session) {
                const auto properties = session.TryGetMediaPropertiesAsync().get();
                const auto playbackInfo = session.GetPlaybackInfo();
                const auto controls = playbackInfo.Controls();
                const auto timeline = session.GetTimelineProperties();

                snapshot.title = toQString(properties.Title()).trimmed();
                snapshot.artist = toQString(properties.Artist()).trimmed();
                snapshot.source = toQString(session.SourceAppUserModelId()).trimmed();
                snapshot.available = !snapshot.title.isEmpty() || !snapshot.artist.isEmpty();
                snapshot.playing = playbackInfo.PlaybackStatus()
                    == MediaControl::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                snapshot.canPrevious = controls.IsPreviousEnabled();
                snapshot.canNext = controls.IsNextEnabled();

                const qint64 positionMs = timeline.Position().count() / 10000;
                const qint64 startMs = timeline.StartTime().count() / 10000;
                const qint64 endMs = timeline.EndTime().count() / 10000;
                const qint64 durationMs = qMax<qint64>(0, endMs - startMs);
                snapshot.progress = durationMs > 0
                    ? qBound(0.0, static_cast<double>(positionMs - startMs)
                                      / static_cast<double>(durationMs), 1.0)
                    : 0.0;

                const auto formatTime = [](qint64 totalMilliseconds) {
                    const qint64 totalSeconds = qMax<qint64>(0, totalMilliseconds) / 1000;
                    const qint64 hours = totalSeconds / 3600;
                    const qint64 minutes = (totalSeconds % 3600) / 60;
                    const qint64 seconds = totalSeconds % 60;
                    if (hours > 0) {
                        return QStringLiteral("%1:%2:%3")
                            .arg(hours)
                            .arg(minutes, 2, 10, QLatin1Char('0'))
                            .arg(seconds, 2, 10, QLatin1Char('0'));
                    }
                    return QStringLiteral("%1:%2")
                        .arg(minutes)
                        .arg(seconds, 2, 10, QLatin1Char('0'));
                };
                snapshot.positionText = formatTime(qMax<qint64>(0, positionMs - startMs));
                snapshot.durationText = formatTime(durationMs);
                snapshot.identity = snapshot.title + QLatin1Char('\n') + snapshot.artist
                                    + QLatin1Char('\n') + snapshot.source;

                if (snapshot.identity == previousIdentity) {
                    snapshot.artworkUrl = previousArtworkUrl;
                } else if (const auto thumbnail = properties.Thumbnail()) {
                    const auto stream = thumbnail.OpenReadAsync().get();
                    const auto size = static_cast<uint32_t>(
                        qMin<uint64_t>(stream.Size(), 8 * 1024 * 1024));
                    if (size > 0) {
                        StorageStreams::Buffer buffer(size);
                        const StorageStreams::IBuffer readBuffer = stream.ReadAsync(
                            buffer, size, StorageStreams::InputStreamOptions::None).get();
                        unsigned char *bytes = nullptr;
                        const auto byteAccess = readBuffer.as<IBufferByteAccess>();
                        if (SUCCEEDED(byteAccess->Buffer(&bytes)) && bytes) {
                            QImage image;
                            if (image.loadFromData(bytes, static_cast<int>(readBuffer.Length()))) {
                                const QString cacheDir = QStandardPaths::writableLocation(
                                    QStandardPaths::CacheLocation);
                                QDir().mkpath(cacheDir);
                                const QByteArray artworkBytes(
                                    reinterpret_cast<const char *>(bytes),
                                    static_cast<qsizetype>(readBuffer.Length()));
                                const QString artworkHash = QString::fromLatin1(
                                    QCryptographicHash::hash(artworkBytes,
                                                             QCryptographicHash::Sha256)
                                        .toHex()
                                        .left(24));
                                const QString artworkPath = cacheDir
                                    + QStringLiteral("/media-artwork-%1.png").arg(artworkHash);
                                if (image.save(artworkPath)) {
                                    snapshot.artworkUrl = QUrl::fromLocalFile(artworkPath).toString();
                                }
                            }
                        }
                    }
                }
            }
        } catch (...) {
            std::scoped_lock lock(platform->mediaMutex);
            platform->mediaSession = nullptr;
        }

        if (apartmentInitialized) {
            winrt::uninit_apartment();
        }
        platform->mediaRefreshInFlight = false;

        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self, [self, snapshot]() {
            if (!self) {
                return;
            }
            const bool changed = self->m_mediaAvailable != snapshot.available
                || self->m_mediaTitle != snapshot.title
                || self->m_mediaArtist != snapshot.artist
                || self->m_mediaSource != snapshot.source
                || self->m_mediaArtworkUrl != snapshot.artworkUrl
                || self->m_mediaPlaying != snapshot.playing
                || self->m_mediaCanPrevious != snapshot.canPrevious
                || self->m_mediaCanNext != snapshot.canNext
                || !qFuzzyCompare(self->m_mediaProgress + 1.0, snapshot.progress + 1.0)
                || self->m_mediaPositionText != snapshot.positionText
                || self->m_mediaDurationText != snapshot.durationText;

            self->m_mediaAvailable = snapshot.available;
            self->m_mediaTitle = snapshot.title;
            self->m_mediaArtist = snapshot.artist;
            self->m_mediaSource = snapshot.source;
            self->m_mediaArtworkUrl = snapshot.artworkUrl;
            self->m_mediaIdentity = snapshot.identity;
            self->m_mediaPlaying = snapshot.playing;
            self->m_mediaCanPrevious = snapshot.canPrevious;
            self->m_mediaCanNext = snapshot.canNext;
            self->m_mediaProgress = snapshot.progress;
            self->m_mediaPositionText = snapshot.positionText;
            self->m_mediaDurationText = snapshot.durationText;
            if (changed) {
                emit self->mediaChanged();
            }
        }, Qt::QueuedConnection);
    });
#endif
}

void IslandController::refreshSystemState()
{
#ifdef Q_OS_WIN
    QString networkName;
    QString networkStatus = QStringLiteral("Offline");
    try {
        if (const auto profile = Connectivity::NetworkInformation::GetInternetConnectionProfile()) {
            networkName = toQString(profile.ProfileName()).trimmed();
            switch (profile.GetNetworkConnectivityLevel()) {
            case Connectivity::NetworkConnectivityLevel::InternetAccess:
                networkStatus = QStringLiteral("Internet access");
                break;
            case Connectivity::NetworkConnectivityLevel::ConstrainedInternetAccess:
                networkStatus = QStringLiteral("Limited access");
                break;
            case Connectivity::NetworkConnectivityLevel::LocalAccess:
                networkStatus = QStringLiteral("Local network");
                break;
            default:
                break;
            }
        }
    } catch (...) {
    }

    SYSTEM_POWER_STATUS powerStatus{};
    bool batteryAvailable = false;
    int batteryPercent = 0;
    QString powerText = QStringLiteral("Power status unavailable");
    if (GetSystemPowerStatus(&powerStatus)) {
        batteryAvailable = powerStatus.BatteryFlag != 128 && powerStatus.BatteryLifePercent != 255;
        batteryPercent = batteryAvailable ? powerStatus.BatteryLifePercent : 0;
        if (!batteryAvailable) {
            powerText = powerStatus.ACLineStatus == 1 ? QStringLiteral("Plugged in")
                                                      : QStringLiteral("Desktop power");
        } else if (powerStatus.ACLineStatus == 1) {
            powerText = QStringLiteral("%1% · charging").arg(batteryPercent);
        } else {
            powerText = QStringLiteral("%1% remaining").arg(batteryPercent);
        }
    }

    int volume = m_volume;
    bool muted = m_muted;
    readDefaultAudioEndpoint(&volume, &muted);

    const bool changed = m_networkName != networkName || m_networkStatus != networkStatus
                         || m_batteryAvailable != batteryAvailable
                         || m_batteryPercent != batteryPercent || m_powerText != powerText
                         || m_volume != volume || m_muted != muted;
    m_networkName = networkName;
    m_networkStatus = networkStatus;
    m_batteryAvailable = batteryAvailable;
    m_batteryPercent = batteryPercent;
    m_powerText = powerText;
    m_volume = volume;
    m_muted = muted;
    if (changed) {
        emit systemChanged();
    }
#endif
}

QString IslandController::formatDuration(qint64 totalMilliseconds) const
{
    const qint64 totalSeconds = qMax<qint64>(0, totalMilliseconds) / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}
