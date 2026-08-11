#include "enhancedtabcaptureworker.h"

#include <QSize>
#include <QString>
#include <QDebug>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/base.h>
#endif

namespace {

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;

constexpr int kMaximumTextureWidth = 1120;
constexpr int kMaximumTextureHeight = 700;
constexpr int kFrameIntervalMs = 30;
constexpr int kMaximumEmptyFrameRetries = 8;
constexpr int kMinimizedFallbackDelayMs = 450;
std::atomic<quint64> g_nextEnhancedTextureId{1};

class SharedTexturePool;

class SharedTextureLease final : public NativeEnhancedTabTexture
{
public:
    SharedTextureLease(std::shared_ptr<SharedTexturePool> pool,
                       int index,
                       quint64 sourceId,
                       HANDLE textureHandle,
                       HANDLE fenceHandle,
                       quint64 producer,
                       quint64 consumer,
                       const QSize &size);
    ~SharedTextureLease() override;

    quint64 sourceId() const override { return m_sourceId; }
    quintptr sharedHandle() const override { return reinterpret_cast<quintptr>(m_textureHandle); }
    quintptr sharedFenceHandle() const override { return reinterpret_cast<quintptr>(m_fenceHandle); }
    quint64 producerFenceValue() const override { return m_producer; }
    quint64 consumerFenceValue() const override { return m_consumer; }
    QSize size() const override { return m_size; }
    void markDisplayed() override;

private:
    std::shared_ptr<SharedTexturePool> m_pool;
    int m_index = -1;
    quint64 m_sourceId = 0;
    HANDLE m_textureHandle = nullptr;
    HANDLE m_fenceHandle = nullptr;
    quint64 m_producer = 0;
    quint64 m_consumer = 0;
    QSize m_size;
    std::atomic_bool m_ready{true};
};

class SharedTexturePool final : public std::enable_shared_from_this<SharedTexturePool>
{
public:
    ~SharedTexturePool()
    {
        for (Buffer &buffer : m_buffers) {
            if (buffer.handle) {
                CloseHandle(buffer.handle);
            }
            if (buffer.fenceHandle) {
                CloseHandle(buffer.fenceHandle);
            }
        }
    }

    bool initialize(ID3D11Device *device, const QSize &size)
    {
        if (!device || size.isEmpty()) {
            return false;
        }
        m_size = size;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);
        if (!context
            || FAILED(device->QueryInterface(IID_PPV_ARGS(&m_device5)))
            || FAILED(context.As(&m_context4))) {
            return false;
        }

