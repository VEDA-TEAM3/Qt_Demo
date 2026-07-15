#include "network/MqttDeviceStatusGateway.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <QUuid>
#include <QtMqtt/QMqttClient>
#include <QtMqtt/QMqttTopicName>
#include <utility>

namespace {
constexpr auto controllerStatusTopic = "veda/hw/+/status";
constexpr auto centralStatusTopic = "veda/hw/status";
constexpr auto sensorAliveTopic = "veda/ch/+/alive";
constexpr int statusQos = 1;
constexpr int reconnectIntervalMsec = 3000;
constexpr int deviceChannelCount = 4;
constexpr int protocolVersion = 1;

enum class StatusProtocol {
    Controller,
    Central,
};

QString environmentValue(const QProcessEnvironment& environment, const QString& name, const QString& fallback) {
    const QString value = environment.value(name).trimmed();
    return value.isEmpty() ? fallback : value;
}

bool readInteger(const QJsonObject& object, const QString& name, qint64& value) {
    const QJsonValue jsonValue = object.value(name);
    if (!jsonValue.isDouble()) {
        return false;
    }

    const qint64 integerValue = jsonValue.toInteger();
    if (static_cast<double>(integerValue) != jsonValue.toDouble()) {
        return false;
    }

    value = integerValue;
    return true;
}

bool readBoolean(const QJsonObject& object, const QString& name, bool& value) {
    const QJsonValue jsonValue = object.value(name);
    if (!jsonValue.isBool()) {
        return false;
    }

    value = jsonValue.toBool();
    return true;
}

bool parseOutputState(const QJsonObject& object, DeviceOutputState& outputs, QString& error) {
    if (!readBoolean(object, QStringLiteral("ledRed"), outputs.ledRed) ||
        !readBoolean(object, QStringLiteral("ledYellow"), outputs.ledYellow) ||
        !readBoolean(object, QStringLiteral("ledGreen"), outputs.ledGreen) ||
        !readBoolean(object, QStringLiteral("siren"), outputs.beacon) ||
        !readBoolean(object, QStringLiteral("buzzer"), outputs.buzzer)) {
        error = QStringLiteral("state must contain boolean ledRed, ledYellow, ledGreen, siren and buzzer fields");
        return false;
    }

    return true;
}

int channelIndexForStatus(qint64 channelId, StatusProtocol protocol) {
    if (protocol == StatusProtocol::Controller) {
        return channelId >= 1 && channelId <= deviceChannelCount ? static_cast<int>(channelId - 1) : -1;
    }

    return channelId >= 0 && channelId < deviceChannelCount ? static_cast<int>(channelId) : -1;
}

bool parseStatusPayload(const QByteArray& payload, const QString& topic, StatusProtocol protocol,
                        DeviceStatusReport& report, QString& error) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Invalid status JSON on %1: %2").arg(topic, parseError.errorString());
        return false;
    }

    const QJsonObject object = document.object();
    const QJsonValue detailValue = object.value(QStringLiteral("detail"));
    if (!detailValue.isString()) {
        error = QStringLiteral("Missing string detail field on %1").arg(topic);
        return false;
    }

    report.detail = detailValue.toString();
    report.node = object.value(QStringLiteral("node")).toString();
    if (report.node.isEmpty()) {
        report.node = protocol == StatusProtocol::Central ? QStringLiteral("central-control-server")
                                                          : topic.section(QLatin1Char('/'), 2, 2);
    }

    qint64 sourceTimestamp = 0;
    if (object.contains(QStringLiteral("ts")) && !readInteger(object, QStringLiteral("ts"), sourceTimestamp)) {
        error = QStringLiteral("Invalid ts field on %1").arg(topic);
        return false;
    }
    report.sourceTimestamp = sourceTimestamp > 0 ? sourceTimestamp : QDateTime::currentMSecsSinceEpoch();

    if (object.contains(QStringLiteral("v"))) {
        qint64 version = 0;
        if (!readInteger(object, QStringLiteral("v"), version) || version != protocolVersion) {
            error = QStringLiteral("Unsupported protocol version on %1").arg(topic);
            return false;
        }
    }

    qint64 channelId = 0;
    if (!readInteger(object, QStringLiteral("channelId"), channelId)) {
        error = QStringLiteral("Missing integer channelId field on %1").arg(topic);
        return false;
    }

    report.channelIndex = channelIndexForStatus(channelId, protocol);
    if (report.channelIndex < 0) {
        error = QStringLiteral("channelId out of range on %1").arg(topic);
        return false;
    }

    if (report.detail == QStringLiteral("rpi_controller_online")) {
        report.type = DeviceStatusReportType::ControllerOnline;
        return true;
    }

    bool successful = false;
    if (protocol == StatusProtocol::Controller) {
        const QJsonValue resultValue = object.value(QStringLiteral("result"));
        if (!resultValue.isString()) {
            error = QStringLiteral("Missing string result field on %1").arg(topic);
            return false;
        }

        const QString result = resultValue.toString();
        if (result != QStringLiteral("ok") && result != QStringLiteral("fail")) {
            error = QStringLiteral("Unsupported result value on %1").arg(topic);
            return false;
        }
        successful = result == QStringLiteral("ok");
    } else {
        const QJsonValue okValue = object.value(QStringLiteral("ok"));
        if (!okValue.isBool()) {
            error = QStringLiteral("Missing boolean ok field on %1").arg(topic);
            return false;
        }
        successful = okValue.toBool();
    }

    if (!successful) {
        report.type = DeviceStatusReportType::FeedbackFailed;
        return true;
    }

    const QJsonValue stateValue = object.value(QStringLiteral("state"));
    if (!stateValue.isObject()) {
        error = QStringLiteral("Missing object state field on %1").arg(topic);
        return false;
    }

    if (!parseOutputState(stateValue.toObject(), report.outputs, error)) {
        error = QStringLiteral("%1 on %2").arg(error, topic);
        return false;
    }

    report.type = DeviceStatusReportType::FeedbackConfirmed;
    report.hasOutputState = true;
    return true;
}

