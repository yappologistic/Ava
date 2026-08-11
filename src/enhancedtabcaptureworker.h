#pragma once

#include "enhancedtabtexture.h"

#include <QVector>

#include <functional>
#include <memory>

class EnhancedTabCaptureWorker final
{
public:
    using FrameCallback = std::function<void(NativeEnhancedTabFrame)>;

    explicit EnhancedTabCaptureWorker(FrameCallback callback);
    ~EnhancedTabCaptureWorker();

    EnhancedTabCaptureWorker(const EnhancedTabCaptureWorker &) = delete;
    EnhancedTabCaptureWorker &operator=(const EnhancedTabCaptureWorker &) = delete;

    void start();
    void stop();
    void setWindows(const QVector<quintptr> &windows);

    static bool isSupported();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