        static constexpr char vertexSource[] = R"(
struct VertexOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VertexOutput main(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VertexOutput output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
})";
        static constexpr char pixelSource[] = R"(
Texture2D<float4> sourceTexture : register(t0);
SamplerState linearSampler : register(s0);
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    return sourceTexture.Sample(linearSampler, uv);
})";
        ComPtr<ID3DBlob> vertexBytecode;
        ComPtr<ID3DBlob> pixelBytecode;
        if (FAILED(D3DCompile(vertexSource,
                              sizeof(vertexSource) - 1,
                              nullptr,
                              nullptr,
                              nullptr,
                              "main",
                              "vs_4_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3,
                              0,
                              &vertexBytecode,
                              nullptr))
            || FAILED(D3DCompile(pixelSource,
                                 sizeof(pixelSource) - 1,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 "main",
                                 "ps_4_0",
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                 0,
                                 &pixelBytecode,
                                 nullptr))
            || FAILED(device->CreateVertexShader(vertexBytecode->GetBufferPointer(),
                                                  vertexBytecode->GetBufferSize(),
                                                  nullptr,
                                                  &m_vertexShader))
            || FAILED(device->CreatePixelShader(pixelBytecode->GetBufferPointer(),
                                                 pixelBytecode->GetBufferSize(),
                                                 nullptr,
                                                 &m_pixelShader))) {
            return false;
        }

        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampler, &m_sampler))) {
            return false;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = UINT(size.width());
        description.Height = UINT(size.height());
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        description.MiscFlags = D3D11_RESOURCE_MISC_SHARED
            | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        for (Buffer &buffer : m_buffers) {
            if (FAILED(device->CreateTexture2D(&description, nullptr, &buffer.texture))
                || FAILED(device->CreateRenderTargetView(buffer.texture.Get(),
                                                          nullptr,
                                                          &buffer.renderTarget))) {
                return false;
            }
            ComPtr<IDXGIResource1> resource;
            if (FAILED(buffer.texture.As(&resource))
                || FAILED(resource->CreateSharedHandle(nullptr,
                                                        GENERIC_ALL,
                                                        nullptr,
                                                        &buffer.handle))
                || FAILED(m_device5->CreateFence(0,
                                                  D3D11_FENCE_FLAG_SHARED,
                                                  IID_PPV_ARGS(&buffer.fence)))
                || FAILED(buffer.fence->CreateSharedHandle(nullptr,
                                                            GENERIC_ALL,
                                                            nullptr,
                                                            &buffer.fenceHandle))) {
                return false;
            }
            buffer.sourceId = g_nextEnhancedTextureId.fetch_add(1,
                                                                 std::memory_order_relaxed);
        }
        return true;
    }

    std::shared_ptr<NativeEnhancedTabTexture> write(ID3D11DeviceContext *context,
                                                     ID3D11ShaderResourceView *source)
    {
        if (!context || !source) {
            return {};
        }
        int index = -1;
        for (int candidate = 0; candidate < int(m_buffers.size()); ++candidate) {
            int expected = Free;
            if (m_buffers[size_t(candidate)].state.compare_exchange_strong(expected,
                                                                            Writing)) {
                index = candidate;
                break;
            }
        }
        if (index < 0) {
            return {};
        }

        Buffer &buffer = m_buffers[size_t(index)];
        if (buffer.lastConsumerValue != 0
            && FAILED(m_context4->Wait(buffer.fence.Get(), buffer.lastConsumerValue))) {
            buffer.state.store(Free);
            return {};
        }

        ID3D11RenderTargetView *target = buffer.renderTarget.Get();
        ID3D11ShaderResourceView *view = source;
        ID3D11SamplerState *sampler = m_sampler.Get();
        D3D11_VIEWPORT viewport{};
        viewport.Width = float(m_size.width());
        viewport.Height = float(m_size.height());
        viewport.MaxDepth = 1.0f;
        context->OMSetRenderTargets(1, &target, nullptr);
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &view);
        context->PSSetSamplers(0, 1, &sampler);
        context->Draw(3, 0);
        ID3D11ShaderResourceView *noView = nullptr;
        context->PSSetShaderResources(0, 1, &noView);

        const quint64 producer = buffer.nextFenceValue++;
        const quint64 consumer = buffer.nextFenceValue++;
        if (FAILED(m_context4->Signal(buffer.fence.Get(), producer))) {
            buffer.state.store(Free);
            return {};
        }
        buffer.lastConsumerValue = consumer;
        buffer.state.store(Ready);
        return std::make_shared<SharedTextureLease>(shared_from_this(),
                                                    index,
                                                    buffer.sourceId,
                                                    buffer.handle,
                                                    buffer.fenceHandle,
                                                    producer,
                                                    consumer,
                                                    m_size);
    }

    void discard(int index)
    {
        if (index < 0 || index >= int(m_buffers.size())) {
            return;
        }
        Buffer &buffer = m_buffers[size_t(index)];
        int expected = Ready;
        if (buffer.state.compare_exchange_strong(expected, Releasing)) {
            buffer.lastConsumerValue = 0;
            buffer.state.store(Free);
        }
    }

    void markDisplayed(int index)
    {
        if (index < 0 || index >= int(m_buffers.size())) {
            return;
        }
        m_buffers[size_t(index)].state.store(Displayed);
        const int previous = m_displayed.exchange(index);
        if (previous >= 0 && previous != index) {
            m_buffers[size_t(previous)].state.store(Free);
        }
    }

