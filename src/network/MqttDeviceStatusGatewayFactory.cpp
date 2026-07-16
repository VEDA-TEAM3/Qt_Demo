#include "network/MqttDeviceStatusGatewayFactory.h"

#include "network/MqttDeviceStatusGateway.h"

std::shared_ptr<DeviceStatusGateway> MqttDeviceStatusGatewayFactory::create(QObject* parent) const {
    return std::make_shared<MqttDeviceStatusGateway>(MqttDeviceStatusConfig::fromEnvironment(), parent);
}
