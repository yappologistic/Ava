#include "enhancedtabtextureitem.h"

#include "enhancedtabsmanager.h"
#include "enhancedtabtexture.h"

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

struct ShaderBytecode {
    ComPtr<ID3DBlob> vertex;
    ComPtr<ID3DBlob> pixel;
};

const ShaderBytecode &shaderBytecode()
{
    static const ShaderBytecode bytecode = [] {
        static constexpr char vertexSource[] = R"(
cbuffer DrawData : register(b0)
{
    column_major float4x4 transform;
    float4 targetRect;
    float inheritedOpacity;
    float3 padding;
};
struct VertexOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VertexOutput main(uint vertexId : SV_VertexID)
{
    float2 unit = float2(vertexId & 1, (vertexId >> 1) & 1);
    VertexOutput output;
    float2 local = targetRect.xy + unit * targetRect.zw;
    output.position = mul(transform, float4(local, 0.0, 1.0));
    output.uv = unit;
    return output;
})";
        static constexpr char pixelSource[] = R"(
cbuffer DrawData : register(b0)
{
    column_major float4x4 transform;
    float4 targetRect;
    float inheritedOpacity;
    float3 padding;
};
Texture2D<float4> windowTexture : register(t0);
SamplerState linearSampler : register(s0);
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    return windowTexture.Sample(linearSampler, uv) * inheritedOpacity;
})";
        ShaderBytecode result;
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

class EnhancedTabRenderNode final : public QSGRenderNode
{
public:
    explicit EnhancedTabRenderNode(QQuickWindow *window) : m_window(window) {}

    void setTexture(std::shared_ptr<NativeEnhancedTabTexture> texture,
                    const QRectF &targetRect)
    {
        m_pendingTexture = std::move(texture);
        m_targetRect = targetRect;
    }

    QRectF rect() const override { return m_targetRect; }
    RenderingFlags flags() const override { return BoundedRectRendering; }
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