private:
    enum State { Free = 0, Writing, Ready, Displayed, Releasing };
    struct Buffer {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> renderTarget;
        ComPtr<ID3D11Fence> fence;
        HANDLE handle = nullptr;
        HANDLE fenceHandle = nullptr;
        quint64 sourceId = 0;
        quint64 nextFenceValue = 1;
        quint64 lastConsumerValue = 0;
        std::atomic_int state{Free};
    };

    std::array<Buffer, 3> m_buffers;
    std::atomic_int m_displayed{-1};
    QSize m_size;
    ComPtr<ID3D11Device5> m_device5;
    ComPtr<ID3D11DeviceContext4> m_context4;
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11SamplerState> m_sampler;
};

SharedTextureLease::SharedTextureLease(std::shared_ptr<SharedTexturePool> pool,
                                       int index,
                                       quint64 sourceId,
                                       HANDLE textureHandle,
                                       HANDLE fenceHandle,
                                       quint64 producer,
                                       quint64 consumer,
                                       const QSize &size)
    : m_pool(std::move(pool))
    , m_index(index)
    , m_sourceId(sourceId)
    , m_textureHandle(textureHandle)
    , m_fenceHandle(fenceHandle)
    , m_producer(producer)
    , m_consumer(consumer)
    , m_size(size)
{
}

SharedTextureLease::~SharedTextureLease()
{
    if (m_ready.exchange(false) && m_pool) {
        m_pool->discard(m_index);
    }
}

void SharedTextureLease::markDisplayed()
{
    if (m_ready.exchange(false) && m_pool) {
        m_pool->markDisplayed(m_index);
    }
}

QSize scaledCaptureSize(const QSize &source)
{
    QSize output = source;
    output.scale(kMaximumTextureWidth, kMaximumTextureHeight, Qt::KeepAspectRatio);
    return QSize(std::max(1, output.width()), std::max(1, output.height()));
}

QSize restoredWindowSize(HWND window)
{
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(window, &placement)) {
        if ((placement.flags & WPF_RESTORETOMAXIMIZED) != 0) {
            const HMONITOR monitor = MonitorFromRect(&placement.rcNormalPosition,
                                                     MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (monitor && GetMonitorInfoW(monitor, &info)) {
                return QSize(info.rcMonitor.right - info.rcMonitor.left,
                             info.rcMonitor.bottom - info.rcMonitor.top);
            }
        }
        const int width = placement.rcNormalPosition.right
            - placement.rcNormalPosition.left;
        const int height = placement.rcNormalPosition.bottom
            - placement.rcNormalPosition.top;
        if (width > 0 && height > 0) {
            return QSize(width, height);
        }
    }
    RECT bounds{};
    if (GetWindowRect(window, &bounds)) {
        return QSize(std::max<LONG>(1, bounds.right - bounds.left),
                     std::max<LONG>(1, bounds.bottom - bounds.top));
    }
    return {};
}

class CaptureSession final
{
public:
    explicit CaptureSession(std::condition_variable &wake) : m_wake(wake) {}
    ~CaptureSession() { reset(); }

