#include "liquidglasstextureitem.h"

#include "liquidglassbackdrop.h"

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGRenderNode>

#include <algorithm>
#include <array>
#include <cstring>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#endif

namespace {

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;

struct BlitShaderBytecode {
    ComPtr<ID3DBlob> vertex;
    ComPtr<ID3DBlob> pixel;
};

const BlitShaderBytecode &blitShaderBytecode()
{
    static const BlitShaderBytecode bytecode = []() {
        static constexpr char vertexSource[] = R"(
cbuffer DrawData : register(b0)
{
    column_major float4x4 transform;
    float4 targetRect;
    float4 sourceRect;
    float4 material;
};
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};
VertexOutput main(uint vertexId : SV_VertexID)
{
    float2 unit = float2(vertexId & 1, (vertexId >> 1) & 1);
    VertexOutput output;
    float2 local = targetRect.xy + unit * targetRect.zw;
    output.position = mul(transform, float4(local, 0.0, 1.0));
    output.uv = sourceRect.xy + unit * sourceRect.zw;
    return output;
})";
        static constexpr char pixelSource[] = R"(
Texture2D<float4> glassTexture : register(t0);
SamplerState linearSampler : register(s0);
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    return glassTexture.Sample(linearSampler, uv);
})";

        BlitShaderBytecode result;
        if (FAILED(D3DCompile(vertexSource,
                              sizeof(vertexSource) - 1,
                              nullptr,
                              nullptr,
                              nullptr,
                              "main",
                              "vs_4_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3,
                              0,
                              &result.vertex,
                              nullptr))) {
            return result;
        }
        D3DCompile(pixelSource,
                   sizeof(pixelSource) - 1,
                   nullptr,
                   nullptr,
                   nullptr,
                   "main",
                   "ps_4_0",
                   D3DCOMPILE_OPTIMIZATION_LEVEL3,
                   0,
                   &result.pixel,
                   nullptr);
        return result;
    }();
    return bytecode;
}

class LiquidGlassRenderNode final : public QSGRenderNode
{
public:
    explicit LiquidGlassRenderNode(QQuickWindow *window)
        : m_window(window)
    {
    }

    void setFrame(std::shared_ptr<NativeLiquidGlassTextureLease> lease,
                  const QRectF &sourceRect,
                  const QRectF &targetRect)
    {
        m_pendingLease = std::move(lease);
        m_sourceRect = sourceRect;
        m_targetRect = targetRect;
    }

    QRectF rect() const override { return m_targetRect; }

    RenderingFlags flags() const override
    {
        return BoundedRectRendering;
    }

    StateFlags changedStates() const override
    {
        return DepthState | StencilState | ScissorState | BlendState
            | CullState | ColorState;
    }

