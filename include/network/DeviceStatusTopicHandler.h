#pragma once

#include "network/MqttTopicHandler.h"

class DeviceStatusTopicHandler final : public MqttTopicHandler {
public:
    QVector<MqttSubscription> subscriptions() const override;
    bool matchesTopic(const QString& topic) const override;
    bool handle(const QByteArray& payload, const QString& topic, MqttMessageBatch& messages,
                QString& error) const override;
};
