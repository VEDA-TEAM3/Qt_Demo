#include "network/MqttConnectionConfig.h"

#include <QProcessEnvironment>
#include <QUuid>

namespace {
QString environmentValue(const QProcessEnvironment& environment, const QString& name, const QString& fallback) {
    const QString value = environment.value(name).trimmed();
    return value.isEmpty() ? fallback : value;
}

bool environmentFlag(const QProcessEnvironment& environment, const QString& name, bool fallback) {
    const QString value = environment.value(name).trimmed().toLower();
    if (value.isEmpty()) {
        return fallback;
    }

    if (value == QStringLiteral("1") || value == QStringLiteral("true") || value == QStringLiteral("on") ||
        value == QStringLiteral("yes")) {
        return true;
    }

    if (value == QStringLiteral("0") || value == QStringLiteral("false") || value == QStringLiteral("off") ||
        value == QStringLiteral("no")) {
        return false;
    }

    return fallback;
}
}  // namespace

/**
 * @brief   프로세스 환경 변수에서 MQTT TLS 접속 설정을 읽습니다.
 * @return  누락된 항목에 기본값을 적용한 접속 설정
 */
MqttConnectionConfig MqttConnectionConfig::fromEnvironment() {
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    MqttConnectionConfig config;
    config.host = environmentValue(environment, QStringLiteral("VEDA_MQTT_HOST"), QStringLiteral("100.73.128.114"));
    config.caCertificatePath =
        environmentValue(environment, QStringLiteral("VEDA_MQTT_CA_FILE"), QStringLiteral("/etc/veda/certs/ca.crt"));
    config.clientId =
        environmentValue(environment, QStringLiteral("VEDA_MQTT_CLIENT_ID"),
                         QStringLiteral("qt-device-status-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    config.debugLogging = environmentFlag(environment, QStringLiteral("VEDA_MQTT_DEBUG"), true);

    bool portValid = false;
    const uint configuredPort =
        environment.value(QStringLiteral("VEDA_MQTT_PORT"), QStringLiteral("8883")).toUInt(&portValid);
    config.port = portValid && configuredPort <= 65535 ? static_cast<quint16>(configuredPort) : 8883;
    return config;
}