    void render(const RenderState *state) override
    {
        if (!m_window || !state || m_targetRect.isEmpty()) {
            return;
        }

        QSGRendererInterface *renderer = m_window->rendererInterface();
        if (!renderer
            || renderer->graphicsApi() != QSGRendererInterface::Direct3D11) {
            return;
        }
        auto *device = static_cast<ID3D11Device *>(
            renderer->getResource(m_window, QSGRendererInterface::DeviceResource));
        auto *context = static_cast<ID3D11DeviceContext *>(
            renderer->getResource(m_window,
                                  QSGRendererInterface::DeviceContextResource));
        if (!device || !context || !ensurePipeline(device, context)) {
            return;
        }

        if (m_pendingLease) {
            const QSize frameSize = m_pendingLease->size();
            SourceEntry *source = sourceEntry(device, m_pendingLease);
            const QRect sourcePixels = m_sourceRect.toAlignedRect().intersected(
                QRect(QPoint(0, 0), frameSize));
            if (source && ensureDisplayTexture(device, frameSize)
                && !sourcePixels.isEmpty()
                && SUCCEEDED(m_context4->Wait(source->fence.Get(),
                                               m_pendingLease->producerFenceValue()))) {
                const D3D11_BOX sourceBox{
                    UINT(sourcePixels.left()),
                    UINT(sourcePixels.top()),
                    0,
                    UINT(sourcePixels.right() + 1),
                    UINT(sourcePixels.bottom() + 1),
                    1};
                context->CopySubresourceRegion(m_displayTexture.Get(),
                                               0,
                                               0,
                                               0,
                                               0,
                                               source->texture.Get(),
                                               0,
                                               &sourceBox);
                if (SUCCEEDED(m_context4->Signal(
                        source->fence.Get(),
                        m_pendingLease->consumerFenceValue()))) {
                    m_displayContentSize = sourcePixels.size();
                    m_pendingLease->markDisplayed();
                    m_pendingLease.reset();
                    m_hasFrame = true;
                }
            }
        }
        if (!m_hasFrame || !m_displayView) {
            return;
        }

        QMatrix4x4 transform;
        if (state->projectionMatrix()) {
            transform = *state->projectionMatrix();
        }
        if (matrix()) {
            transform *= *matrix();
        }

        struct alignas(16) DrawConstants {
            float transform[16];
            float targetRect[4];
            float sourceRect[4];
            float material[4];
        } constants{};
        std::copy_n(transform.constData(), 16, constants.transform);
        constants.targetRect[0] = float(m_targetRect.x());
        constants.targetRect[1] = float(m_targetRect.y());
        constants.targetRect[2] = float(m_targetRect.width());
        constants.targetRect[3] = float(m_targetRect.height());
        constants.sourceRect[0] = 0.0f;
        constants.sourceRect[1] = 0.0f;
        constants.sourceRect[2] = float(m_displayContentSize.width())
            / float(m_displaySize.width());
        constants.sourceRect[3] = float(m_displayContentSize.height())
            / float(m_displaySize.height());
        constants.material[0] = 1.0f;
        m_drawConstantIndex = (m_drawConstantIndex + 1) % m_drawConstants.size();
        ID3D11Buffer *constantBuffer = m_drawConstants[m_drawConstantIndex].Get();
        D3D11_MAPPED_SUBRESOURCE mappedConstants{};
        if (FAILED(context->Map(constantBuffer,
                                0,
                                D3D11_MAP_WRITE_DISCARD,
                                0,
                                &mappedConstants))) {
            return;
        }
        std::memcpy(mappedConstants.pData, &constants, sizeof(constants));
        context->Unmap(constantBuffer, 0);

        ID3D11ShaderResourceView *view = m_displayView.Get();
        ID3D11SamplerState *sampler = m_sampler.Get();
        constexpr FLOAT blendFactor[4] = {0, 0, 0, 0};
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &constantBuffer);
        context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &constantBuffer);
        context->PSSetShaderResources(0, 1, &view);
        context->PSSetSamplers(0, 1, &sampler);
        context->RSSetState(m_rasterizer.Get());
        context->OMSetDepthStencilState(m_depthStencil.Get(), 0);
        context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xffffffff);
        context->Draw(4, 0);
        ID3D11ShaderResourceView *noView = nullptr;
        context->PSSetShaderResources(0, 1, &noView);
    }

    void releaseResources() override
    {
        for (SourceEntry &source : m_sources) {
            source = {};
        }
        m_displayView.Reset();
        m_displayTexture.Reset();
        m_vertexShader.Reset();
        m_pixelShader.Reset();
        for (ComPtr<ID3D11Buffer> &buffer : m_drawConstants) {
            buffer.Reset();
        }
        m_sampler.Reset();
        m_rasterizer.Reset();
        m_depthStencil.Reset();
        m_blendState.Reset();
        m_context4.Reset();
        m_device5.Reset();
        m_device1.Reset();
        m_device.Reset();
        m_displaySize = {};
        m_displayContentSize = {};
        m_hasFrame = false;
    }

