#pragma once

#include <QString>

struct MqttConnectionConfig {
    QString host;
    quint16 port = 8883;
    QString caCertificatePath;
    QString clientId;
    int keepAliveSeconds = 60;
    bool debugLogging = true;

    static MqttConnectionConfig fromEnvironment();
};
