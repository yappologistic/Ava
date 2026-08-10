#include "liquidglassbackdrop.h"

#include <QQuickWindow>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

LiquidGlassBackdrop::LiquidGlassBackdrop(QObject *parent)
    : QObject(parent)
    , m_nativeWorker(std::make_unique<LiquidGlassCaptureWorker>(
          [this](NativeLiquidGlassFrame frame) {
              submitNativeFrame(std::move(frame));
          }))
{
}

LiquidGlassBackdrop::~LiquidGlassBackdrop()
{
    stopNativeCapture();
}

bool LiquidGlassBackdrop::frameAvailable() const
{
    return bool(m_nativeSurfaceTexture);
}

std::shared_ptr<NativeLiquidGlassTextureLease>
LiquidGlassBackdrop::nativeSurfaceTexture() const
{
    return m_nativeSurfaceTexture;
}

bool LiquidGlassBackdrop::timerFrameAvailable() const
{
    return bool(m_nativeTimerTexture);
}

std::shared_ptr<NativeLiquidGlassTextureLease>
LiquidGlassBackdrop::nativeTimerTexture() const
{
    return m_nativeTimerTexture;
}

void LiquidGlassBackdrop::attachWindow(QQuickWindow *window, QObject *root)
{
    m_window = window;
    m_root = root;
    if (m_active) {
        startNativeCapture();
        requestImmediateFrame();
    }
}

void LiquidGlassBackdrop::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    if (m_active) {
        startNativeCapture();
        requestImmediateFrame();
    } else {
        stopNativeCapture();
        clearFrames();
    }
    emit activeChanged();
}

void LiquidGlassBackdrop::requestImmediateFrame()
{
    if (!m_active) {
        return;
    }

    if (!m_nativeCaptureActive) {
        startNativeCapture();
    }
    updateNativeGeometry();
}

void LiquidGlassBackdrop::startNativeCapture()
{
#ifdef Q_OS_WIN
    if (!m_active || !m_window || !m_root) {
        return;
    }
    updateNativeGeometry();
    if (m_nativeWorker) {
        m_nativeWorker->start();
    }
    if (!m_nativeCaptureActive) {
        m_nativeCaptureActive = true;
        emit captureModeChanged();
    }
#endif
}

void LiquidGlassBackdrop::stopNativeCapture()
{
    const bool wasRealtimeCaptureActive = m_nativeCaptureActive;
    if (m_nativeWorker) {
        m_nativeWorker->stop();
    }

    m_nativeCaptureActive = false;
    for (NativeFrameSlot &slot : m_pendingNativeFrames) {
        slot.frame = {};
        slot.sequence = 0;
        slot.state.store(NativeFrameSlotFree, std::memory_order_release);
    }
    m_nativePublishScheduled.store(false, std::memory_order_release);
    if (wasRealtimeCaptureActive) {
        emit captureModeChanged();
    }
}