private:
    struct SourceEntry {
        quint64 sourceId = 0;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11Fence> fence;
    };

    bool ensurePipeline(ID3D11Device *device, ID3D11DeviceContext *context)
    {
        if (m_device.Get() == device && m_vertexShader && m_pixelShader) {
            return true;
        }
        releaseResources();
        m_device = device;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&m_device1)))
            || FAILED(device->QueryInterface(IID_PPV_ARGS(&m_device5)))
            || FAILED(context->QueryInterface(IID_PPV_ARGS(&m_context4)))) {
            return false;
        }

        const BlitShaderBytecode &bytecode = blitShaderBytecode();
        if (!bytecode.vertex || !bytecode.pixel
            || FAILED(device->CreateVertexShader(bytecode.vertex->GetBufferPointer(),
                                                   bytecode.vertex->GetBufferSize(),
                                                   nullptr,
                                                   &m_vertexShader))
            || FAILED(device->CreatePixelShader(bytecode.pixel->GetBufferPointer(),
                                                 bytecode.pixel->GetBufferSize(),
                                                 nullptr,
                                                 &m_pixelShader))) {
            return false;
        }

        D3D11_BUFFER_DESC bufferDescription{};
        bufferDescription.ByteWidth = 112;
        bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
        bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        for (ComPtr<ID3D11Buffer> &constantBuffer : m_drawConstants) {
            if (FAILED(device->CreateBuffer(&bufferDescription,
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
        samplerDescription.MinLOD = 0;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&samplerDescription, &m_sampler))) {
            return false;
        }

        D3D11_RASTERIZER_DESC rasterizerDescription{};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.DepthClipEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&rasterizerDescription,
                                                  &m_rasterizer))) {
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC depthDescription{};
        depthDescription.DepthEnable = FALSE;
        depthDescription.StencilEnable = FALSE;
        if (FAILED(device->CreateDepthStencilState(&depthDescription,
                                                    &m_depthStencil))) {
            return false;
        }

        D3D11_BLEND_DESC blendDescription{};
        auto &target = blendDescription.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D11_BLEND_ONE;
        target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOp = D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D11_BLEND_ONE;
        target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        return SUCCEEDED(device->CreateBlendState(&blendDescription, &m_blendState));
    }

    SourceEntry *sourceEntry(
        ID3D11Device *device,
        const std::shared_ptr<NativeLiquidGlassTextureLease> &lease)
    {
        if (!lease || lease->sourceId() == 0 || lease->sharedHandle() == 0
            || lease->sharedFenceHandle() == 0) {
            return nullptr;
        }
        const quint64 key = lease->sourceId();
        SourceEntry *available = nullptr;
        for (SourceEntry &entry : m_sources) {
            if (entry.sourceId == key) {
                return &entry;
            }
            if (!available && entry.sourceId == 0) {
                available = &entry;
            }
        }
        if (!available) {
            for (SourceEntry &entry : m_sources) {
                entry = {};
            }
            available = &m_sources.front();
        }
        if (FAILED(m_device1->OpenSharedResource1(
                reinterpret_cast<HANDLE>(lease->sharedHandle()),
                IID_PPV_ARGS(&available->texture)))) {
            *available = {};
            return nullptr;
        }
        if (FAILED(m_device5->OpenSharedFence(
                reinterpret_cast<HANDLE>(lease->sharedFenceHandle()),
                IID_PPV_ARGS(&available->fence)))) {
            *available = {};
            return nullptr;
        }
        available->sourceId = key;
        return available;
    }

    bool ensureDisplayTexture(ID3D11Device *device, const QSize &size)
    {
        if (!size.isEmpty() && m_displayTexture && m_displaySize == size) {
            return true;
        }
        if (size.isEmpty()) {
            return false;
        }
        m_displayView.Reset();
        m_displayTexture.Reset();
        D3D11_TEXTURE2D_DESC description{};
        description.Width = UINT(size.width());
        description.Height = UINT(size.height());
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&description,
                                           nullptr,
                                           &m_displayTexture))
            || FAILED(device->CreateShaderResourceView(m_displayTexture.Get(),
                                                        nullptr,
                                                        &m_displayView))) {
            return false;
        }
        m_displaySize = size;
        m_hasFrame = false;
        return true;
    }

    QQuickWindow *m_window = nullptr;
    std::shared_ptr<NativeLiquidGlassTextureLease> m_pendingLease;
    QRectF m_sourceRect;
    QRectF m_targetRect;
    std::array<SourceEntry, 12> m_sources;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11Device1> m_device1;
    ComPtr<ID3D11Device5> m_device5;
    ComPtr<ID3D11DeviceContext4> m_context4;
    ComPtr<ID3D11Texture2D> m_displayTexture;
    ComPtr<ID3D11ShaderResourceView> m_displayView;
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    std::array<ComPtr<ID3D11Buffer>, 3> m_drawConstants;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11RasterizerState> m_rasterizer;
    ComPtr<ID3D11DepthStencilState> m_depthStencil;
    ComPtr<ID3D11BlendState> m_blendState;
    QSize m_displaySize;
    QSize m_displayContentSize;
    size_t m_drawConstantIndex = 0;
    bool m_hasFrame = false;
};
#else
class LiquidGlassRenderNode final : public QSGRenderNode
{
public:
    explicit LiquidGlassRenderNode(QQuickWindow *) {}
    void setFrame(std::shared_ptr<NativeLiquidGlassTextureLease>,
                  const QRectF &,
                  const QRectF &targetRect)
    {
        m_targetRect = targetRect;
    }
    void render(const RenderState *) override {}
    QRectF rect() const override { return m_targetRect; }
private:
    QRectF m_targetRect;
};
#endif

} // namespace

