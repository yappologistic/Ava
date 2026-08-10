#include "liquidglasscaptureworker.h"

#include <QImage>
#include <QtGlobal>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/base.h>
#endif

namespace {

constexpr int kFrameWaitMs = 16;
constexpr int kMinimumDeliveryIntervalMs = 15;

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;
std::atomic<quint64> g_nextSharedSurfaceId{1};

struct CaptureGeometry
{
    QRect surface;
    QRect timer;
    int padding = 0;
    quintptr foregroundWindow = 0;
    NativeLiquidGlassOptics optics;
    quint64 revision = 0;
};

class SharedSurfacePool;

class SharedSurfaceLease final : public NativeLiquidGlassTextureLease
{
public:
    SharedSurfaceLease(std::shared_ptr<SharedSurfacePool> pool,
                       int index,
                       quint64 sourceId,
                       quintptr handle,
                       quintptr fenceHandle,
                       quint64 producerFenceValue,
                       quint64 consumerFenceValue,
                       QSize size);
    ~SharedSurfaceLease() override;

    quint64 sourceId() const override { return m_sourceId; }
    quintptr sharedHandle() const override { return m_handle; }
    quintptr sharedFenceHandle() const override { return m_fenceHandle; }
    quint64 producerFenceValue() const override { return m_producerFenceValue; }
    quint64 consumerFenceValue() const override { return m_consumerFenceValue; }
    QSize size() const override { return m_size; }
    void markDisplayed() override;

private:
    std::shared_ptr<SharedSurfacePool> m_pool;
    int m_index = -1;
    quint64 m_sourceId = 0;
    quintptr m_handle = 0;
    quintptr m_fenceHandle = 0;
    quint64 m_producerFenceValue = 0;
    quint64 m_consumerFenceValue = 0;
    QSize m_size;
    std::atomic_bool m_ready{true};
};

class SharedSurfacePool final : public std::enable_shared_from_this<SharedSurfacePool>
{
public:
    ~SharedSurfacePool();

    bool initialize(ID3D11Device *device, const QSize &size)
    {
        m_size = size;
        ComPtr<ID3D11DeviceContext> immediateContext;
        device->GetImmediateContext(&immediateContext);
        const HRESULT device5Result = device->QueryInterface(IID_PPV_ARGS(&m_device5));
        const HRESULT context4Result = immediateContext
            ? immediateContext.As(&m_context4) : E_POINTER;
        if (FAILED(device5Result) || FAILED(context4Result)) {
            return false;
        }
        static constexpr char vertexShaderSource[] = R"(
struct VertexOutput { float4 position : SV_POSITION; };
VertexOutput main(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VertexOutput output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
})";
        static constexpr char pixelShaderSource[] = R"(
Texture2D<float4> wallpaperTexture : register(t0);
Texture2D<float4> windowTexture : register(t1);
SamplerState linearSampler : register(s0);

cbuffer OpticalData : register(b0)
{
    float4 requestRect;    // screen origin, output size
    float4 wallpaperRect;  // screen origin, texture size
    float4 windowRect;     // screen origin, texture size
    float4 contentRect;    // output-local origin, glass size
    float4 shapeData;      // radius, side inset, ear depth, lens band
    float4 materialData;   // thickness, intensity, pill mode, reserved
    float4 interactionData; // pointer-local position, active, reserved
};

float roundedBoxDistance(float2 samplePoint, float2 halfSize, float radius)
{
    radius = clamp(radius, 0.5, min(halfSize.x, halfSize.y));
    float2 q = abs(samplePoint) - halfSize + radius;
    return -(min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius);
}

float glassDistance(float2 samplePoint)
{
    const float2 size = contentRect.zw;
    if (materialData.z > 0.5) {
        return roundedBoxDistance(samplePoint - size * 0.5,
                                  max(float2(1.0, 1.0), size * 0.5),
                                  shapeData.x);
    }

    const float radius = clamp(shapeData.x, 1.0, size.y * 0.5);
    const float earDepth = max(1.0, shapeData.z);
    const float earProgress = smoothstep(0.0, earDepth, samplePoint.y);
    const float inset = shapeData.y * earProgress;
    float distance = min(min(samplePoint.x - inset,
                             size.x - inset - samplePoint.x),
                         min(samplePoint.y, size.y - samplePoint.y));
    if (samplePoint.y > size.y - radius) {
        if (samplePoint.x < shapeData.y + radius) {
            distance = min(distance,
                           radius - length(samplePoint
                                           - float2(shapeData.y + radius,
                                                    size.y - radius)));
        } else if (samplePoint.x > size.x - shapeData.y - radius) {
            distance = min(distance,
                           radius - length(samplePoint
                                           - float2(size.x - shapeData.y - radius,
                                                    size.y - radius)));
        }
    }
    return distance;
}

float3 sampleBackdrop(float2 screenPosition)
{
    float2 wallpaperUv = (screenPosition - wallpaperRect.xy + 0.5)
                         / max(wallpaperRect.zw, float2(1.0, 1.0));
    float3 background = wallpaperTexture.SampleLevel(linearSampler,
                                                       saturate(wallpaperUv),
                                                       0.0).rgb;
    float2 windowLocal = screenPosition - windowRect.xy;
    if (windowLocal.x >= 0.0 && windowLocal.y >= 0.0
        && windowLocal.x < windowRect.z && windowLocal.y < windowRect.w) {
        float2 windowUv = (windowLocal + 0.5) / max(windowRect.zw,
                                                    float2(1.0, 1.0));
        float4 foreground = windowTexture.SampleLevel(linearSampler,
                                                       saturate(windowUv),
                                                       0.0);
        background = foreground.rgb + background * (1.0 - foreground.a);
    }
    return background;
}

float gaussian(float value, float center, float width)
{
    float x = (value - center) / max(width, 0.0001);
    return exp(-0.5 * x * x);
}

float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    const float2 local = position.xy - contentRect.xy;
    const float distance = glassDistance(local);
    const float alpha = smoothstep(-1.1, 1.1, distance);
    if (alpha <= 0.001) {
        return 0.0;
    }

    const float lensBand = clamp(shapeData.w,
                                 2.0,
                                 min(contentRect.z, contentRect.w) * 0.5);
    const float bandPosition = saturate(max(0.0, distance) / lensBand);
    const float edge = pow(saturate(1.0 - bandPosition), 0.92);

