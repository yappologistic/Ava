#pragma once

#include <QPointF>
#include <QRect>

#include <functional>
#include <memory>

class NativeLiquidGlassTextureLease
{
public:
    virtual ~NativeLiquidGlassTextureLease() = default;

    virtual quint64 sourceId() const = 0;
    virtual quintptr sharedHandle() const = 0;
    virtual quintptr sharedFenceHandle() const = 0;
    virtual quint64 producerFenceValue() const = 0;
    virtual quint64 consumerFenceValue() const = 0;
    virtual QSize size() const = 0;
    virtual void markDisplayed() = 0;
};

struct NativeLiquidGlassFrame
{
    std::shared_ptr<NativeLiquidGlassTextureLease> surfaceTexture;
    std::shared_ptr<NativeLiquidGlassTextureLease> timerTexture;
    QRect surfaceContentRect;
    QRect timerContentRect;
    int surfacePadding = 0;
    int timerPadding = 0;
};

struct NativeLiquidGlassOptics
{
    QRect contentRect;
    float radius = 1.0f;
    float sideInset = 0.0f;
    float earDepth = 1.0f;
    float lensBand = 16.0f;
    float thickness = 0.7f;
    float intensity = 1.0f;
    float edgeStrength = 1.0f;
    QPointF pointer;
    bool pill = true;
    bool pointerActive = false;
    bool reducedMotion = false;
};

class LiquidGlassCaptureWorker final
{
public:
    using FrameCallback = std::function<void(NativeLiquidGlassFrame)>;

    explicit LiquidGlassCaptureWorker(FrameCallback callback);
    ~LiquidGlassCaptureWorker();

    LiquidGlassCaptureWorker(const LiquidGlassCaptureWorker &) = delete;
    LiquidGlassCaptureWorker &operator=(const LiquidGlassCaptureWorker &) = delete;

    void start();
    void stop();
    void setGeometry(const QRect &surfaceCapture,
                     const QRect &timerCapture,
                     int padding,
                     quintptr foregroundWindow,
                     const NativeLiquidGlassOptics &optics);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