LiquidGlassTextureItem::LiquidGlassTextureItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void LiquidGlassTextureItem::prewarmShaders()
{
#ifdef Q_OS_WIN
    (void)blitShaderBytecode();
#endif
}

void LiquidGlassTextureItem::setBackdrop(LiquidGlassBackdrop *backdrop)
{
    if (m_backdrop == backdrop) {
        return;
    }
    if (m_backdrop) {
        disconnect(m_backdrop, nullptr, this, nullptr);
    }
    m_backdrop = backdrop;
    if (m_backdrop) {
        connect(m_backdrop,
                &LiquidGlassBackdrop::frameChanged,
                this,
                &LiquidGlassTextureItem::refreshFrame);
    }
    refreshFrame();
    emit backdropChanged();
}

void LiquidGlassTextureItem::setTimer(bool timer)
{
    if (m_timer == timer) {
        return;
    }
    m_timer = timer;
    refreshFrame();
    emit timerChanged();
}

QSGNode *LiquidGlassTextureItem::updatePaintNode(QSGNode *oldNode,
                                                  UpdatePaintNodeData *data)
{
    Q_UNUSED(data);
    auto *node = static_cast<LiquidGlassRenderNode *>(oldNode);
    if (!node) {
        node = new LiquidGlassRenderNode(window());
    }

    // updatePaintNode runs during Qt Quick's synchronized scene-graph phase.
    // The GUI-thread snapshot therefore moves across without a render-thread
    // mutex or a call back into the live capture object.
    node->setFrame(std::move(m_pendingLease), m_pendingSourceRect, boundingRect());
    return node;
}

void LiquidGlassTextureItem::releaseResources()
{
    m_textureProvider = nullptr;
}

bool LiquidGlassTextureItem::isTextureProvider() const
{
    return false;
}

QSGTextureProvider *LiquidGlassTextureItem::textureProvider() const
{
    return nullptr;
}

void LiquidGlassTextureItem::refreshFrame()
{
    QSize nextSize;
    if (m_backdrop) {
        m_pendingLease = m_timer ? m_backdrop->nativeTimerTexture()
                                 : m_backdrop->nativeSurfaceTexture();
        if (m_pendingLease) {
            nextSize = m_pendingLease->size();
            m_pendingSourceRect = m_timer
                ? QRectF(m_backdrop->nativeTimerContentRect())
                : QRectF(m_backdrop->nativeSurfaceContentRect());
        }
    } else {
        m_pendingLease.reset();
        m_pendingSourceRect = {};
    }
    if (m_sourceSize != nextSize) {
        m_sourceSize = nextSize;
        emit sourceSizeChanged();
    }
    update();
}
