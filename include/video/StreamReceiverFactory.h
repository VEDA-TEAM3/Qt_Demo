#pragma once

#include <gst/gst.h>

#include <memory>

class StreamReceiver;

class StreamReceiverFactory {
public:
    virtual ~StreamReceiverFactory() = default;

    virtual std::shared_ptr<StreamReceiver> create(guintptr outputWindowHandle) const = 0;
};

class GstStreamReceiverFactory final : public StreamReceiverFactory {
public:
    std::shared_ptr<StreamReceiver> create(guintptr outputWindowHandle) const override;
};
