#include "network/MqttDeviceStatusGatewayFactory.h"

#include <QVector>
#include <memory>

#include "network/BlurTopicHandler.h"
#include "network/DeviceStatusTopicHandler.h"
#include "network/MqttConnectionConfig.h"
#include "network/MqttDeviceStatusGateway.h"
#include "network/MqttMessageRouter.h"
#include "network/QtMqttTransportFactory.h"
#include "network/RiskTopicHandler.h"

std::shared_ptr<DeviceStatusGateway> MqttDeviceStatusGatewayFactory::create(QObject* parent) const {
    const MqttConnectionConfig config = MqttConnectionConfig::fromEnvironment();
    auto transportFactory = std::make_shared<QtMqttTransportFactory>(config);

    QVector<std::shared_ptr<MqttTopicHandler>> handlers;
    handlers.append(std::make_shared<DeviceStatusTopicHandler>());
    handlers.append(std::make_shared<RiskTopicHandler>());
    handlers.append(std::make_shared<BlurTopicHandler>());
    auto messageRouter = std::make_shared<MqttMessageRouter>(std::move(handlers));

    return std::make_shared<MqttDeviceStatusGateway>(std::move(transportFactory), std::move(messageRouter),
                                                     config.debugLogging, parent);
}
