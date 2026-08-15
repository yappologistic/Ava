#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <memory>

class IslandController final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool expanded READ expanded WRITE setExpanded NOTIFY expandedChanged)
    Q_PROPERTY(bool pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)
    Q_PROPERTY(bool pillMode READ pillMode WRITE setPillMode NOTIFY pillModeChanged)
    Q_PROPERTY(bool liquidGlassEnabled READ liquidGlassEnabled WRITE setLiquidGlassEnabled NOTIFY liquidGlassEnabledChanged)
    Q_PROPERTY(bool liquidGlassBlurred READ liquidGlassBlurred WRITE setLiquidGlassBlurred NOTIFY liquidGlassBlurredChanged)
    Q_PROPERTY(bool monitorEnabled READ monitorEnabled WRITE setMonitorEnabled NOTIFY monitorEnabledChanged)
    Q_PROPERTY(bool settingsOpen READ settingsOpen WRITE setSettingsOpen NOTIFY settingsOpenChanged)
    Q_PROPERTY(int compactWidth READ compactWidth WRITE setCompactWidth NOTIFY compactWidthChanged)
    Q_PROPERTY(bool mediaArtworkAccentEnabled READ mediaArtworkAccentEnabled WRITE setMediaArtworkAccentEnabled NOTIFY mediaArtworkAccentEnabledChanged)
    Q_PROPERTY(bool audioPulseEnabled READ audioPulseEnabled WRITE setAudioPulseEnabled NOTIFY audioPulseEnabledChanged)
    Q_PROPERTY(bool mediaPeekEnabled READ mediaPeekEnabled WRITE setMediaPeekEnabled NOTIFY mediaPeekEnabledChanged)
    Q_PROPERTY(bool timerSatelliteEnabled READ timerSatelliteEnabled WRITE setTimerSatelliteEnabled NOTIFY timerSatelliteEnabledChanged)
    Q_PROPERTY(QString weekStartMode READ weekStartMode WRITE setWeekStartMode NOTIFY weekStartModeChanged)
    Q_PROPERTY(int calendarWeekStartDay READ calendarWeekStartDay NOTIFY weekStartModeChanged)
    Q_PROPERTY(bool respectFullscreenApps READ respectFullscreenApps WRITE setRespectFullscreenApps NOTIFY respectFullscreenAppsChanged)
    Q_PROPERTY(QString motionMode READ motionMode WRITE setMotionMode NOTIFY motionModeChanged)
    Q_PROPERTY(int hoverOpenDelay READ hoverOpenDelay WRITE setHoverOpenDelay NOTIFY hoverOpenDelayChanged)
    Q_PROPERTY(int leaveCloseDelay READ leaveCloseDelay WRITE setLeaveCloseDelay NOTIFY leaveCloseDelayChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY reducedMotionChanged)

    Q_PROPERTY(QString timeText READ timeText NOTIFY clockChanged)
    Q_PROPERTY(QString compactDateText READ compactDateText NOTIFY clockChanged)
    Q_PROPERTY(QString dateText READ dateText NOTIFY clockChanged)

    Q_PROPERTY(bool timerPanelOpen READ timerPanelOpen NOTIFY timerChanged)
    Q_PROPERTY(bool timerActive READ timerActive NOTIFY timerChanged)
    Q_PROPERTY(bool timerPaused READ timerPaused NOTIFY timerChanged)
    Q_PROPERTY(bool timerRinging READ timerRinging NOTIFY timerChanged)
    Q_PROPERTY(int timerDurationSeconds READ timerDurationSeconds NOTIFY timerChanged)
    Q_PROPERTY(int timerRemainingSeconds READ timerRemainingSeconds NOTIFY timerChanged)
    Q_PROPERTY(QString timerRemainingText READ timerRemainingText NOTIFY timerChanged)
    Q_PROPERTY(double timerProgress READ timerProgress NOTIFY timerChanged)

    Q_PROPERTY(bool wallpaperPanelOpen READ wallpaperPanelOpen NOTIFY wallpaperChanged)
    Q_PROPERTY(int wallpaperIndex READ wallpaperIndex NOTIFY wallpaperChanged)
    Q_PROPERTY(QString wallpaperStatus READ wallpaperStatus NOTIFY wallpaperChanged)

    Q_PROPERTY(bool mediaAvailable READ mediaAvailable NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaArtist READ mediaArtist NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaSource READ mediaSource NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaAppIconUrl READ mediaAppIconUrl NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaArtworkUrl READ mediaArtworkUrl NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaArtworkAccent READ mediaArtworkAccent NOTIFY mediaChanged)
    Q_PROPERTY(bool mediaPlaying READ mediaPlaying NOTIFY mediaChanged)
    Q_PROPERTY(bool mediaCanPrevious READ mediaCanPrevious NOTIFY mediaChanged)
    Q_PROPERTY(bool mediaCanNext READ mediaCanNext NOTIFY mediaChanged)
    Q_PROPERTY(bool mediaSeekable READ mediaSeekable NOTIFY mediaChanged)
    Q_PROPERTY(double mediaProgress READ mediaProgress NOTIFY mediaChanged)
    Q_PROPERTY(qint64 mediaDurationMilliseconds READ mediaDurationMilliseconds NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaPositionText READ mediaPositionText NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaDurationText READ mediaDurationText NOTIFY mediaChanged)
    Q_PROPERTY(QVariantList audioPeakLevels READ audioPeakLevels NOTIFY audioPeakChanged)

    Q_PROPERTY(QString networkName READ networkName NOTIFY systemChanged)
    Q_PROPERTY(QString networkStatus READ networkStatus NOTIFY systemChanged)
    Q_PROPERTY(bool batteryAvailable READ batteryAvailable NOTIFY systemChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY systemChanged)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY systemChanged)
    Q_PROPERTY(QString powerText READ powerText NOTIFY systemChanged)
    Q_PROPERTY(int cpuUsage READ cpuUsage NOTIFY performanceChanged)
    Q_PROPERTY(int gpuUsage READ gpuUsage NOTIFY performanceChanged)
    Q_PROPERTY(bool monitorDetailsOpen READ monitorDetailsOpen NOTIFY monitorDetailsChanged)
    Q_PROPERTY(int memoryUsage READ memoryUsage NOTIFY performanceChanged)
    Q_PROPERTY(int diskUsage READ diskUsage NOTIFY performanceChanged)
    Q_PROPERTY(QString memoryDetailText READ memoryDetailText NOTIFY performanceChanged)
    Q_PROPERTY(QString diskDetailText READ diskDetailText NOTIFY performanceChanged)
    Q_PROPERTY(QVariantList topProcesses READ topProcesses NOTIFY performanceChanged)
    Q_PROPERTY(int volume READ volume NOTIFY systemChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY systemChanged)
    Q_PROPERTY(bool foregroundFullscreen READ foregroundFullscreen NOTIFY foregroundFullscreenChanged)

    Q_PROPERTY(int droppedFileCount READ droppedFileCount NOTIFY droppedFilesChanged)
    Q_PROPERTY(QString lastDroppedFile READ lastDroppedFile NOTIFY droppedFilesChanged)

