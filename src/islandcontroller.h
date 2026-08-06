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
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY reducedMotionChanged)

    Q_PROPERTY(QString timeText READ timeText NOTIFY clockChanged)
    Q_PROPERTY(QString meridiemText READ meridiemText NOTIFY clockChanged)
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

    Q_PROPERTY(bool mediaAvailable READ mediaAvailable NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaArtist READ mediaArtist NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaSource READ mediaSource NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaArtworkUrl READ mediaArtworkUrl NOTIFY mediaChanged)
    Q_PROPERTY(bool mediaPlaying READ mediaPlaying NOTIFY mediaChanged)
    Q_PROPERTY(bool mediaCanPrevious READ mediaCanPrevious NOTIFY mediaChanged)
    Q_PROPERTY(bool mediaCanNext READ mediaCanNext NOTIFY mediaChanged)
    Q_PROPERTY(double mediaProgress READ mediaProgress NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaPositionText READ mediaPositionText NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaDurationText READ mediaDurationText NOTIFY mediaChanged)

    Q_PROPERTY(QString networkName READ networkName NOTIFY systemChanged)
    Q_PROPERTY(QString networkStatus READ networkStatus NOTIFY systemChanged)
    Q_PROPERTY(bool batteryAvailable READ batteryAvailable NOTIFY systemChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY systemChanged)
    Q_PROPERTY(QString powerText READ powerText NOTIFY systemChanged)
    Q_PROPERTY(int volume READ volume NOTIFY systemChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY systemChanged)

    Q_PROPERTY(int droppedFileCount READ droppedFileCount NOTIFY droppedFilesChanged)
    Q_PROPERTY(QString lastDroppedFile READ lastDroppedFile NOTIFY droppedFilesChanged)

public:
    explicit IslandController(QObject *parent = nullptr);
    ~IslandController() override;

    bool expanded() const { return m_expanded; }
    bool pinned() const { return m_pinned; }
    bool reducedMotion() const { return m_reducedMotion; }

    QString timeText() const { return m_timeText; }
    QString meridiemText() const { return m_meridiemText; }
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

    bool mediaAvailable() const { return m_mediaAvailable; }
    QString mediaTitle() const { return m_mediaTitle; }
    QString mediaArtist() const { return m_mediaArtist; }
    QString mediaSource() const { return m_mediaSource; }
    QString mediaArtworkUrl() const { return m_mediaArtworkUrl; }
    bool mediaPlaying() const { return m_mediaPlaying; }
    bool mediaCanPrevious() const { return m_mediaCanPrevious; }
    bool mediaCanNext() const { return m_mediaCanNext; }
    double mediaProgress() const { return m_mediaProgress; }
    QString mediaPositionText() const { return m_mediaPositionText; }
    QString mediaDurationText() const { return m_mediaDurationText; }

    QString networkName() const { return m_networkName; }
    QString networkStatus() const { return m_networkStatus; }
    bool batteryAvailable() const { return m_batteryAvailable; }
    int batteryPercent() const { return m_batteryPercent; }
    QString powerText() const { return m_powerText; }
    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }

    int droppedFileCount() const { return m_droppedFiles.size(); }
    QString lastDroppedFile() const;

public slots:
    void setExpanded(bool expanded);
    void toggleExpanded();
    void setPinned(bool pinned);
    void togglePinned();

    void openTimer();
    void closeTimer();
    void startTimer(int durationSeconds);
    void toggleTimerPaused();
    void addTimerMinute();
    void cancelTimer();
    void dismissTimer();

    void togglePlayback();
    void previousTrack();
    void nextTrack();
    void setVolume(int volume);
    void toggleMute();

    void handleDrop(const QVariantList &urls);
    void clearDroppedFiles();
    void revealLastDroppedFile();

signals:
    void expandedChanged();
    void pinnedChanged();
    void reducedMotionChanged();
    void clockChanged();
    void timerChanged();
    void mediaChanged();
    void systemChanged();
    void droppedFilesChanged();

private slots:
    void tick();
    void updateTimer();
    void soundTimerAlert();

private:
    struct PlatformState;

    void updateClock();
    void refreshMedia();
    void refreshSystemState();
    QString formatDuration(qint64 totalMilliseconds) const;
    QString formatTimerDuration(qint64 totalMilliseconds) const;
    void finishTimer();

    std::shared_ptr<PlatformState> m_platform;
    QTimer m_timer;
    QTimer m_countdownTimer;
    QTimer m_alarmTimer;
    int m_slowRefreshCounter = 0;

    bool m_expanded = false;
    bool m_pinned = false;
    bool m_reducedMotion = false;

    QString m_timeText;
    QString m_meridiemText;
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

    bool m_mediaAvailable = false;
    QString m_mediaTitle;
    QString m_mediaArtist;
    QString m_mediaSource;
    QString m_mediaArtworkUrl;
    QString m_mediaIdentity;
    bool m_mediaPlaying = false;
    bool m_mediaCanPrevious = false;
    bool m_mediaCanNext = false;
    double m_mediaProgress = 0.0;
    QString m_mediaPositionText = QStringLiteral("0:00");
    QString m_mediaDurationText = QStringLiteral("0:00");

    QString m_networkName;
    QString m_networkStatus = QStringLiteral("Offline");
    bool m_batteryAvailable = false;
    int m_batteryPercent = 0;
    QString m_powerText;
    int m_volume = 0;
    bool m_muted = false;

    QStringList m_droppedFiles;
};