bool parseAlivePayload(const QByteArray& payload, const QString& topic, DeviceStatusReport& report, QString& error) {
    static const QRegularExpression topicPattern(QStringLiteral("^veda/ch/([0-3])/alive$"));
    const QRegularExpressionMatch match = topicPattern.match(topic);
    if (!match.hasMatch()) {
        error = QStringLiteral("Invalid sensor alive topic: %1").arg(topic);
        return false;
    }

    const QByteArray state = payload.trimmed();
    if (state != "0" && state != "1") {
        error = QStringLiteral("Alive payload must be 0 or 1 on %1").arg(topic);
        return false;
    }

    report.channelIndex = match.captured(1).toInt();
    report.sourceTimestamp = QDateTime::currentMSecsSinceEpoch();
    report.node = QStringLiteral("compute-server");
    report.type = state == "1" ? DeviceStatusReportType::SensorOnline : DeviceStatusReportType::SensorOffline;
    report.detail = state == "1" ? QStringLiteral("sensor_alive") : QStringLiteral("sensor_lwt_offline");
    return true;
}
}  // namespace

MqttDeviceStatusConfig MqttDeviceStatusConfig::fromEnvironment() {
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    MqttDeviceStatusConfig config;
    config.host = environmentValue(environment, QStringLiteral("VEDA_MQTT_HOST"), QStringLiteral("172.20.27.174"));
    config.caCertificatePath =
        environmentValue(environment, QStringLiteral("VEDA_MQTT_CA_FILE"), QStringLiteral("/etc/veda/certs/ca.crt"));
    config.clientId =
        environmentValue(environment, QStringLiteral("VEDA_MQTT_CLIENT_ID"),
                         QStringLiteral("qt-device-status-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    bool portValid = false;
    const uint configuredPort =
        environment.value(QStringLiteral("VEDA_MQTT_PORT"), QStringLiteral("8883")).toUInt(&portValid);
    config.port = portValid && configuredPort <= 65535 ? static_cast<quint16>(configuredPort) : 8883;
    return config;
}

MqttDeviceStatusGateway::MqttDeviceStatusGateway(MqttDeviceStatusConfig config, QObject* parent)
    : DeviceStatusGateway(parent), config_(std::move(config)) {
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setInterval(reconnectIntervalMsec);
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, &MqttDeviceStatusGateway::connectToBroker);
}

void MqttDeviceStatusGateway::start() {
    if (client_) {
        return;
    }

    stopping_ = false;
    client_ = new QMqttClient(this);
    client_->setHostname(config_.host);
    client_->setPort(config_.port);
    client_->setClientId(config_.clientId);
    client_->setKeepAlive(static_cast<quint16>(config_.keepAliveSeconds));
    client_->setCleanSession(true);

    connect(client_, &QMqttClient::connected, this, [this]() {
        emit brokerConnectionChanged(true);
        subscribeToTopics();
    });
    connect(client_, &QMqttClient::disconnected, this, [this]() {
        emit brokerConnectionChanged(false);
        scheduleReconnect();
    });
    connect(client_, &QMqttClient::messageReceived, this,
            [this](const QByteArray& payload, const QMqttTopicName& topic) { handleMessage(payload, topic.name()); });
    connect(client_, &QMqttClient::errorChanged, this, [this](QMqttClient::ClientError error) {
        if (error != QMqttClient::NoError) {
            emitProtocolError(QStringLiteral("MQTT client error: %1").arg(static_cast<int>(error)));
            scheduleReconnect();
        }
    });

    connectToBroker();
}

void MqttDeviceStatusGateway::stop() {
    stopping_ = true;
    reconnectTimer_->stop();

    if (!client_) {
        return;
    }

    disconnect(client_, nullptr, this, nullptr);
    if (client_->state() != QMqttClient::Disconnected) {
        client_->disconnectFromHost();
    }
    delete client_;
    client_ = nullptr;
    emit brokerConnectionChanged(false);
}

void MqttDeviceStatusGateway::connectToBroker() {
    if (stopping_ || !client_ || client_->state() != QMqttClient::Disconnected) {
        return;
    }

    const QList<QSslCertificate> certificates = QSslCertificate::fromPath(config_.caCertificatePath);
    if (certificates.isEmpty()) {
        emitProtocolError(QStringLiteral("MQTT CA certificate not found or invalid: %1")
                              .arg(QFileInfo(config_.caCertificatePath).absoluteFilePath()));
        scheduleReconnect();
        return;
    }

    QSslConfiguration sslConfiguration = QSslConfiguration::defaultConfiguration();
    sslConfiguration.setCaCertificates(certificates);
    sslConfiguration.setPeerVerifyMode(QSslSocket::VerifyPeer);
    sslConfiguration.setProtocol(QSsl::TlsV1_2OrLater);
    client_->connectToHostEncrypted(sslConfiguration);
}

void MqttDeviceStatusGateway::subscribeToTopics() {
    const struct {
        const char* topic;
        quint8 qos;
    } subscriptions[] = {
        {controllerStatusTopic, statusQos}, {centralStatusTopic, statusQos}, {sensorAliveTopic, statusQos}};

    for (const auto& subscription : subscriptions) {
        if (!client_->subscribe(QString::fromLatin1(subscription.topic), subscription.qos)) {
            emitProtocolError(QStringLiteral("MQTT subscribe failed: %1").arg(QString::fromLatin1(subscription.topic)));
        }
    }
}

void MqttDeviceStatusGateway::scheduleReconnect() {
    if (!stopping_ && client_ && client_->state() == QMqttClient::Disconnected && !reconnectTimer_->isActive()) {
        reconnectTimer_->start();
    }
}

void MqttDeviceStatusGateway::handleMessage(const QByteArray& payload, const QString& topic) {
    DeviceStatusReport report;
    QString error;
    bool parsed = false;

    if (topic == QString::fromLatin1(centralStatusTopic)) {
        parsed = parseStatusPayload(payload, topic, StatusProtocol::Central, report, error);
    } else if (topic.startsWith(QStringLiteral("veda/hw/")) && topic.endsWith(QStringLiteral("/status"))) {
        parsed = parseStatusPayload(payload, topic, StatusProtocol::Controller, report, error);
    } else if (topic.startsWith(QStringLiteral("veda/ch/")) && topic.endsWith(QStringLiteral("/alive"))) {
        parsed = parseAlivePayload(payload, topic, report, error);
    }

    if (!parsed) {
        emitProtocolError(error.isEmpty() ? QStringLiteral("Unsupported MQTT topic: %1").arg(topic) : error);
        return;
    }

    emit reportReceived(std::move(report));
}

void MqttDeviceStatusGateway::emitProtocolError(QString detail) {
    DeviceStatusReport report;
    report.type = DeviceStatusReportType::ProtocolError;
    report.sourceTimestamp = QDateTime::currentMSecsSinceEpoch();
    report.detail = std::move(detail);
    emit reportReceived(std::move(report));
}
