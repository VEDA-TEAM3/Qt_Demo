#pragma once

#include "video/StreamReceiverFactory.h"

class GstStreamReceiverFactory final : public StreamReceiverFactory {
public:
    std::shared_ptr<StreamReceiver> create(WId outputWindowHandle) const override;
};
