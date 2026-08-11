#pragma once

#include <QSize>
#include <QString>
#include <QtGlobal>

#include <memory>

class NativeEnhancedTabTexture
{
public:
    virtual ~NativeEnhancedTabTexture() = default;

    virtual quint64 sourceId() const = 0;
    virtual quintptr sharedHandle() const = 0;
    virtual quintptr sharedFenceHandle() const = 0;
    virtual quint64 producerFenceValue() const = 0;
    virtual quint64 consumerFenceValue() const = 0;
    virtual QSize size() const = 0;
    virtual void markDisplayed() = 0;
};

struct NativeEnhancedTabFrame
{
    QString windowKey;
    std::shared_ptr<NativeEnhancedTabTexture> texture;
};