    const float gradientStep = 1.0;
    float2 inward = float2(glassDistance(local + float2(gradientStep, 0.0))
                           - glassDistance(local - float2(gradientStep, 0.0)),
                           glassDistance(local + float2(0.0, gradientStep))
                           - glassDistance(local - float2(0.0, gradientStep)));
    inward = normalize(inward + float2(0.00001, 0.00001));

    const float thickness = saturate(materialData.x);
    const float intensity = max(0.0, materialData.y);
    const float panel = smoothstep(0.62, 0.72, thickness);
    const float2 centered = local / max(contentRect.zw, float2(1.0, 1.0)) - 0.5;

    // A convex lens samples toward its optical center. The displacement grows
    // steeply through the meniscus, which visibly bends lines and moving UI
    // instead of merely blurring it.
    // Convex-squircle surface profile. Its derivative is steep at the outer
    // rim and falls smoothly to zero at the flat center, avoiding the harsh
    // secondary edge produced by a circular dome stretched into a capsule.
    const float surfaceX = saturate(bandPosition);
    const float surfaceInv = 1.0 - surfaceX;
    const float surfaceBase = max(0.0001, 1.0 - pow(surfaceInv, 4.0));
    const float surfaceSlope = pow(surfaceInv, 3.0)
                               / pow(surfaceBase, 0.75);
    const float sinIncident = surfaceSlope / sqrt(1.0 + surfaceSlope * surfaceSlope);
    const float sinRefracted = sinIncident / 1.5;
    const float refractedSlope = sinRefracted
                                 / sqrt(max(0.001,
                                            1.0 - sinRefracted * sinRefracted));
    const float edgeBend = refractedSlope * lensBand
                           * lerp(0.56, 0.70, thickness) * intensity;
    const float2 centerBend = -centered
                               * min(contentRect.z, contentRect.w)
                               * lerp(0.018, 0.016, panel)
                               * intensity;
    const float2 pointerDelta = local - interactionData.xy;
    const float pointerRadius = max(24.0,
                                    min(contentRect.z, contentRect.w) * 0.72);
    const float pointerEnergy = interactionData.z
                                * exp(-dot(pointerDelta, pointerDelta)
                                      / (pointerRadius * pointerRadius));
    const float2 pointerFlex = normalize(pointerDelta + float2(0.0001, 0.0001))
                               * pointerEnergy * edge * 1.8;
    const float2 samplePosition = requestRect.xy + position.xy
                                  + inward * edgeBend + centerBend + pointerFlex;

    const float scatter = lerp(0.45, 1.25, thickness);
    float3 clearColor = sampleBackdrop(samplePosition);
    float3 softened = clearColor * 0.52;
    softened += sampleBackdrop(samplePosition + float2(scatter, 0.0)) * 0.12;
    softened += sampleBackdrop(samplePosition - float2(scatter, 0.0)) * 0.12;
    softened += sampleBackdrop(samplePosition + float2(0.0, scatter)) * 0.12;
    softened += sampleBackdrop(samplePosition - float2(0.0, scatter)) * 0.12;
    const float detailEnergy = length(clearColor - softened);
    float3 color = lerp(clearColor, softened, lerp(0.025, 0.11, thickness));

    // Real glass splits wavelengths subtly at the steepest part of the lens.
    const float chromaticBand = pow(edge, 2.35);
    const float dispersion = lerp(0.42, 1.08, thickness)
                             * chromaticBand * intensity;
    const float3 redSample = sampleBackdrop(samplePosition + inward * dispersion);
    const float3 blueSample = sampleBackdrop(samplePosition - inward * dispersion);
    color.r = lerp(color.r, redSample.r, chromaticBand * 0.22);
    color.b = lerp(color.b, blueSample.b, chromaticBand * 0.22);

    // Keep the compact island transparent. Larger information surfaces get a
    // restrained adaptive neutral density so white labels remain legible.
    const float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    const float brightBackdrop = smoothstep(0.48, 0.88, luminance);
    const float busyBackdrop = smoothstep(0.035, 0.19, detailEnergy);
    const float compactVeil = brightBackdrop * 0.24;
    const float panelVeil = saturate(0.72
                                     + brightBackdrop * 0.14
                                     + busyBackdrop * 0.06);
    const float legibilityVeil = lerp(compactVeil, panelVeil, panel);
    color = lerp(color, float3(0.010, 0.014, 0.022), legibilityVeil);

    // Fresnel rim, broad upper reflection, inner shadow and lower caustic are
    // coupled to the same analytic lens profile; no decorative border layer is
    // needed, so there are no square edges or asynchronous overlay updates.
    const float2 outward = -inward;
    const float2 coolDirection = normalize(float2(-0.68, -0.74));
    const float2 warmDirection = normalize(float2(0.58, 0.82));
    const float3 surfaceNormal = normalize(float3(outward * surfaceSlope, 1.0));
    const float3 keyLight = normalize(float3(-0.52, -0.66, 0.54));
    const float physicalSpecular = pow(saturate(dot(surfaceNormal, keyLight)), 18.0);
    const float physicalFresnel = pow(saturate(1.0 - surfaceNormal.z), 2.4);
    const float directional = 0.20 + 0.80 * max(0.0, dot(outward, coolDirection));
    const float outerRim = pow(edge, 3.15);
    const float fresnel = pow(edge, 2.55) * directional;
    const float rimSheet = gaussian(bandPosition, 0.055, 0.075)
                           * (0.28 + 0.72 * directional);
    const float innerShadow = gaussian(bandPosition, 0.15, 0.095)
                              * (0.48 + 0.52
                                 * max(0.0, dot(outward,
                                                normalize(float2(0.86, -0.28)))));
    color *= 1.0 - innerShadow * lerp(0.06, 0.10, thickness);

    const float2 uv = local / max(contentRect.zw, float2(1.0, 1.0));
    const float reflectionLine = uv.y - (0.035 + 0.16 * uv.x);
    float upperReflection = exp(-reflectionLine * reflectionLine / 0.012)
                            * (1.0 - smoothstep(0.05, 0.78, uv.y));
    upperReflection *= 0.28 + 0.72 * edge;
    const float caustic = gaussian(bandPosition, 0.32, 0.10)
                          * (0.20 + 0.80 * max(0.0, dot(outward, warmDirection)));
    const float coolRim = outerRim * max(0.0, dot(outward, coolDirection));
    const float warmRim = (outerRim * 0.42 + caustic)
                          * max(0.0, dot(outward, warmDirection));

