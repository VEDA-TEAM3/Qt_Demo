#pragma once

#include <QString>

#include "network/DeviceStatusGateway.h"

class QByteArray;
class QMqttClient;
class QTimer;

struct MqttDeviceStatusConfig {
    QString host;
    quint16 port = 8883;
    QString caCertificatePath;
    QString clientId;
    int keepAliveSeconds = 60;

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
    void emitProtocolError(QString detail);

    MqttDeviceStatusConfig config_;
    QMqttClient* client_ = nullptr;
    QTimer* reconnectTimer_ = nullptr;
    bool stopping_ = false;
};