    bool initialize(HWND window,
                    const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice &device,
                    ID3D11Device *nativeDevice)
    {
        reset();
        try {
            m_originalPlacement = {};
            m_originalPlacement.length = sizeof(m_originalPlacement);
            m_hasOriginalPlacement = GetWindowPlacement(window,
                                                        &m_originalPlacement) != FALSE;
            m_wasMinimized = IsIconic(window);
            auto factory = winrt::get_activation_factory<GraphicsCaptureItem,
                                                         IGraphicsCaptureItemInterop>();
            winrt::check_hresult(factory->CreateForWindow(
                window,
                winrt::guid_of<GraphicsCaptureItem>(),
                winrt::put_abi(m_item)));
            const auto size = m_item.Size();
            if (size.Width <= 0 || size.Height <= 0) {
                return false;
            }
            m_sourceSize = m_wasMinimized ? restoredWindowSize(window)
                                          : QSize(size.Width, size.Height);
            if (m_sourceSize.isEmpty()) {
                m_sourceSize = QSize(size.Width, size.Height);
            }
            m_framePoolSize = m_sourceSize;
            m_winrtDevice = device;
            m_framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                device,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2,
                {m_framePoolSize.width(), m_framePoolSize.height()});
            m_session = m_framePool.CreateCaptureSession(m_item);
            m_session.IsCursorCaptureEnabled(false);
            try {
                m_session.IsBorderRequired(false);
            } catch (const winrt::hresult_error &) {
            }
            m_frameToken = m_framePool.FrameArrived([this](const auto &, const auto &) {
                m_pending.store(true, std::memory_order_release);
                m_wake.notify_one();
            });
            m_closedToken = m_item.Closed([this](const auto &, const auto &) {
                m_closed.store(true, std::memory_order_release);
                m_wake.notify_one();
            });
            m_window = window;
            m_pool = std::make_shared<SharedTexturePool>();
            if (!m_pool->initialize(nativeDevice, scaledCaptureSize(m_sourceSize))) {
                qWarning() << "Enhanced Tabs: shared texture pool initialization failed for"
                           << reinterpret_cast<quintptr>(window);
                reset();
                return false;
            }
            m_session.StartCapture();
            m_initializedAt = std::chrono::steady_clock::now();
            if (m_wasMinimized) {
                m_restoredForCapture = ShowWindowAsync(window,
                                                       SW_SHOWNOACTIVATE) != FALSE;
            }
            return true;
        } catch (const winrt::hresult_error &error) {
            qWarning() << "Enhanced Tabs: WGC session initialization failed for"
                       << reinterpret_cast<quintptr>(window)
                       << Qt::hex << quint32(error.code().value)
                       << QString::fromWCharArray(error.message().c_str());
            reset();
            return false;
        }
    }

    void reset()
    {
        restoreOriginalMinimizedState();
        try {
            if (m_framePool && m_frameToken.value) {
                m_framePool.FrameArrived(m_frameToken);
            }
            if (m_item && m_closedToken.value) {
                m_item.Closed(m_closedToken);
            }
            if (m_session) {
                m_session.Close();
            }
            if (m_framePool) {
                m_framePool.Close();
            }
        } catch (const winrt::hresult_error &) {
        }
        m_frameToken = {};
        m_closedToken = {};
        m_session = nullptr;
        m_framePool = nullptr;
        m_item = nullptr;
        m_winrtDevice = nullptr;
        m_pool.reset();
        m_sourceSize = {};
        m_framePoolSize = {};
        m_window = nullptr;
        m_pending.store(false, std::memory_order_release);
        m_closed.store(false, std::memory_order_release);
        m_hasDeliveredFrame = false;
        m_emptyFrameRetries = 0;
        m_staticFallbackAttempted = false;
        m_wasMinimized = false;
        m_restoredForCapture = false;
        m_hasOriginalPlacement = false;
        m_originalPlacement = {};
        m_initializedAt = {};
        m_lastDelivery = {};
    }

    bool valid() const
    {
        return m_window && IsWindow(m_window) && m_framePool
            && !m_closed.load(std::memory_order_acquire);
    }
    HWND window() const { return m_window; }

    std::shared_ptr<NativeEnhancedTabTexture> takeFrame(ID3D11Device *device,
                                                        ID3D11DeviceContext *context)
    {
        if (!valid()) {
            return {};
        }
        if (m_wasMinimized && m_hasDeliveredFrame) {
            return {};
        }
        const auto now = std::chrono::steady_clock::now();
        if (m_lastDelivery.time_since_epoch().count() != 0
            && now - m_lastDelivery < std::chrono::milliseconds(kFrameIntervalMs)) {
            return {};
        }
        const bool notified = m_pending.exchange(false, std::memory_order_acq_rel);
        // Poll until the first surface arrives as well as reacting to events.
        // FrameArrived may race TryGetNextFrame for a static or occluded window;
        // dropping that sole notification would otherwise leave the card on its
        // placeholder forever.
        if (!notified && m_hasDeliveredFrame) {
            return {};
        }
        try {
            Direct3D11CaptureFrame newest{nullptr};
            for (;;) {
                Direct3D11CaptureFrame frame = m_framePool.TryGetNextFrame();
                if (!frame) {
                    break;
                }
                if (newest) {
                    newest.Close();
                }
                newest = std::move(frame);
            }
            if (!newest) {
                if (auto fallback = takeMinimizedFallback(device, context, now)) {
                    m_hasDeliveredFrame = true;
                    m_lastDelivery = now;
                    return fallback;
                }
                if (notified && ++m_emptyFrameRetries < kMaximumEmptyFrameRetries) {
                    m_pending.store(true, std::memory_order_release);
                    m_wake.notify_one();
                } else if (notified) {
                    m_emptyFrameRetries = 0;
                }
                return {};
            }
            m_emptyFrameRetries = 0;
            const auto content = newest.ContentSize();
            if (content.Width <= 0 || content.Height <= 0) {
                newest.Close();
                return {};
            }
            const QSize nextSourceSize(content.Width, content.Height);
            if (m_wasMinimized
                && (nextSourceSize.height() <= 64 || nextSourceSize.width() <= 256)) {
                newest.Close();
                if (auto fallback = takeMinimizedFallback(device, context, now)) {
                    m_hasDeliveredFrame = true;
                    m_lastDelivery = now;
                    return fallback;
                }
                return {};
            }
            if (nextSourceSize != m_framePoolSize) {
                m_sourceSize = nextSourceSize;
                m_framePoolSize = nextSourceSize;
                newest.Close();
                m_framePool.Recreate(
                    m_winrtDevice,
                    DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    2,
                    {content.Width, content.Height});
                m_pool = std::make_shared<SharedTexturePool>();
                if (!m_pool->initialize(device, scaledCaptureSize(m_sourceSize))) {
                    return {};
                }
                return {};
            }
            auto access = newest.Surface().as<
                ::Windows::Graphics::DirectX::Direct3D11::
                    IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(access->GetInterface(IID_PPV_ARGS(&texture)))) {
                qWarning() << "Enhanced Tabs: could not access the WGC texture for"
                           << reinterpret_cast<quintptr>(m_window);
                newest.Close();
                return {};
            }
            ComPtr<ID3D11ShaderResourceView> view;
            if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
                qWarning() << "Enhanced Tabs: could not create the WGC texture view for"
                           << reinterpret_cast<quintptr>(m_window);
                newest.Close();
                return {};
            }
            auto output = m_pool ? m_pool->write(context, view.Get()) : nullptr;
            if (!output && !m_hasDeliveredFrame) {
                qWarning() << "Enhanced Tabs: could not publish the first shared texture for"
                           << reinterpret_cast<quintptr>(m_window);
            }
            newest.Close();
            if (output) {
                m_lastDelivery = now;
                m_hasDeliveredFrame = true;
                restoreOriginalMinimizedState();
            }
            return output;
        } catch (const winrt::hresult_error &) {
            return {};
        }
    }