    color += float3(0.93, 0.97, 1.0)
             * (fresnel * 0.18 + physicalFresnel * 0.10
                + outerRim * 0.06 + rimSheet * 0.07
                + upperReflection * 0.05
                + physicalSpecular * 0.12)
             * intensity;
    color += float3(0.58, 0.82, 1.0) * coolRim * 0.035 * intensity;
    color += float3(1.0, 0.76, 0.43) * warmRim * 0.035 * intensity;
    color += float3(1.0, 0.93, 0.78) * caustic * 0.07 * intensity;
    color += float3(0.92, 0.97, 1.0)
             * pointerEnergy * (0.025 + edge * 0.07);
    color = saturate(color);
    return float4(color * alpha, alpha);
})";

        static std::once_flag shaderCompileOnce;
        static ComPtr<ID3DBlob> vertexBytecode;
        static ComPtr<ID3DBlob> pixelBytecode;
        std::call_once(shaderCompileOnce, []() {
            if (FAILED(D3DCompile(vertexShaderSource,
                                  sizeof(vertexShaderSource) - 1,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  "main",
                                  "vs_4_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                  0,
                                  &vertexBytecode,
                                  nullptr))) {
                return;
            }
            D3DCompile(pixelShaderSource,
                       sizeof(pixelShaderSource) - 1,
                       nullptr,
                       nullptr,
                       nullptr,
                       "main",
                       "ps_4_0",
                       D3DCOMPILE_OPTIMIZATION_LEVEL3,
                       0,
                       &pixelBytecode,
                       nullptr);
        });
        if (!vertexBytecode || !pixelBytecode) {
            return false;
        }
        if (FAILED(device->CreateVertexShader(vertexBytecode->GetBufferPointer(),
                                                  vertexBytecode->GetBufferSize(),
                                                  nullptr,
                                                  &m_vertexShader))
            || FAILED(device->CreatePixelShader(pixelBytecode->GetBufferPointer(),
                                                 pixelBytecode->GetBufferSize(),
                                                 nullptr,
                                                 &m_pixelShader))) {
            return false;
        }

        D3D11_BUFFER_DESC constantDescription{};
        constantDescription.ByteWidth = 112;
        constantDescription.Usage = D3D11_USAGE_DYNAMIC;
        constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        for (ComPtr<ID3D11Buffer> &constantBuffer : m_opticalConstants) {
            if (FAILED(device->CreateBuffer(&constantDescription,
                                            nullptr,
                                            &constantBuffer))) {
                return false;
            }
        }

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDescription.MinLOD = 0.0f;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&samplerDescription, &m_sampler))) {
            return false;
        }

        D3D11_RASTERIZER_DESC rasterizerDescription{};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.DepthClipEnable = TRUE;
        rasterizerDescription.ScissorEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&rasterizerDescription,
                                                  &m_rasterizer))) {
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
            const HRESULT textureResult = device->CreateTexture2D(&description,
                                                                   nullptr,
                                                                   &buffer.texture);
            const HRESULT renderTargetResult = SUCCEEDED(textureResult)
                ? device->CreateRenderTargetView(buffer.texture.Get(),
                                                 nullptr,
                                                 &buffer.renderTarget)
                : textureResult;
            if (FAILED(textureResult) || FAILED(renderTargetResult)) {
                return false;
            }
            ComPtr<IDXGIResource1> resource;
            HANDLE handle = nullptr;
            const HRESULT resourceResult = buffer.texture.As(&resource);
            const HRESULT handleResult = SUCCEEDED(resourceResult)
                ? resource->CreateSharedHandle(
                      nullptr,
                      GENERIC_ALL,
                      nullptr,
                      &handle)
                : resourceResult;
            if (FAILED(resourceResult) || FAILED(handleResult) || !handle) {
                return false;
            }
            buffer.handle = handle;
            const HRESULT fenceResult = m_device5->CreateFence(
                0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&buffer.fence));
            const HRESULT fenceHandleResult = SUCCEEDED(fenceResult)
                ? buffer.fence->CreateSharedHandle(nullptr,
                                                   GENERIC_ALL,
                                                   nullptr,
                                                   &buffer.fenceHandle)
                : fenceResult;
            if (FAILED(fenceResult) || FAILED(fenceHandleResult)
                || !buffer.fenceHandle) {
                return false;
            }
            buffer.sourceId = g_nextSharedSurfaceId.fetch_add(1,
                                                               std::memory_order_relaxed);
        }
        return true;
    }

    std::shared_ptr<NativeLiquidGlassTextureLease> write(
        ID3D11DeviceContext *context,
        ID3D11ShaderResourceView *backgroundView,
        const QRect &backgroundBounds,
        ID3D11ShaderResourceView *desktopView,
        const QRect &requestedRect,
        const QRect &outputRect,
        const NativeLiquidGlassOptics &optics)
    {
        int bufferIndex = -1;
        for (int index = 0; index < int(m_buffers.size()); ++index) {
            int expected = Free;
            if (m_buffers[index].state.compare_exchange_strong(expected, Writing)) {
                bufferIndex = index;
                break;
            }
        }
        if (bufferIndex < 0) {
            return {};
        }

        Buffer &buffer = m_buffers[size_t(bufferIndex)];
        if (buffer.lastConsumerFenceValue != 0
            && FAILED(m_context4->Wait(buffer.fence.Get(),
                                       buffer.lastConsumerFenceValue))) {
            buffer.state.store(Free);
            return {};
        }

        ID3D11RenderTargetView *renderTarget = buffer.renderTarget.Get();
        ID3D11Buffer *constantBuffer = m_opticalConstants[size_t(bufferIndex)].Get();
        D3D11_VIEWPORT viewport{};
        viewport.Width = FLOAT(m_size.width());
        viewport.Height = FLOAT(m_size.height());
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->OMSetRenderTargets(1, &renderTarget, nullptr);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context->RSSetViewports(1, &viewport);
        context->RSSetState(m_rasterizer.Get());
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &constantBuffer);

        struct alignas(16) OpticalConstants {
            float requestRect[4];
            float wallpaperRect[4];
            float windowRect[4];
            float contentRect[4];
            float shapeData[4];
            float materialData[4];
            float interactionData[4];
        } constants{
            {float(requestedRect.x()), float(requestedRect.y()),
             float(requestedRect.width()), float(requestedRect.height())},
            {float(backgroundBounds.x()), float(backgroundBounds.y()),
             float(backgroundBounds.width()), float(backgroundBounds.height())},
            {float(outputRect.x()), float(outputRect.y()),
             float(outputRect.width()), float(outputRect.height())},
            {float(optics.contentRect.x() - requestedRect.x()),
             float(optics.contentRect.y() - requestedRect.y()),
             float(optics.contentRect.width()), float(optics.contentRect.height())},
            {optics.radius, optics.sideInset, optics.earDepth, optics.lensBand},
            {optics.thickness, optics.intensity, optics.pill ? 1.0f : 0.0f, 0.0f},
            {float(optics.pointer.x()), float(optics.pointer.y()),
             optics.pointerActive ? 1.0f : 0.0f, 0.0f}
        };
        D3D11_MAPPED_SUBRESOURCE mappedConstants{};
        if (FAILED(context->Map(constantBuffer,
                                0,
                                D3D11_MAP_WRITE_DISCARD,
                                0,
                                &mappedConstants))) {
            buffer.state.store(Free);
            return {};
        }
        std::memcpy(mappedConstants.pData, &constants, sizeof(constants));
        context->Unmap(constantBuffer, 0);
        const QRect contentLocal = optics.contentRect.translated(-requestedRect.topLeft())
            .intersected(QRect(QPoint(0, 0), m_size));
        const D3D11_RECT scissor{
            LONG(contentLocal.left()),
            LONG(contentLocal.top()),
            LONG(contentLocal.right() + 1),
            LONG(contentLocal.bottom() + 1)};
        context->RSSetScissorRects(1, &scissor);
        ID3D11ShaderResourceView *views[2] = {backgroundView, desktopView};
        ID3D11SamplerState *samplers[1] = {m_sampler.Get()};
        context->PSSetShaderResources(0, 2, views);
        context->PSSetSamplers(0, 1, samplers);
        context->Draw(3, 0);
        ID3D11ShaderResourceView *noViews[2] = {nullptr, nullptr};
        context->PSSetShaderResources(0, 2, noViews);
        const quint64 producerFenceValue = buffer.nextFenceValue++;
        const quint64 consumerFenceValue = buffer.nextFenceValue++;
        if (FAILED(m_context4->Signal(buffer.fence.Get(), producerFenceValue))) {
            buffer.state.store(Free);
            return {};
        }
        buffer.lastConsumerFenceValue = consumerFenceValue;
        buffer.state.store(Ready);
        return std::make_shared<SharedSurfaceLease>(shared_from_this(),
                                                    bufferIndex,
                                                    buffer.sourceId,
                                                    reinterpret_cast<quintptr>(buffer.handle),
                                                    reinterpret_cast<quintptr>(buffer.fenceHandle),
                                                    producerFenceValue,
                                                    consumerFenceValue,
                                                    m_size);
    }

    void discard(int index)
    {
        if (index < 0 || index >= int(m_buffers.size())) {
            return;
        }
        Buffer &buffer = m_buffers[size_t(index)];
        int expected = Ready;
        if (!buffer.state.compare_exchange_strong(expected, Releasing)) {
            return;
        }
        // No consumer submitted work for this lease. Reuse is safe because all
        // producer commands stay ordered on the dedicated immediate context.
        buffer.lastConsumerFenceValue = 0;
        buffer.state.store(Free);
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
    enum State {
        Free = 0,
        Writing,
        Ready,
        Displayed,
        Releasing
    };

    struct Buffer
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> renderTarget;
        ComPtr<ID3D11Fence> fence;
        HANDLE handle = nullptr;
        HANDLE fenceHandle = nullptr;
        quint64 sourceId = 0;
        quint64 nextFenceValue = 1;
        quint64 lastConsumerFenceValue = 0;
        std::atomic_int state{Free};
    };

    std::array<Buffer, 3> m_buffers;
    std::atomic_int m_displayed{-1};
    QSize m_size;
    ComPtr<ID3D11Device5> m_device5;
    ComPtr<ID3D11DeviceContext4> m_context4;
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    std::array<ComPtr<ID3D11Buffer>, 3> m_opticalConstants;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11RasterizerState> m_rasterizer;
};

