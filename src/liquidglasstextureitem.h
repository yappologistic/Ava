#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QRectF>

#include <memory>

class LiquidGlassBackdrop;
class NativeLiquidGlassTextureLease;
class QSGTextureProvider;

class LiquidGlassTextureItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(LiquidGlassBackdrop *backdrop READ backdrop WRITE setBackdrop NOTIFY backdropChanged)
    Q_PROPERTY(bool timer READ timer WRITE setTimer NOTIFY timerChanged)
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)

public:
    explicit LiquidGlassTextureItem(QQuickItem *parent = nullptr);

    // Compile immutable shader bytecode before the first visible scene-graph
    // frame so renderer initialization cannot hitch the island entrance.
    static void prewarmShaders();

    LiquidGlassBackdrop *backdrop() const { return m_backdrop; }
    void setBackdrop(LiquidGlassBackdrop *backdrop);

    bool timer() const { return m_timer; }
    void setTimer(bool timer);

    QSize sourceSize() const { return m_sourceSize; }

signals:
    void backdropChanged();
    void timerChanged();
    void sourceSizeChanged();

private:
    void refreshFrame();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode,
                             UpdatePaintNodeData *data) override;
    void releaseResources() override;
    bool isTextureProvider() const override;
    QSGTextureProvider *textureProvider() const override;

private:
    QPointer<LiquidGlassBackdrop> m_backdrop;
    std::shared_ptr<NativeLiquidGlassTextureLease> m_pendingLease;
    QRectF m_pendingSourceRect;
    bool m_timer = false;
    QSize m_sourceSize;
    mutable QSGTextureProvider *m_textureProvider = nullptr;
};
