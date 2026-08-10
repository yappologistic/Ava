#pragma once

#include "liquidglasscaptureworker.h"

#include <QObject>
#include <QPointer>

#include <array>
#include <atomic>
#include <memory>

class QQuickWindow;

class LiquidGlassBackdrop final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool frameAvailable READ frameAvailable NOTIFY frameChanged)
    Q_PROPERTY(bool timerFrameAvailable READ timerFrameAvailable NOTIFY frameChanged)
    Q_PROPERTY(int surfacePadding READ surfacePadding NOTIFY frameChanged)
    Q_PROPERTY(int timerPadding READ timerPadding NOTIFY frameChanged)
    Q_PROPERTY(int surfaceOffsetX READ surfaceOffsetX NOTIFY surfaceGeometryChanged)
    Q_PROPERTY(int surfaceOffsetY READ surfaceOffsetY NOTIFY surfaceGeometryChanged)
    Q_PROPERTY(int surfaceContentWidth READ surfaceContentWidth NOTIFY surfaceGeometryChanged)
    Q_PROPERTY(int surfaceContentHeight READ surfaceContentHeight NOTIFY surfaceGeometryChanged)
    Q_PROPERTY(bool realtimeCaptureActive READ realtimeCaptureActive NOTIFY captureModeChanged)

public:
    explicit LiquidGlassBackdrop(QObject *parent = nullptr);
    ~LiquidGlassBackdrop() override;

    bool active() const { return m_active; }
    bool frameAvailable() const;
    bool timerFrameAvailable() const;
    int surfacePadding() const { return m_surfacePadding; }
    int timerPadding() const { return m_timerPadding; }
    int surfaceOffsetX() const { return m_surfaceOffsetX; }
    int surfaceOffsetY() const { return m_surfaceOffsetY; }
    int surfaceContentWidth() const { return m_surfaceContentWidth; }
    int surfaceContentHeight() const { return m_surfaceContentHeight; }
    bool realtimeCaptureActive() const { return m_nativeCaptureActive; }
    std::shared_ptr<NativeLiquidGlassTextureLease> nativeSurfaceTexture() const;
    std::shared_ptr<NativeLiquidGlassTextureLease> nativeTimerTexture() const;
    QRect nativeSurfaceContentRect() const { return m_nativeSurfaceContentRect; }
    QRect nativeTimerContentRect() const { return m_nativeTimerContentRect; }

    void attachWindow(QQuickWindow *window, QObject *root);

public slots:
    void setActive(bool active);
    void requestImmediateFrame();

signals:
    void activeChanged();
    void frameChanged();
    void captureModeChanged();
    void surfaceGeometryChanged();

private:
    enum NativeFrameSlotState {
        NativeFrameSlotFree = 0,
        NativeFrameSlotWriting,
        NativeFrameSlotReady,
        NativeFrameSlotReading
    };

    struct NativeFrameSlot {
        std::atomic_int state{NativeFrameSlotFree};
        NativeLiquidGlassFrame frame;
        quint64 sequence = 0;
    };

    void startNativeCapture();
    void stopNativeCapture();
    void updateNativeGeometry();
    void submitNativeFrame(NativeLiquidGlassFrame frame);
    void publishNativeFrame();
    void scheduleNativePublish();
    void clearFrames();
    bool m_active = false;
    QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_root;
    std::unique_ptr<LiquidGlassCaptureWorker> m_nativeWorker;
    std::array<NativeFrameSlot, 3> m_pendingNativeFrames;
    std::atomic<quint64> m_nativeFrameSequence{0};
    std::atomic_bool m_nativePublishScheduled{false};
    std::shared_ptr<NativeLiquidGlassTextureLease> m_nativeSurfaceTexture;
    std::shared_ptr<NativeLiquidGlassTextureLease> m_nativeTimerTexture;
    QRect m_nativeSurfaceContentRect;
    QRect m_nativeTimerContentRect;
    int m_surfacePadding = 0;
    int m_timerPadding = 0;
    int m_surfaceOffsetX = 0;
    int m_surfaceOffsetY = 0;
    int m_surfaceContentWidth = 1;
    int m_surfaceContentHeight = 1;
    bool m_nativeCaptureActive = false;
    int m_revision = 0;
};