SharedSurfacePool::~SharedSurfacePool()
{
    for (Buffer &buffer : m_buffers) {
        if (buffer.handle) {
            CloseHandle(buffer.handle);
            buffer.handle = nullptr;
        }
        if (buffer.fenceHandle) {
            CloseHandle(buffer.fenceHandle);
            buffer.fenceHandle = nullptr;
        }
    }
}

SharedSurfaceLease::SharedSurfaceLease(std::shared_ptr<SharedSurfacePool> pool,
                                       int index,
                                       quint64 sourceId,
                                       quintptr handle,
                                       quintptr fenceHandle,
                                       quint64 producerFenceValue,
                                       quint64 consumerFenceValue,
                                       QSize size)
    : m_pool(std::move(pool))
    , m_index(index)
    , m_sourceId(sourceId)
    , m_handle(handle)
    , m_fenceHandle(fenceHandle)
    , m_producerFenceValue(producerFenceValue)
    , m_consumerFenceValue(consumerFenceValue)
    , m_size(size)
{
}

SharedSurfaceLease::~SharedSurfaceLease()
{
    if (m_ready.exchange(false) && m_pool) {
        m_pool->discard(m_index);
    }
}

void SharedSurfaceLease::markDisplayed()
{
    if (m_ready.exchange(false) && m_pool) {
        m_pool->markDisplayed(m_index);
    }
}

static QRect windowBounds(HWND window)
{
    RECT bounds{};
    if (SUCCEEDED(DwmGetWindowAttribute(window,
                                        DWMWA_EXTENDED_FRAME_BOUNDS,
                                        &bounds,
                                        sizeof(bounds)))) {
        return QRect(bounds.left,
                     bounds.top,
                     bounds.right - bounds.left,
                     bounds.bottom - bounds.top);
    }
    if (GetWindowRect(window, &bounds)) {
        return QRect(bounds.left,
                     bounds.top,
                     bounds.right - bounds.left,
                     bounds.bottom - bounds.top);
    }
    return {};
}