public:
    explicit IslandController(QObject *parent = nullptr);
    ~IslandController() override;

    bool expanded() const { return m_expanded; }
    bool pinned() const { return m_pinned; }
    bool pillMode() const { return m_pillMode; }
    bool liquidGlassEnabled() const { return m_liquidGlassEnabled; }
    bool liquidGlassBlurred() const { return m_liquidGlassBlurred; }
    bool monitorEnabled() const { return m_monitorEnabled; }
    bool settingsOpen() const { return m_settingsOpen; }
    int compactWidth() const { return m_compactWidth; }
    bool mediaArtworkAccentEnabled() const { return m_mediaArtworkAccentEnabled; }
    bool audioPulseEnabled() const { return m_audioPulseEnabled; }
    bool mediaPeekEnabled() const { return m_mediaPeekEnabled; }
    bool timerSatelliteEnabled() const { return m_timerSatelliteEnabled; }
    QString weekStartMode() const { return m_weekStartMode; }
    int calendarWeekStartDay() const;
    bool respectFullscreenApps() const { return m_respectFullscreenApps; }
    QString motionMode() const { return m_motionMode; }
    int hoverOpenDelay() const { return m_hoverOpenDelay; }
    int leaveCloseDelay() const { return m_leaveCloseDelay; }
    bool reducedMotion() const { return m_reducedMotion; }
#ifdef AVA_TESTING
    bool audioMeterPollingForTest() const { return m_audioMeterTimer.isActive(); }
    bool foregroundFullscreenPollingForTest() const { return m_foregroundTimer.isActive(); }
    int foregroundFullscreenPollIntervalForTest() const { return m_foregroundTimer.interval(); }
    static bool fullscreenGeometryForTest(int windowLeft,
                                          int windowTop,
                                          int windowRight,
                                          int windowBottom,
                                          int monitorLeft,
                                          int monitorTop,
                                          int monitorRight,
                                          int monitorBottom);
    void setMediaStateForTest(bool available, bool playing, bool muted);
    void setForegroundFullscreenForTest(bool fullscreen);
