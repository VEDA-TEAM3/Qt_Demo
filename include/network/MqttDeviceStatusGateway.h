#pragma once

#include <array>

#include <QString>

#include "network/DeviceStatusGateway.h"

class QByteArray;
class BlurFrameDispatcher;
class QMqttClient;
class QTimer;
class TopViewFrameDispatcher;

struct MqttDeviceStatusConfig {
    QString host;
    quint16 port = 8883;
    QString caCertificatePath;
    QString clientId;
    int keepAliveSeconds = 60;
    bool debugLogging = true;

    static MqttDeviceStatusConfig fromEnvironment();
};

class MqttDeviceStatusGateway final : public DeviceStatusGateway {
    Q_OBJECT

public:
    explicit MqttDeviceStatusGateway(MqttDeviceStatusConfig config = MqttDeviceStatusConfig::fromEnvironment(),
                                     QObject* parent = nullptr);

    void start() override;
    void stop() override;

private:
    void connectToBroker();
    void subscribeToTopics();
    void scheduleReconnect();
    void handleMessage(const QByteArray& payload, const QString& topic);
    void logReceivedMessage(const QByteArray& payload, const QString& topic) const;
    void logBlurFrame(const QString& topic, const BlurFrameData& frame);
    void logTopViewFrame(const QString& topic, const TopViewFrameData& frame);
    void emitProtocolError(QString detail);

    MqttDeviceStatusConfig config_;
    QMqttClient* client_ = nullptr;
    QTimer* reconnectTimer_ = nullptr;
    BlurFrameDispatcher* blurDispatcher_ = nullptr;
    TopViewFrameDispatcher* topViewDispatcher_ = nullptr;
    std::array<qint64, 4> lastBlurDebugLogMsec_{};
    std::array<qint64, 4> lastTopViewDebugLogMsec_{};
    bool stopping_ = false;
};