static bool isCaptureCandidate(HWND window, DWORD ownProcessId)
{
    if (!window || !IsWindowVisible(window) || IsIconic(window)) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == ownProcessId) {
        return false;
    }
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(window,
                                        DWMWA_CLOAKED,
                                        &cloaked,
                                        sizeof(cloaked)))
        && cloaked) {
        return false;
    }
    return !windowBounds(window).isEmpty();
}

static HWND findUnderlyingWindow(HWND foregroundWindow, const QRect &captureRect)
{
    if (!foregroundWindow || captureRect.isEmpty()) {
        return nullptr;
    }
    DWORD ownProcessId = 0;
    GetWindowThreadProcessId(foregroundWindow, &ownProcessId);
    HWND firstIntersecting = nullptr;
    const QPoint center = captureRect.center();
    for (HWND candidate = GetWindow(foregroundWindow, GW_HWNDNEXT);
         candidate;
         candidate = GetWindow(candidate, GW_HWNDNEXT)) {
        if (!isCaptureCandidate(candidate, ownProcessId)) {
            continue;
        }
        const QRect bounds = windowBounds(candidate);
        if (!bounds.intersects(captureRect)) {
            continue;
        }
        if (bounds.contains(center)) {
            return candidate;
        }
        if (!firstIntersecting) {
            firstIntersecting = candidate;
        }
    }
    return firstIntersecting;
}

static QRect captureBoundsFor(HWND window, const QSize &contentSize)
{
    if (!window || contentSize.isEmpty()) {
        return {};
    }

    std::array<QRect, 3> candidates;
    size_t candidateCount = 0;
    const QRect extended = windowBounds(window);
    if (!extended.isEmpty()) {
        candidates[candidateCount++] = extended;
    }

    RECT rawWindow{};
    if (GetWindowRect(window, &rawWindow)) {
        candidates[candidateCount++] = QRect(rawWindow.left,
                                             rawWindow.top,
                                             rawWindow.right - rawWindow.left,
                                             rawWindow.bottom - rawWindow.top);
    }

    RECT client{};
    POINT clientOrigin{};
    if (GetClientRect(window, &client) && ClientToScreen(window, &clientOrigin)) {
        candidates[candidateCount++] = QRect(clientOrigin.x,
                                             clientOrigin.y,
                                             client.right - client.left,
                                             client.bottom - client.top);
    }

    if (candidateCount == 0) {
        return {};
    }
    const auto score = [&contentSize](const QRect &candidate) {
        return std::abs(candidate.width() - contentSize.width())
            + std::abs(candidate.height() - contentSize.height());
    };
    const QRect best = *std::min_element(candidates.begin(),
                                         candidates.begin() + candidateCount,
                                         [&score](const QRect &left, const QRect &right) {
                                             return score(left) < score(right);
                                         });
    return QRect(best.topLeft(), contentSize);
}

class WindowCaptureSession
{
public:
    ~WindowCaptureSession() { reset(); }

    bool initialize(HWND targetWindow, const QPoint &captureCenter)
    {
        reset();
        if (!targetWindow
            || !winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
            return false;
        }

        try {
            if (!createDevice(captureCenter)) {
                return false;
            }
            createWallpaperView(captureCenter);

            auto interopFactory = winrt::get_activation_factory<
                winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                IGraphicsCaptureItemInterop>();
            winrt::check_hresult(interopFactory->CreateForWindow(
                targetWindow,
                winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
                winrt::put_abi(m_item)));

            const auto size = m_item.Size();
            if (size.Width <= 0 || size.Height <= 0) {
                reset();
                return false;
            }
            m_framePool = winrt::Windows::Graphics::Capture::
                Direct3D11CaptureFramePool::CreateFreeThreaded(
                    m_winrtDevice,
                    winrt::Windows::Graphics::DirectX::
                        DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    3,
                    size);
            m_session = m_framePool.CreateCaptureSession(m_item);
            m_session.IsCursorCaptureEnabled(false);
            // Windows may keep its capture indicator when borderless access was
            // not granted. Setting this still removes it on systems where the
            // user has already approved borderless graphics capture.
            try {
                m_session.IsBorderRequired(false);
            } catch (const winrt::hresult_error &) {
            }

            m_frameArrivedToken = m_framePool.FrameArrived(
                [this](const auto &, const auto &) {
                    m_framePending.store(true, std::memory_order_release);
                    m_frameCondition.notify_one();
                });
            m_closedToken = m_item.Closed(
                [this](const auto &, const auto &) {
                    m_closed.store(true, std::memory_order_release);
                    m_frameCondition.notify_one();
                });
            m_targetWindow = targetWindow;
            m_lastSize = QSize(size.Width, size.Height);
            m_session.StartCapture();
            return true;
        } catch (const winrt::hresult_error &) {
            reset();
            return false;
        }
    }