private:
    std::shared_ptr<NativeEnhancedTabTexture> takeMinimizedFallback(
        ID3D11Device *device,
        ID3D11DeviceContext *context,
        std::chrono::steady_clock::time_point now)
    {
        if (m_staticFallbackAttempted || !m_wasMinimized
            || m_initializedAt.time_since_epoch().count() == 0
            || now - m_initializedAt
                < std::chrono::milliseconds(kMinimizedFallbackDelayMs)
            || !device || !context || !m_pool || m_sourceSize.isEmpty()) {
            return {};
        }
        m_staticFallbackAttempted = true;

        const int width = m_sourceSize.width();
        const int height = m_sourceSize.height();
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
            return {};
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = width;
        bitmapInfo.bmiHeader.biHeight = -height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void *pixels = nullptr;
        HDC memoryDc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = memoryDc
            ? CreateDIBSection(memoryDc,
                               &bitmapInfo,
                               DIB_RGB_COLORS,
                               &pixels,
                               nullptr,
                               0)
            : nullptr;
        if (!memoryDc || !bitmap || !pixels) {
            if (bitmap) {
                DeleteObject(bitmap);
            }
            if (memoryDc) {
                DeleteDC(memoryDc);
            }
            return {};
        }

        const HGDIOBJ previous = SelectObject(memoryDc, bitmap);
        PatBlt(memoryDc, 0, 0, width, height, BLACKNESS);
        constexpr UINT kPrintWindowRenderFullContent = 0x00000002;
        const BOOL printed = PrintWindow(m_window,
                                         memoryDc,
                                         kPrintWindowRenderFullContent);
        SelectObject(memoryDc, previous);

        std::shared_ptr<NativeEnhancedTabTexture> output;
        if (printed) {
            auto *bytes = static_cast<unsigned char *>(pixels);
            const size_t pixelCount = size_t(width) * size_t(height);
            for (size_t index = 0; index < pixelCount; ++index) {
                bytes[index * 4 + 3] = 0xff;
            }

            D3D11_TEXTURE2D_DESC description{};
            description.Width = UINT(width);
            description.Height = UINT(height);
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = pixels;
            initial.SysMemPitch = UINT(width * 4);
            ComPtr<ID3D11Texture2D> texture;
            ComPtr<ID3D11ShaderResourceView> view;
            if (SUCCEEDED(device->CreateTexture2D(&description,
                                                   &initial,
                                                   &texture))
                && SUCCEEDED(device->CreateShaderResourceView(texture.Get(),
                                                               nullptr,
                                                               &view))) {
                output = m_pool->write(context, view.Get());
            }
        }
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        restoreOriginalMinimizedState();
        return output;
    }

    void restoreOriginalMinimizedState()
    {
        if (m_wasMinimized && m_restoredForCapture && m_window
            && IsWindow(m_window)) {
            if (m_hasOriginalPlacement) {
                SetWindowPlacement(m_window, &m_originalPlacement);
            } else {
                ShowWindowAsync(m_window, SW_MINIMIZE);
            }
            m_restoredForCapture = false;
        }
    }

    std::condition_variable &m_wake;
    GraphicsCaptureItem m_item{nullptr};
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{nullptr};
    Direct3D11CaptureFramePool m_framePool{nullptr};
    GraphicsCaptureSession m_session{nullptr};
    winrt::event_token m_frameToken{};
    winrt::event_token m_closedToken{};
    std::shared_ptr<SharedTexturePool> m_pool;
    QSize m_sourceSize;
    QSize m_framePoolSize;
    HWND m_window = nullptr;
    std::atomic_bool m_pending{false};
    std::atomic_bool m_closed{false};
    bool m_hasDeliveredFrame = false;
    int m_emptyFrameRetries = 0;
    bool m_staticFallbackAttempted = false;
    bool m_wasMinimized = false;
    bool m_restoredForCapture = false;
    bool m_hasOriginalPlacement = false;
    WINDOWPLACEMENT m_originalPlacement{};
    std::chrono::steady_clock::time_point m_initializedAt;
    std::chrono::steady_clock::time_point m_lastDelivery;
};

