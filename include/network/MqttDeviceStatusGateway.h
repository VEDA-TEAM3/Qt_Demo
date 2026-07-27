#pragma once

#include <array>
#include <memory>

#include "network/DeviceStatusGateway.h"

class BlurFrameDispatcher;
class MqttMessageRouter;
class MqttTransport;
class MqttTransportFactory;
class RiskFrameDispatcher;
struct MqttMessageBatch;

class MqttDeviceStatusGateway final : public DeviceStatusGateway {
    Q_OBJECT

public:
    MqttDeviceStatusGateway(std::shared_ptr<MqttTransportFactory> transportFactory,
                            std::shared_ptr<MqttMessageRouter> messageRouter, bool debugLogging,
                            QObject* parent = nullptr);
    ~MqttDeviceStatusGateway() override;

    void start() override;
    void stop() override;

private:
    void handleConnectionChanged(bool connected);
    void subscribeToTopics();
    void handleMessage(const QByteArray& payload, const QString& topic);
    void dispatchMessages(MqttMessageBatch messages);
    void logReceivedMessage(const QByteArray& payload, const QString& topic) const;
    void logBlurFrame(const QString& topic, const BlurFrameData& frame);
    void emitProtocolError(QString detail);

    std::shared_ptr<MqttTransportFactory> transportFactory_;
    std::shared_ptr<MqttMessageRouter> messageRouter_;
    std::unique_ptr<MqttTransport> transport_;
    BlurFrameDispatcher* blurDispatcher_ = nullptr;
    RiskFrameDispatcher* riskDispatcher_ = nullptr;
    std::array<qint64, 4> lastBlurDebugLogMsec_{};
    bool debugLogging_ = true;
};