    void reset()
    {
        try {
            if (m_latestFrame) {
                m_latestFrame.Close();
            }
            if (m_framePool && m_frameArrivedToken.value) {
                m_framePool.FrameArrived(m_frameArrivedToken);
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
        m_frameArrivedToken = {};
        m_closedToken = {};
        m_session = nullptr;
        m_framePool = nullptr;
        m_item = nullptr;
        m_winrtDevice = nullptr;
        m_latestFrame = nullptr;
        m_latestCaptureView.Reset();
        m_latestSourceBounds = {};
        m_wallpaperView.Reset();
        m_wallpaperTexture.Reset();
        m_wallpaperBounds = {};
        for (CaptureViewEntry &entry : m_captureViews) {
            entry = {};
        }
        m_surfacePool.reset();
        m_timerPool.reset();
        m_context.Reset();
        m_device.Reset();
        m_surfacePoolSize = {};
        m_timerPoolSize = {};
        m_lastSize = {};
        m_targetWindow = nullptr;
        m_framePending.store(false, std::memory_order_release);
        m_closed.store(false, std::memory_order_release);
    }

    bool isValid() const
    {
        return m_targetWindow && m_session && m_framePool
            && m_device && m_context && !m_closed.load(std::memory_order_acquire);
    }

    HWND targetWindow() const { return m_targetWindow; }

    bool hasLatestFrame() const
    {
        return bool(m_latestFrame) && m_latestCaptureView
            && !m_latestSourceBounds.isEmpty();
    }

    bool waitForFrame(std::stop_token token)
    {
        std::unique_lock lock(m_frameMutex);
        m_frameCondition.wait_for(lock,
                                  std::chrono::milliseconds(kFrameWaitMs),
                                  [this, &token]() {
                                      return token.stop_requested()
                                          || m_framePending.load(std::memory_order_acquire)
                                          || m_closed.load(std::memory_order_acquire);
                                  });
        return !token.stop_requested()
            && m_framePending.exchange(false, std::memory_order_acq_rel);
    }

    NativeLiquidGlassFrame read(const CaptureGeometry &geometry)
    {
        NativeLiquidGlassFrame output;
        if (!isValid()) {
            return output;
        }

        try {
            auto frame = m_framePool.TryGetNextFrame();
            if (!frame) {
                return output;
            }
            while (auto newer = m_framePool.TryGetNextFrame()) {
                frame.Close();
                frame = std::move(newer);
            }

            const auto frameSize = frame.ContentSize();
            const QSize contentSize(frameSize.Width, frameSize.Height);
            auto access = frame.Surface().as<
                ::Windows::Graphics::DirectX::Direct3D11::
                    IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> sourceTexture;
            winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&sourceTexture)));
            ID3D11ShaderResourceView *sourceView = sourceViewFor(sourceTexture.Get());
            if (!sourceView) {
                frame.Close();
                return output;
            }

            const QRect sourceBounds = captureBoundsFor(m_targetWindow, contentSize);
            if (sourceBounds.isEmpty()) {
                frame.Close();
                return output;
            }
            const bool sizeChanged = contentSize != m_lastSize;
            if (m_latestFrame) {
                m_latestFrame.Close();
            }
            m_latestFrame = std::move(frame);
            m_latestCaptureView = sourceView;
            m_latestSourceBounds = sourceBounds;
            output = renderLatest(geometry);
            if (sizeChanged && contentSize.width() > 0 && contentSize.height() > 0) {
                m_latestFrame.Close();
                m_latestFrame = nullptr;
                m_latestCaptureView.Reset();
                m_latestSourceBounds = {};
                for (CaptureViewEntry &entry : m_captureViews) {
                    entry = {};
                }
                const winrt::Windows::Graphics::SizeInt32 size{
                    contentSize.width(), contentSize.height()};
                m_framePool.Recreate(
                    m_winrtDevice,
                    winrt::Windows::Graphics::DirectX::
                        DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    3,
                    size);
                m_lastSize = contentSize;
            }
        } catch (const winrt::hresult_error &) {
            m_closed.store(true, std::memory_order_release);
        }
        return output;
    }

    NativeLiquidGlassFrame renderLatest(const CaptureGeometry &geometry)
    {
        NativeLiquidGlassFrame output;
        if (!isValid() || !hasLatestFrame() || geometry.surface.isEmpty()) {
            return output;
        }

        if (!m_surfacePool || m_surfacePoolSize != geometry.surface.size()) {
            auto pool = std::make_shared<SharedSurfacePool>();
            if (!pool->initialize(m_device.Get(), geometry.surface.size())) {
                return output;
            }
            m_surfacePool = std::move(pool);
            m_surfacePoolSize = geometry.surface.size();
        }

        output.surfaceTexture = m_surfacePool->write(m_context.Get(),
                                                     m_wallpaperView.Get(),
                                                     m_wallpaperBounds,
                                                     m_latestCaptureView.Get(),
                                                     geometry.surface,
                                                     m_latestSourceBounds,
                                                     geometry.optics);
        output.surfacePadding = geometry.padding;
        output.surfaceContentRect = geometry.optics.contentRect.translated(
            -geometry.surface.topLeft());

        if (!geometry.timer.isEmpty()) {
            if (!m_timerPool || m_timerPoolSize != geometry.timer.size()) {
                auto pool = std::make_shared<SharedSurfacePool>();
                if (pool->initialize(m_device.Get(), geometry.timer.size())) {
                    m_timerPool = std::move(pool);
                    m_timerPoolSize = geometry.timer.size();
                }
            }
            if (m_timerPool) {
                NativeLiquidGlassOptics timerOptics;
                timerOptics.contentRect = geometry.timer.adjusted(
                    geometry.padding,
                    geometry.padding,
                    -geometry.padding,
                    -geometry.padding);
                const float diameter = float(timerOptics.contentRect.width());
                timerOptics.radius = diameter * 0.5f;
                timerOptics.lensBand = qMax(6.0f, diameter * 0.26f);
                timerOptics.thickness = 0.62f;
                timerOptics.intensity = 0.92f;
                timerOptics.pill = true;
                output.timerTexture = m_timerPool->write(m_context.Get(),
                                                          m_wallpaperView.Get(),
                                                          m_wallpaperBounds,
                                                          m_latestCaptureView.Get(),
                                                          geometry.timer,
                                                          m_latestSourceBounds,
                                                          timerOptics);
                output.timerPadding = geometry.padding;
                output.timerContentRect = timerOptics.contentRect.translated(
                    -geometry.timer.topLeft());
            }
        } else {
            m_timerPool.reset();
            m_timerPoolSize = {};
        }
        return output;
    }