#endif

} // namespace

class EnhancedTabCaptureWorker::Impl final
{
public:
    explicit Impl(FrameCallback callback) : m_callback(std::move(callback)) {}
    ~Impl() { stop(); }

    void start()
    {
        if (!m_thread.joinable()) {
            m_thread = std::jthread([this](std::stop_token token) { run(token); });
        }
    }

    void stop()
    {
        if (m_thread.joinable()) {
            m_thread.request_stop();
            m_wake.notify_all();
            m_thread.join();
        }
    }

    void setWindows(const QVector<quintptr> &windows)
    {
        QVector<quintptr> ordered = windows;
        std::sort(ordered.begin(), ordered.end());
        {
            std::scoped_lock lock(m_commandMutex);
            if (m_requestedWindows == ordered) {
                return;
            }
            m_requestedWindows = std::move(ordered);
            ++m_revision;
        }
        m_wake.notify_one();
    }

private:
    void run(std::stop_token token)
    {
#ifdef Q_OS_WIN
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error &error) {
            qWarning() << "Enhanced Tabs: WinRT apartment initialization failed"
                       << Qt::hex << quint32(error.code().value)
                       << QString::fromWCharArray(error.message().c_str());
            return;
        }

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                           D3D_FEATURE_LEVEL_11_0,
                                           D3D_FEATURE_LEVEL_10_1};
        D3D_FEATURE_LEVEL level{};
        if (FAILED(D3D11CreateDevice(nullptr,
                                     D3D_DRIVER_TYPE_HARDWARE,
                                     nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                     levels,
                                     UINT(std::size(levels)),
                                     D3D11_SDK_VERSION,
                                     &device,
                                     &level,
                                     &context))) {
            qWarning() << "Enhanced Tabs: D3D11 device creation failed";
            winrt::uninit_apartment();
            return;
        }
        ComPtr<IDXGIDevice> dxgiDevice;
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice{nullptr};
        if (FAILED(device.As(&dxgiDevice))
            || FAILED(CreateDirect3D11DeviceFromDXGIDevice(
                dxgiDevice.Get(),
                reinterpret_cast<IInspectable **>(winrt::put_abi(winrtDevice))))) {
            qWarning() << "Enhanced Tabs: WinRT D3D device creation failed";
            winrt::uninit_apartment();
            return;
        }

        std::vector<std::unique_ptr<CaptureSession>> sessions;
        quint64 appliedRevision = 0;
        while (!token.stop_requested()) {
            QVector<quintptr> requested;
            quint64 revision = 0;
            {
                std::unique_lock lock(m_commandMutex);
                if (appliedRevision == m_revision) {
                    m_wake.wait_for(lock, std::chrono::milliseconds(16));
                }
                requested = m_requestedWindows;
                revision = m_revision;
            }
            if (revision != appliedRevision) {
                sessions.clear();
                sessions.reserve(requested.size());
                for (quintptr handle : requested) {
                    auto session = std::make_unique<CaptureSession>(m_wake);
                    if (session->initialize(reinterpret_cast<HWND>(handle),
                                            winrtDevice,
                                            device.Get())) {
                        sessions.push_back(std::move(session));
                    }
                }
                appliedRevision = revision;
            }

            for (const auto &session : sessions) {
                if (!session->valid()) {
                    continue;
                }
                auto texture = session->takeFrame(device.Get(), context.Get());
                if (texture) {
                    NativeEnhancedTabFrame frame;
                    frame.windowKey = QString::number(
                        reinterpret_cast<quintptr>(session->window()));
                    frame.texture = std::move(texture);
                    m_callback(std::move(frame));
                }
            }
        }
        sessions.clear();
        winrt::uninit_apartment();
#else
        Q_UNUSED(token);
#endif
    }

    FrameCallback m_callback;
    std::mutex m_commandMutex;
    std::condition_variable m_wake;
    QVector<quintptr> m_requestedWindows;
    quint64 m_revision = 0;
    std::jthread m_thread;
};

EnhancedTabCaptureWorker::EnhancedTabCaptureWorker(FrameCallback callback)
    : m_impl(std::make_unique<Impl>(std::move(callback)))
{
}

EnhancedTabCaptureWorker::~EnhancedTabCaptureWorker() = default;

void EnhancedTabCaptureWorker::start()
{
    m_impl->start();
}

void EnhancedTabCaptureWorker::stop()
{
    m_impl->stop();
}

void EnhancedTabCaptureWorker::setWindows(const QVector<quintptr> &windows)
{
    m_impl->setWindows(windows);
}

bool EnhancedTabCaptureWorker::isSupported()
{
#ifdef Q_OS_WIN
    try {
        return GraphicsCaptureSession::IsSupported();
    } catch (const winrt::hresult_error &) {
        return false;
    }
#else
    return false;
#endif
}