void LiquidGlassBackdrop::updateNativeGeometry()
{
#ifdef Q_OS_WIN
    if (!m_window || !m_root || !m_window->isVisible()) {
        return;
    }

    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    RECT windowRect{};
    if (!GetWindowRect(hwnd, &windowRect)) {
        return;
    }

    const qreal scale = qMax<qreal>(1.0, m_window->devicePixelRatio());
    const qreal bodyWidth = m_root->property("islandVisualWidth").toReal();
    const qreal surfaceHeight = m_root->property("surfaceHeight").toReal();
    const qreal overflow = m_root->property("shellHorizontalOverflow").toReal();
    const qreal surfaceTop = m_root->property("islandSurfaceTop").toReal();
    const qreal surfaceLeft = (m_window->width() - bodyWidth) / 2.0 - overflow;
    const QSize surfaceSize(qMax(1, qRound((bodyWidth + overflow * 2.0) * scale)),
                            qMax(1, qRound(surfaceHeight * scale)));
    const QPoint surfaceOrigin(windowRect.left + qRound(surfaceLeft * scale),
                               windowRect.top + qRound(surfaceTop * scale));
    // The optical shader can pull rays tens of logical pixels outside the
    // surface. Keep real surrounding desktop content available instead of
    // clamping those rays to the capture edge, which flattens the meniscus.
    const int padding = qMax(48, qRound(64.0 * scale));
    const qreal fixedWidth = qMax(bodyWidth + overflow * 2.0,
                                  m_root->property("liquidGlassCaptureWidth").toReal());
    const qreal fixedHeight = qMax(surfaceHeight,
                                   m_root->property("liquidGlassCaptureHeight").toReal());
    const QSize fixedSurfaceSize(qMax(1, qRound(fixedWidth * scale)),
                                 qMax(1, qRound(fixedHeight * scale)));
    const QPoint fixedSurfaceOrigin(
        windowRect.left + qRound((m_window->width() - fixedWidth) / 2.0 * scale),
        windowRect.top + qRound(surfaceTop * scale));
    const QRect surfaceCapture(fixedSurfaceOrigin - QPoint(padding, padding),
                               fixedSurfaceSize + QSize(padding * 2, padding * 2));

    const QPoint contentOffset = surfaceOrigin - surfaceCapture.topLeft();
    const bool surfaceGeometryDidChange = m_surfaceOffsetX != contentOffset.x()
        || m_surfaceOffsetY != contentOffset.y()
        || m_surfaceContentWidth != surfaceSize.width()
        || m_surfaceContentHeight != surfaceSize.height();
    m_surfaceOffsetX = contentOffset.x();
    m_surfaceOffsetY = contentOffset.y();
    m_surfaceContentWidth = surfaceSize.width();
    m_surfaceContentHeight = surfaceSize.height();
    if (surfaceGeometryDidChange) {
        emit surfaceGeometryChanged();
    }

    QRect timerCapture;
    if (m_root->property("timerSatelliteVisible").toBool()) {
        const qreal diameter = m_root->property("timerSatelliteDiameter").toReal();
        const qreal gap = m_root->property("timerSatelliteGap").toReal();
        const qreal timerLeft = (m_window->width() + bodyWidth) / 2.0 + overflow + gap;
        const QSize timerSize(qMax(1, qRound(diameter * scale)),
                              qMax(1, qRound(diameter * scale)));
        const QPoint timerOrigin(windowRect.left + qRound(timerLeft * scale),
                                 windowRect.top + qRound(surfaceTop * scale));
        timerCapture = QRect(timerOrigin - QPoint(padding, padding),
                             timerSize + QSize(padding * 2, padding * 2));
    }

    if (m_nativeWorker) {
        const bool pill = m_root->property("pillMode").toBool();
        const qreal expansion = qBound<qreal>(
            0.0, (surfaceHeight - 39.0) / 100.0, 1.0);
        NativeLiquidGlassOptics optics;
        optics.contentRect = QRect(surfaceOrigin, surfaceSize);
        optics.radius = float((pill
                                   ? m_root->property("pillCornerRadius").toReal()
                                   : m_root->property("dynamicCornerRadius").toReal())
                              * scale);
        optics.sideInset = float((pill
                                      ? 0.0
                                      : m_root->property("dynamicEarWidth").toReal())
                                 * scale);
        optics.earDepth = float(qMax<qreal>(
            1.0, m_root->property("dynamicEarDepth").toReal()) * scale);
        optics.lensBand = float((pill
                                     ? qMin(surfaceHeight * 0.30,
                                            9.0 + expansion * 10.0)
                                     : qMin<qreal>(22.0,
                                                   qMax<qreal>(11.0,
                                                               optics.radius
                                                                   / scale * 0.56)))
                                * scale);
        optics.thickness = float(pill
                                     ? 0.52 + expansion * 0.22
                                     : 0.60 + expansion * 0.16);
        optics.intensity = pill ? 0.90f : 0.94f;
        optics.pointer = QPointF(
            m_root->property("liquidGlassPointerX").toReal() * scale,
            m_root->property("liquidGlassPointerY").toReal() * scale);
        optics.pointerActive = m_root->property("liquidGlassPointerActive").toBool();
        optics.pill = pill;
        m_nativeWorker->setGeometry(surfaceCapture,
                                    timerCapture,
                                    padding,
                                    reinterpret_cast<quintptr>(hwnd),
                                    optics);
    }
#endif
}

void LiquidGlassBackdrop::submitNativeFrame(NativeLiquidGlassFrame frame)
{
    NativeFrameSlot *target = nullptr;
    for (NativeFrameSlot &slot : m_pendingNativeFrames) {
        int expected = NativeFrameSlotFree;
        if (slot.state.compare_exchange_strong(expected,
                                               NativeFrameSlotWriting,
                                               std::memory_order_acq_rel)) {
            target = &slot;
            break;
        }
    }

    if (!target) {
        NativeFrameSlot *oldest = nullptr;
        for (NativeFrameSlot &slot : m_pendingNativeFrames) {
            if (slot.state.load(std::memory_order_acquire) == NativeFrameSlotReady
                && (!oldest || slot.sequence < oldest->sequence)) {
                oldest = &slot;
            }
        }
        if (oldest) {
            int expected = NativeFrameSlotReady;
            if (oldest->state.compare_exchange_strong(expected,
                                                      NativeFrameSlotWriting,
                                                      std::memory_order_acq_rel)) {
                target = oldest;
            }
        }
    }

    if (!target) {
        return;
    }

    target->frame = std::move(frame);
    target->sequence = m_nativeFrameSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    target->state.store(NativeFrameSlotReady, std::memory_order_release);
    scheduleNativePublish();
}

