#pragma once

#include "network/DeviceStatusGatewayFactory.h"

class MqttDeviceStatusGatewayFactory final : public DeviceStatusGatewayFactory {
public:
    std::shared_ptr<DeviceStatusGateway> create(QObject* parent = nullptr) const override;
};