        if (m_pendingTexture) {
            SourceEntry *source = sourceEntry(m_pendingTexture);
            const QSize size = m_pendingTexture->size();
            if (source && ensureDisplayTexture(device, size)
                && SUCCEEDED(m_context4->Wait(source->fence.Get(),
                                               m_pendingTexture->producerFenceValue()))) {
                context->CopyResource(m_displayTexture.Get(), source->texture.Get());
                if (SUCCEEDED(m_context4->Signal(
                        source->fence.Get(),
                        m_pendingTexture->consumerFenceValue()))) {
                    m_pendingTexture->markDisplayed();
                    m_pendingTexture.reset();
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
            float inheritedOpacity;
            float padding[3];
        } constants{};
        std::copy_n(transform.constData(), 16, constants.transform);
        constants.targetRect[0] = float(m_targetRect.x());
        constants.targetRect[1] = float(m_targetRect.y());
        constants.targetRect[2] = float(m_targetRect.width());
        constants.targetRect[3] = float(m_targetRect.height());
        constants.inheritedOpacity = float(inheritedOpacity());
        m_constantIndex = (m_constantIndex + 1) % m_constants.size();
        ID3D11Buffer *constantBuffer = m_constants[m_constantIndex].Get();
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(constantBuffer,
                                0,
                                D3D11_MAP_WRITE_DISCARD,
                                0,
                                &mapped))) {
            return;
        }
        std::memcpy(mapped.pData, &constants, sizeof(constants));
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
        const QRect qtScissor = state->scissorEnabled()
            ? state->scissorRect() : QRect(0, 0, 32767, 32767);
        const D3D11_RECT scissor{qtScissor.left(),
                                 qtScissor.top(),
                                 qtScissor.right() + 1,
                                 qtScissor.bottom() + 1};
        context->RSSetScissorRects(1, &scissor);
        context->OMSetDepthStencilState(m_depthStencil.Get(), 0);
        context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xffffffff);
        context->Draw(4, 0);
        ID3D11ShaderResourceView *noView = nullptr;
        context->PSSetShaderResources(0, 1, &noView);
    }

    void releaseResources() override
    {
        for (SourceEntry &entry : m_sources) {
            entry = {};
        }
        m_displayView.Reset();
        m_displayTexture.Reset();
        m_vertexShader.Reset();
        m_pixelShader.Reset();
        for (auto &constant : m_constants) {
            constant.Reset();
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
        const ShaderBytecode &bytecode = shaderBytecode();
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
        D3D11_BUFFER_DESC constantDescription{};
        constantDescription.ByteWidth = 96;
        constantDescription.Usage = D3D11_USAGE_DYNAMIC;
        constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        for (auto &constant : m_constants) {
            if (FAILED(device->CreateBuffer(&constantDescription, nullptr, &constant))) {
                return false;
            }
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
        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_NONE;
        rasterizer.DepthClipEnable = TRUE;
        rasterizer.ScissorEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&rasterizer, &m_rasterizer))) {
            return false;
        }
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE;
        if (FAILED(device->CreateDepthStencilState(&depth, &m_depthStencil))) {
            return false;
        }
        D3D11_BLEND_DESC blend{};
        auto &target = blend.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D11_BLEND_ONE;
        target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOp = D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D11_BLEND_ONE;
        target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        return SUCCEEDED(device->CreateBlendState(&blend, &m_blendState));
    }

    SourceEntry *sourceEntry(const std::shared_ptr<NativeEnhancedTabTexture> &texture)
    {
        if (!texture || !texture->sourceId() || !texture->sharedHandle()
            || !texture->sharedFenceHandle()) {
            return nullptr;
        }
        SourceEntry *available = nullptr;
        for (SourceEntry &entry : m_sources) {
            if (entry.sourceId == texture->sourceId()) {
                return &entry;
            }
            if (!available && entry.sourceId == 0) {
                available = &entry;
            }
        }
        if (!available) {
            m_sources.front() = {};
            available = &m_sources.front();
        }
        if (FAILED(m_device1->OpenSharedResource1(
                reinterpret_cast<HANDLE>(texture->sharedHandle()),
                IID_PPV_ARGS(&available->texture)))
            || FAILED(m_device5->OpenSharedFence(
                reinterpret_cast<HANDLE>(texture->sharedFenceHandle()),
                IID_PPV_ARGS(&available->fence)))) {
            *available = {};
            return nullptr;
        }
        available->sourceId = texture->sourceId();
        return available;
    }

    bool ensureDisplayTexture(ID3D11Device *device, const QSize &size)
    {
        if (!size.isEmpty() && m_displayTexture && size == m_displaySize) {
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
        if (FAILED(device->CreateTexture2D(&description, nullptr, &m_displayTexture))
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
    std::shared_ptr<NativeEnhancedTabTexture> m_pendingTexture;
    QRectF m_targetRect;
    std::array<SourceEntry, 8> m_sources;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11Device1> m_device1;
    ComPtr<ID3D11Device5> m_device5;
    ComPtr<ID3D11DeviceContext4> m_context4;
    ComPtr<ID3D11Texture2D> m_displayTexture;
    ComPtr<ID3D11ShaderResourceView> m_displayView;
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    std::array<ComPtr<ID3D11Buffer>, 3> m_constants;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11RasterizerState> m_rasterizer;
    ComPtr<ID3D11DepthStencilState> m_depthStencil;
    ComPtr<ID3D11BlendState> m_blendState;
    QSize m_displaySize;
    size_t m_constantIndex = 0;
    bool m_hasFrame = false;
};
#else
class EnhancedTabRenderNode final : public QSGRenderNode
{
public:
    explicit EnhancedTabRenderNode(QQuickWindow *) {}
    void setTexture(std::shared_ptr<NativeEnhancedTabTexture>, const QRectF &rect)
    {
        m_rect = rect;
    }
    void render(const RenderState *) override {}
    QRectF rect() const override { return m_rect; }
private:
    QRectF m_rect;
};
#endif

} // namespace

EnhancedTabTextureItem::EnhancedTabTextureItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void EnhancedTabTextureItem::prewarmShaders()
{
#ifdef Q_OS_WIN
    (void)shaderBytecode();
#endif
}

void EnhancedTabTextureItem::setManager(EnhancedTabsManager *manager)
{
    if (m_manager == manager) {
        return;
    }
    if (m_manager) {
        disconnect(m_manager, nullptr, this, nullptr);
    }
    m_manager = manager;
    if (m_manager) {
        connect(m_manager,
                &EnhancedTabsManager::frameChanged,
                this,
                &EnhancedTabTextureItem::refreshFrame);
    }
    refreshFrame();
    emit managerChanged();
}

void EnhancedTabTextureItem::setWindowKey(const QString &windowKey)
{
    if (m_windowKey == windowKey) {
        return;
    }
    m_windowKey = windowKey;
    refreshFrame();
    emit windowKeyChanged();
}

QSGNode *EnhancedTabTextureItem::updatePaintNode(QSGNode *oldNode,
                                                  UpdatePaintNodeData *data)
{
    Q_UNUSED(data);
    auto *node = static_cast<EnhancedTabRenderNode *>(oldNode);
    if (!node) {
        node = new EnhancedTabRenderNode(window());
    }
    node->setTexture(std::move(m_pendingTexture), boundingRect());
    return node;
}

void EnhancedTabTextureItem::refreshFrame(const QString &changedKey)
{
    if (!changedKey.isEmpty() && changedKey != m_windowKey) {
        return;
    }
    QSize nextSize;
    if (m_manager && !m_windowKey.isEmpty()) {
        m_pendingTexture = m_manager->nativeTexture(m_windowKey);
        if (m_pendingTexture) {
            nextSize = m_pendingTexture->size();
        }
    } else {
        m_pendingTexture.reset();
    }
    if (nextSize != m_sourceSize) {
        m_sourceSize = nextSize;
        emit sourceSizeChanged();
    }
    update();
}