#endif

    QString timeText() const { return m_timeText; }
    QString compactDateText() const { return m_compactDateText; }
    QString dateText() const { return m_dateText; }

    bool timerPanelOpen() const { return m_timerPanelOpen; }
    bool timerActive() const { return m_timerActive; }
    bool timerPaused() const { return m_timerPaused; }
    bool timerRinging() const { return m_timerRinging; }
    int timerDurationSeconds() const { return m_timerDurationSeconds; }
    int timerRemainingSeconds() const { return m_timerRemainingSeconds; }
    QString timerRemainingText() const { return m_timerRemainingText; }
    double timerProgress() const { return m_timerProgress; }

    bool wallpaperPanelOpen() const { return m_wallpaperPanelOpen; }
    int wallpaperIndex() const { return m_wallpaperIndex; }
    QString wallpaperStatus() const { return m_wallpaperStatus; }

    bool mediaAvailable() const { return m_mediaAvailable; }
    QString mediaTitle() const { return m_mediaTitle; }
    QString mediaArtist() const { return m_mediaArtist; }
    QString mediaSource() const { return m_mediaSource; }
    QString mediaAppIconUrl() const { return m_mediaAppIconUrl; }
    QString mediaArtworkUrl() const { return m_mediaArtworkUrl; }
    QString mediaArtworkAccent() const { return m_mediaArtworkAccent; }
    bool mediaPlaying() const { return m_mediaPlaying; }
    bool mediaCanPrevious() const { return m_mediaCanPrevious; }
    bool mediaCanNext() const { return m_mediaCanNext; }
    bool mediaSeekable() const { return m_mediaSeekable; }
    double mediaProgress() const { return m_mediaProgress; }
    qint64 mediaDurationMilliseconds() const { return m_mediaDurationMilliseconds; }
    QString mediaPositionText() const { return m_mediaPositionText; }
    QString mediaDurationText() const { return m_mediaDurationText; }
    QVariantList audioPeakLevels() const { return m_audioPeakLevels; }

    QString networkName() const { return m_networkName; }
    QString networkStatus() const { return m_networkStatus; }
    bool batteryAvailable() const { return m_batteryAvailable; }
    int batteryPercent() const { return m_batteryPercent; }
    bool batteryCharging() const { return m_batteryCharging; }
    QString powerText() const { return m_powerText; }
    int cpuUsage() const { return m_cpuUsage; }
    int gpuUsage() const { return m_gpuUsage; }
    bool monitorDetailsOpen() const { return m_monitorDetailsOpen; }
    int memoryUsage() const { return m_memoryUsage; }
    int diskUsage() const { return m_diskUsage; }
    QString memoryDetailText() const { return m_memoryDetailText; }
    QString diskDetailText() const { return m_diskDetailText; }
    QVariantList topProcesses() const { return m_topProcesses; }
    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    bool foregroundFullscreen() const { return m_foregroundFullscreen; }

    int droppedFileCount() const { return m_droppedFiles.size(); }
    QString lastDroppedFile() const;

public slots:
    void setExpanded(bool expanded);
    void toggleExpanded();
    void setPinned(bool pinned);
    void togglePinned();
    void setPillMode(bool pillMode);
    void togglePillMode();
    void setLiquidGlassEnabled(bool enabled);
    void toggleLiquidGlassEnabled();
    void setLiquidGlassBlurred(bool blurred);
    void toggleLiquidGlassBlurred();
    void setMonitorEnabled(bool enabled);
    void toggleMonitorEnabled();
    void setSettingsOpen(bool open);
    void openSettings();
    void closeSettings();
    void setCompactWidth(int width);
    void setMediaArtworkAccentEnabled(bool enabled);
    void setAudioPulseEnabled(bool enabled);
    void setMediaPeekEnabled(bool enabled);
    void setTimerSatelliteEnabled(bool enabled);
    void setWeekStartMode(const QString &mode);
    void setRespectFullscreenApps(bool enabled);
    void setMotionMode(const QString &mode);
    void setHoverOpenDelay(int milliseconds);
    void setLeaveCloseDelay(int milliseconds);
    void openMonitorDetails();
    void closeMonitorDetails();

    void openTimer();
    void closeTimer();
    void startTimer(int durationSeconds);
    void toggleTimerPaused();
    void addTimerMinute();
    void cancelTimer();
    void dismissTimer();

    void openWallpaperPanel();
    void closeWallpaperPanel();
    void setWallpaper(int index);

    void togglePlayback();
    void previousTrack();
    void nextTrack();
    void seekMedia(double progress);
    void setVolume(int volume);
    void toggleMute();

    void handleDrop(const QVariantList &urls);
    void clearDroppedFiles();
    void revealLastDroppedFile();