void LiquidGlassBackdrop::scheduleNativePublish()
{
    if (m_nativePublishScheduled.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    QMetaObject::invokeMethod(this,
                              [this]() { publishNativeFrame(); },
                              Qt::QueuedConnection);
}

void LiquidGlassBackdrop::publishNativeFrame()
{
    NativeFrameSlot *newest = nullptr;
    for (NativeFrameSlot &slot : m_pendingNativeFrames) {
        if (slot.state.load(std::memory_order_acquire) == NativeFrameSlotReady
            && (!newest || slot.sequence > newest->sequence)) {
            newest = &slot;
        }
    }

    NativeLiquidGlassFrame frame;
    if (newest) {
        int expected = NativeFrameSlotReady;
        if (newest->state.compare_exchange_strong(expected,
                                                  NativeFrameSlotReading,
                                                  std::memory_order_acq_rel)) {
            frame = std::move(newest->frame);
            newest->frame = {};
            newest->sequence = 0;
            newest->state.store(NativeFrameSlotFree, std::memory_order_release);
        }
    }

    // Coalescing keeps GUI delivery bounded. Superseded leases are released on
    // the producer thread without ever blocking the scene-graph thread.
    for (NativeFrameSlot &slot : m_pendingNativeFrames) {
        int expected = NativeFrameSlotReady;
        if (slot.state.compare_exchange_strong(expected,
                                               NativeFrameSlotWriting,
                                               std::memory_order_acq_rel)) {
            slot.frame = {};
            slot.sequence = 0;
            slot.state.store(NativeFrameSlotFree, std::memory_order_release);
        }
    }

    if (m_active && frame.surfaceTexture) {
        const bool captureModeChangedNow = !m_nativeCaptureActive;
        m_nativeCaptureActive = true;
        m_surfacePadding = frame.surfacePadding;
        m_timerPadding = frame.timerPadding;
        m_nativeSurfaceContentRect = frame.surfaceContentRect;
        m_nativeTimerContentRect = frame.timerContentRect;
        m_nativeSurfaceTexture = std::move(frame.surfaceTexture);
        m_nativeTimerTexture = std::move(frame.timerTexture);
        updateNativeGeometry();
        ++m_revision;
        if (captureModeChangedNow) {
            emit captureModeChanged();
        }
        emit frameChanged();
    }

    m_nativePublishScheduled.store(false, std::memory_order_release);
    for (const NativeFrameSlot &slot : m_pendingNativeFrames) {
        if (slot.state.load(std::memory_order_acquire) == NativeFrameSlotReady) {
            scheduleNativePublish();
            break;
        }
    }
}

void LiquidGlassBackdrop::clearFrames()
{
    const bool hadNativeSurface = bool(m_nativeSurfaceTexture) || bool(m_nativeTimerTexture);
    for (NativeFrameSlot &slot : m_pendingNativeFrames) {
        slot.frame = {};
        slot.sequence = 0;
        slot.state.store(NativeFrameSlotFree, std::memory_order_release);
    }
    m_nativeSurfaceTexture.reset();
    m_nativeTimerTexture.reset();
    m_nativeSurfaceContentRect = {};
    m_nativeTimerContentRect = {};
    m_nativePublishScheduled.store(false, std::memory_order_release);
    m_nativeCaptureActive = false;
    if (!hadNativeSurface) {
        return;
    }
    m_surfacePadding = 0;
    m_timerPadding = 0;
    const bool surfaceGeometryDidChange = m_surfaceOffsetX != 0
        || m_surfaceOffsetY != 0
        || m_surfaceContentWidth != 1
        || m_surfaceContentHeight != 1;
    m_surfaceOffsetX = 0;
    m_surfaceOffsetY = 0;
    m_surfaceContentWidth = 1;
    m_surfaceContentHeight = 1;
    ++m_revision;
    if (surfaceGeometryDidChange) {
        emit surfaceGeometryChanged();
    }
    emit frameChanged();
}