private:
    struct CaptureViewEntry {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
    };

    ID3D11ShaderResourceView *sourceViewFor(ID3D11Texture2D *texture)
    {
        CaptureViewEntry *available = nullptr;
        for (CaptureViewEntry &entry : m_captureViews) {
            if (entry.texture.Get() == texture) {
                return entry.view.Get();
            }
            if (!available && !entry.texture) {
                available = &entry;
            }
        }
        if (!available) {
            for (CaptureViewEntry &entry : m_captureViews) {
                entry = {};
            }
            available = &m_captureViews.front();
        }
        available->texture = texture;
        if (FAILED(m_device->CreateShaderResourceView(texture,
                                                       nullptr,
                                                       &available->view))) {
            *available = {};
            return nullptr;
        }
        return available->view.Get();
    }

    bool createWallpaperView(const QPoint &captureCenter)
    {
        const POINT point{captureCenter.x(), captureCenter.y()};
        const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!monitor || !GetMonitorInfoW(monitor, &info)) {
            return false;
        }

        wchar_t wallpaperPath[32768]{};
        if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER,
                                   DWORD(std::size(wallpaperPath)),
                                   wallpaperPath,
                                   0)
            || wallpaperPath[0] == L'\0') {
            return false;
        }

        QImage wallpaper(QString::fromWCharArray(wallpaperPath));
        const QSize monitorSize(info.rcMonitor.right - info.rcMonitor.left,
                                info.rcMonitor.bottom - info.rcMonitor.top);
        if (wallpaper.isNull() || monitorSize.isEmpty()) {
            return false;
        }
        wallpaper = wallpaper.scaled(monitorSize,
                                     Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation);
        const int cropX = qMax(0, (wallpaper.width() - monitorSize.width()) / 2);
        const int cropY = qMax(0, (wallpaper.height() - monitorSize.height()) / 2);
        wallpaper = wallpaper.copy(cropX,
                                   cropY,
                                   monitorSize.width(),
                                   monitorSize.height())
                        .convertToFormat(QImage::Format_ARGB32);

        D3D11_TEXTURE2D_DESC description{};
        description.Width = UINT(wallpaper.width());
        description.Height = UINT(wallpaper.height());
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initialData{};
        initialData.pSysMem = wallpaper.constBits();
        initialData.SysMemPitch = UINT(wallpaper.bytesPerLine());
        if (FAILED(m_device->CreateTexture2D(&description,
                                             &initialData,
                                             &m_wallpaperTexture))
            || FAILED(m_device->CreateShaderResourceView(m_wallpaperTexture.Get(),
                                                          nullptr,
                                                          &m_wallpaperView))) {
            m_wallpaperTexture.Reset();
            m_wallpaperView.Reset();
            return false;
        }
        m_wallpaperBounds = QRect(info.rcMonitor.left,
                                  info.rcMonitor.top,
                                  monitorSize.width(),
                                  monitorSize.height());
        return true;
    }

    bool createDevice(const QPoint &captureCenter)
    {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return false;
        }

        ComPtr<IDXGIAdapter1> selectedAdapter;
        for (UINT adapterIndex = 0; !selectedAdapter; ++adapterIndex) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            for (UINT outputIndex = 0; ; ++outputIndex) {
                ComPtr<IDXGIOutput> output;
                if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                DXGI_OUTPUT_DESC description{};
                if (FAILED(output->GetDesc(&description)) || !description.AttachedToDesktop) {
                    continue;
                }
                const RECT &bounds = description.DesktopCoordinates;
                if (captureCenter.x() >= bounds.left && captureCenter.x() < bounds.right
                    && captureCenter.y() >= bounds.top && captureCenter.y() < bounds.bottom) {
                    selectedAdapter = adapter;
                    break;
                }
            }
        }
        if (!selectedAdapter) {
            return false;
        }

        constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL createdLevel{};
        HRESULT result = D3D11CreateDevice(selectedAdapter.Get(),
                                           D3D_DRIVER_TYPE_UNKNOWN,
                                           nullptr,
                                           flags,
                                           levels,
                                           UINT(std::size(levels)),
                                           D3D11_SDK_VERSION,
                                           &m_device,
                                           &createdLevel,
                                           &m_context);
        if (result == E_INVALIDARG) {
            result = D3D11CreateDevice(selectedAdapter.Get(),
                                       D3D_DRIVER_TYPE_UNKNOWN,
                                       nullptr,
                                       flags,
                                       levels + 1,
                                       UINT(std::size(levels) - 1),
                                       D3D11_SDK_VERSION,
                                       &m_device,
                                       &createdLevel,
                                       &m_context);
        }
        if (FAILED(result)) {
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(m_device.As(&dxgiDevice))) {
            return false;
        }
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.Get(),
            reinterpret_cast<IInspectable **>(winrt::put_abi(m_winrtDevice))));
        return true;
    }

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Texture2D> m_wallpaperTexture;
    ComPtr<ID3D11ShaderResourceView> m_wallpaperView;
    ComPtr<ID3D11ShaderResourceView> m_latestCaptureView;
    std::array<CaptureViewEntry, 4> m_captureViews;
    std::shared_ptr<SharedSurfacePool> m_surfacePool;
    std::shared_ptr<SharedSurfacePool> m_timerPool;
    QRect m_wallpaperBounds;
    QRect m_latestSourceBounds;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame m_latestFrame{nullptr};
    winrt::event_token m_frameArrivedToken{};
    winrt::event_token m_closedToken{};
    std::condition_variable m_frameCondition;
    std::mutex m_frameMutex;
    std::atomic_bool m_framePending{false};
    std::atomic_bool m_closed{false};
    QSize m_surfacePoolSize;
    QSize m_timerPoolSize;
    QSize m_lastSize;
    HWND m_targetWindow = nullptr;
};
#endif

} // namespace

class LiquidGlassCaptureWorker::Impl
{
public:
    explicit Impl(FrameCallback callback)
        : m_callback(std::move(callback))
    {
    }

    ~Impl() { stop(); }

    void start()
    {
        if (m_thread.joinable()) {
            return;
        }
        m_thread = std::jthread([this](std::stop_token token) { run(token); });
    }

    void stop()
    {
        if (!m_thread.joinable()) {
            return;
        }
        m_thread.request_stop();
        m_thread.join();
    }

    void setGeometry(const QRect &surfaceCapture,
                     const QRect &timerCapture,
                     int padding,
                     quintptr foregroundWindow,
                     const NativeLiquidGlassOptics &optics)
    {
#ifdef Q_OS_WIN
        CaptureGeometry next;
        next.surface = surfaceCapture;
        next.timer = timerCapture;
        next.padding = padding;
        next.foregroundWindow = foregroundWindow;
        next.optics = optics;
        if (m_hasSubmittedGeometry && geometryMatches(m_lastSubmittedGeometry, next)) {
            return;
        }
        next.revision = ++m_geometryRevision;
        m_lastSubmittedGeometry = next;
        m_hasSubmittedGeometry = true;

        GeometrySlot *target = nullptr;
        for (GeometrySlot &slot : m_geometrySlots) {
            int expected = GeometryFree;
            if (slot.state.compare_exchange_strong(expected,
                                                   GeometryWriting,
                                                   std::memory_order_acq_rel)) {
                target = &slot;
                break;
            }
        }
        if (!target) {
            GeometrySlot *oldest = nullptr;
            for (GeometrySlot &slot : m_geometrySlots) {
                if (slot.state.load(std::memory_order_acquire) == GeometryReady
                    && (!oldest || slot.geometry.revision < oldest->geometry.revision)) {
                    oldest = &slot;
                }
            }
            if (oldest) {
                int expected = GeometryReady;
                if (oldest->state.compare_exchange_strong(expected,
                                                          GeometryWriting,
                                                          std::memory_order_acq_rel)) {
                    target = oldest;
                }
            }
        }
        if (!target) {
            return;
        }
        target->geometry = next;
        target->state.store(GeometryReady, std::memory_order_release);
#else
        Q_UNUSED(surfaceCapture);
        Q_UNUSED(timerCapture);
        Q_UNUSED(padding);
        Q_UNUSED(foregroundWindow);
        Q_UNUSED(optics);
#endif
    }

private:
#ifdef Q_OS_WIN
    enum GeometrySlotState {
        GeometryFree = 0,
        GeometryWriting,
        GeometryReady,
        GeometryReading
    };