signals:
    void expandedChanged();
    void pinnedChanged();
    void pillModeChanged();
    void liquidGlassEnabledChanged();
    void liquidGlassBlurredChanged();
    void monitorEnabledChanged();
    void settingsOpenChanged();
    void compactWidthChanged();
    void mediaArtworkAccentEnabledChanged();
    void audioPulseEnabledChanged();
    void mediaPeekEnabledChanged();
    void timerSatelliteEnabledChanged();
    void weekStartModeChanged();
    void respectFullscreenAppsChanged();
    void motionModeChanged();
    void hoverOpenDelayChanged();
    void leaveCloseDelayChanged();
    void monitorDetailsChanged();
    void reducedMotionChanged();
    void clockChanged();
    void timerChanged();
    void wallpaperChanged();
    void mediaChanged();
    void mediaSeekFinished(bool accepted, double requestedProgress);
    void mediaCommandRejected(const QString &command);
    void audioPeakChanged();
    void systemChanged();
    void performanceChanged();
    void droppedFilesChanged();
    void foregroundFullscreenChanged();

private slots:
    void tick();
    void updateTimer();
    void updateAudioPeak();
    void soundTimerAlert();

private:
    struct PlatformState;

    void updateClock();
    void updateMediaTimeline();
    void syncAudioMeterTimer();
    void updateReducedMotion();
    void refreshMedia();
    void refreshSystemState();
    void refreshPerformanceState();
    void refreshForegroundFullscreen();
    void updateForegroundFullscreen(bool fullscreen);
    QString formatDuration(qint64 totalMilliseconds) const;
    QString formatTimerDuration(qint64 totalMilliseconds) const;
    void finishTimer();

    std::shared_ptr<PlatformState> m_platform;
    QTimer m_timer;
    QTimer m_countdownTimer;
    QTimer m_audioMeterTimer;
    QTimer m_alarmTimer;
    QTimer m_foregroundTimer;
    int m_slowRefreshCounter = 0;
    int m_mediaFallbackCounter = 0;

    bool m_expanded = false;
    bool m_pinned = false;
    bool m_pillMode = false;
    bool m_liquidGlassEnabled = false;
    bool m_liquidGlassBlurred = true;
    bool m_monitorEnabled = false;
    bool m_settingsOpen = false;
    int m_compactWidth = 170;
    bool m_mediaArtworkAccentEnabled = true;
    bool m_audioPulseEnabled = true;
    bool m_mediaPeekEnabled = true;
    bool m_timerSatelliteEnabled = true;
    QString m_weekStartMode = QStringLiteral("system");
    bool m_respectFullscreenApps = true;
    QString m_motionMode = QStringLiteral("system");
    int m_hoverOpenDelay = 280;
    int m_leaveCloseDelay = 560;
    bool m_systemReducedMotion = false;
    bool m_monitorDetailsOpen = false;
    bool m_reducedMotion = false;

    QString m_timeText;
    QString m_compactDateText;
    QString m_dateText;

    bool m_timerPanelOpen = false;
    bool m_timerActive = false;
    bool m_timerPaused = false;
    bool m_timerRinging = false;
    int m_timerDurationSeconds = 0;
    int m_timerRemainingSeconds = 0;
    qint64 m_timerDeadlineMs = 0;
    qint64 m_timerPausedRemainingMs = 0;
    QString m_timerRemainingText = QStringLiteral("0:00");
    double m_timerProgress = 0.0;

    bool m_wallpaperPanelOpen = false;
    int m_wallpaperIndex = 0;
    QString m_wallpaperStatus;

    bool m_mediaAvailable = false;
    QString m_mediaTitle;
    QString m_mediaArtist;
    QString m_mediaSource;
    QString m_mediaAppIconUrl;
    QString m_mediaIconSource;
    QString m_mediaArtworkUrl;
    QString m_mediaArtworkAccent;
    QString m_mediaIdentity;
    bool m_mediaPlaying = false;
    bool m_mediaCanPrevious = false;
    bool m_mediaCanNext = false;
    bool m_mediaSeekable = false;
    double m_mediaProgress = 0.0;
    double m_mediaPlaybackRate = 1.0;
    qint64 m_mediaDurationMilliseconds = 0;
    qint64 m_mediaPositionAtSampleMilliseconds = 0;
    qint64 m_mediaPositionSampleTimestampMilliseconds = 0;
    QString m_mediaPositionText = QStringLiteral("0:00");
    QString m_mediaDurationText = QStringLiteral("0:00");
    QVariantList m_audioPeakLevels{0.0, 0.0, 0.0, 0.0, 0.0};

    QString m_networkName;
    QString m_networkStatus = QStringLiteral("Offline");
    bool m_batteryAvailable = false;
    int m_batteryPercent = 0;
    bool m_batteryCharging = false;
    QString m_powerText;
    int m_cpuUsage = -1;
    int m_gpuUsage = -1;
    int m_memoryUsage = -1;
    int m_diskUsage = -1;
    QString m_memoryDetailText;
    QString m_diskDetailText;
    QVariantList m_topProcesses;
    int m_volume = 0;
    bool m_muted = false;
    bool m_foregroundFullscreen = false;

    QStringList m_droppedFiles;
};