    struct GeometrySlot {
        std::atomic_int state{GeometryFree};
        CaptureGeometry geometry;
    };

    static bool geometryMatches(const CaptureGeometry &left,
                                const CaptureGeometry &right)
    {
        return left.surface == right.surface
            && left.timer == right.timer
            && left.padding == right.padding
            && left.foregroundWindow == right.foregroundWindow
            && left.optics.contentRect == right.optics.contentRect
            && qFuzzyCompare(left.optics.radius, right.optics.radius)
            && qFuzzyCompare(left.optics.sideInset, right.optics.sideInset)
            && qFuzzyCompare(left.optics.earDepth, right.optics.earDepth)
            && qFuzzyCompare(left.optics.lensBand, right.optics.lensBand)
            && qFuzzyCompare(left.optics.thickness, right.optics.thickness)
            && qFuzzyCompare(left.optics.intensity, right.optics.intensity)
            && left.optics.pointer == right.optics.pointer
            && left.optics.pointerActive == right.optics.pointerActive
            && left.optics.pill == right.optics.pill;
    }

    bool takeLatestGeometry(CaptureGeometry &geometry)
    {
        GeometrySlot *newest = nullptr;
        for (GeometrySlot &slot : m_geometrySlots) {
            if (slot.state.load(std::memory_order_acquire) == GeometryReady
                && (!newest || slot.geometry.revision > newest->geometry.revision)) {
                newest = &slot;
            }
        }
        if (!newest) {
            return false;
        }
        int expected = GeometryReady;
        if (!newest->state.compare_exchange_strong(expected,
                                                   GeometryReading,
                                                   std::memory_order_acq_rel)) {
            return false;
        }
        geometry = newest->geometry;
        newest->state.store(GeometryFree, std::memory_order_release);

        for (GeometrySlot &slot : m_geometrySlots) {
            expected = GeometryReady;
            if (slot.state.compare_exchange_strong(expected,
                                                   GeometryWriting,
                                                   std::memory_order_acq_rel)) {
                slot.state.store(GeometryFree, std::memory_order_release);
            }
        }
        return true;
    }
#endif

    void run(std::stop_token token)
    {
#ifdef Q_OS_WIN
        bool apartmentInitialized = false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartmentInitialized = true;
        } catch (const winrt::hresult_error &) {
            return;
        }

        WindowCaptureSession session;
        auto lastDelivery = std::chrono::steady_clock::time_point{};
        auto lastTargetCheck = std::chrono::steady_clock::time_point{};
        CaptureGeometry geometry;
        bool geometryDirty = false;
        bool captureDirty = false;
        while (!token.stop_requested()) {
            geometryDirty = takeLatestGeometry(geometry) || geometryDirty;
            const HWND foregroundWindow = reinterpret_cast<HWND>(geometry.foregroundWindow);
            if (geometry.surface.isEmpty() || !foregroundWindow) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kFrameWaitMs));
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            const bool targetCheckDue = !session.isValid()
                || lastTargetCheck.time_since_epoch().count() == 0
                || now - lastTargetCheck >= std::chrono::milliseconds(50);
            if (targetCheckDue) {
                const HWND target = findUnderlyingWindow(foregroundWindow,
                                                         geometry.surface);
                lastTargetCheck = now;
                if (!target) {
                    session.reset();
                    captureDirty = false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                if (!session.isValid() || target != session.targetWindow()) {
                    if (!session.initialize(target, geometry.surface.center())) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    captureDirty = false;
                    geometryDirty = true;
                }
            }

            captureDirty = session.waitForFrame(token) || captureDirty;
            if (!session.isValid()) {
                session.reset();
                captureDirty = false;
                continue;
            }
            geometryDirty = takeLatestGeometry(geometry) || geometryDirty;
            if (!captureDirty && (!geometryDirty || !session.hasLatestFrame())) {
                continue;
            }
            const auto deliveryTime = std::chrono::steady_clock::now();
            if (lastDelivery.time_since_epoch().count() != 0
                && deliveryTime - lastDelivery
                    < std::chrono::milliseconds(kMinimumDeliveryIntervalMs)) {
                continue;
            }

            const bool consumeCapture = captureDirty;
            NativeLiquidGlassFrame frame = consumeCapture
                ? session.read(geometry) : session.renderLatest(geometry);
            if (consumeCapture) {
                captureDirty = false;
            }
            if (frame.surfaceTexture) {
                lastDelivery = deliveryTime;
                geometryDirty = false;
                m_callback(std::move(frame));
            } else if (session.hasLatestFrame()) {
                geometryDirty = true;
            }
        }
        session.reset();
        if (apartmentInitialized) {
            winrt::uninit_apartment();
        }
#else
        Q_UNUSED(token);
#endif
    }

    FrameCallback m_callback;
#ifdef Q_OS_WIN
    std::array<GeometrySlot, 3> m_geometrySlots;
    CaptureGeometry m_lastSubmittedGeometry;
    quint64 m_geometryRevision = 0;
    bool m_hasSubmittedGeometry = false;
#endif
    std::jthread m_thread;
};

LiquidGlassCaptureWorker::LiquidGlassCaptureWorker(FrameCallback callback)
    : m_impl(std::make_unique<Impl>(std::move(callback)))
{
}

LiquidGlassCaptureWorker::~LiquidGlassCaptureWorker() = default;

void LiquidGlassCaptureWorker::start()
{
    m_impl->start();
}

void LiquidGlassCaptureWorker::stop()
{
    m_impl->stop();
}

void LiquidGlassCaptureWorker::setGeometry(const QRect &surfaceCapture,
                                            const QRect &timerCapture,
                                            int padding,
                                            quintptr foregroundWindow,
                                            const NativeLiquidGlassOptics &optics)
{
    m_impl->setGeometry(surfaceCapture,
                        timerCapture,
                        padding,
                        foregroundWindow,
                        optics);
}
