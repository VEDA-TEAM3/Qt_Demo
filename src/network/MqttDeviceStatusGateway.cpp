#include "network/MqttDeviceStatusGateway.h"

#include "network/TopViewFrameDispatcher.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
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
#include <cmath>
#include <utility>

namespace {
constexpr auto controllerStatusTopic = "veda/hw/+/status";
constexpr auto centralStatusTopic = "veda/hw/status";
constexpr auto sensorAliveTopic = "veda/ch/+/alive";
constexpr auto directTopViewTopic = "veda/ch/+/topview";
constexpr auto relayedTopViewTopic = "veda/qt/ch/+/topview";
constexpr auto centralEventTopic = "veda/qt/event";
constexpr auto visionDetectionsTopic = "veda/vision/+/detections";
constexpr int statusQos = 1;
constexpr int topViewQos = 0;
constexpr int reconnectIntervalMsec = 3000;
constexpr int deviceChannelCount = 4;
constexpr int protocolVersion = 1;
constexpr int topViewDebugLogIntervalMsec = 1000;
constexpr qsizetype maximumDebugPayloadLength = 512;

enum class StatusProtocol {
    Controller,
    Central,
};

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

QString debugPayloadText(const QByteArray& payload) {
    QString text = QString::fromUtf8(payload).simplified();
    if (text.size() > maximumDebugPayloadLength) {
        text = text.left(maximumDebugPayloadLength) + QStringLiteral("...");
    }
    return text;
}

bool isVisionDetectionsTopic(const QString& topic) {
    static const QRegularExpression topicPattern(QStringLiteral("^veda/vision/[1-4]/detections$"));
    return topicPattern.match(topic).hasMatch();
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

int channelIndexForStatus(qint64 channelId) {
    // Both the per-node controller and the fixed central server use wire IDs 1..4.
    return channelId >= 1 && channelId <= deviceChannelCount ? static_cast<int>(channelId - 1) : -1;
}

int channelIndexFromControllerTopic(const QString& topic) {
    static const QRegularExpression topicPattern(
        QStringLiteral("^veda/hw/(?:rpi|ch|channel)?([1-4])/status$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = topicPattern.match(topic);
    return match.hasMatch() ? match.captured(1).toInt() - 1 : -1;
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

    if (object.contains(QStringLiteral("channelId"))) {
        qint64 channelId = 0;
        if (!readInteger(object, QStringLiteral("channelId"), channelId)) {
            error = QStringLiteral("Invalid integer channelId field on %1").arg(topic);
            return false;
        }
        report.channelIndex = channelIndexForStatus(channelId);
    } else if (protocol == StatusProtocol::Controller) {
        report.channelIndex = channelIndexFromControllerTopic(topic);
    }

    if (report.channelIndex < 0) {
        error = QStringLiteral("Missing or out-of-range channelId on %1").arg(topic);
        return false;
    }

    if (report.detail == QStringLiteral("rpi_controller_online") ||
        report.detail == QStringLiteral("controller_online")) {
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
        if (protocol == StatusProtocol::Controller) {
            report.type = DeviceStatusReportType::FeedbackAcknowledged;
            return true;
        }
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

bool readFiniteNumber(const QJsonObject& object, const QString& name, double& value) {
    const QJsonValue jsonValue = object.value(name);
    if (!jsonValue.isDouble() || !std::isfinite(jsonValue.toDouble())) {
        return false;
    }

    value = jsonValue.toDouble();
    return true;
}

bool parseTopViewPayload(const QByteArray& payload, const QString& topic, TopViewFrameData& frame, QString& error) {
    static const QRegularExpression topicPattern(
        QStringLiteral("^veda/(?:qt/)?ch/([0-3])/topview$"));
    const QRegularExpressionMatch match = topicPattern.match(topic);
    if (!match.hasMatch()) {
        error = QStringLiteral("Invalid TopView topic: %1").arg(topic);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Invalid TopView JSON on %1: %2").arg(topic, parseError.errorString());
        return false;
    }

    const QJsonObject object = document.object();
    qint64 version = 0;
    qint64 timestamp = 0;
    qint64 payloadChannel = 0;
    if (!readInteger(object, QStringLiteral("v"), version) || version != protocolVersion ||
        !readInteger(object, QStringLiteral("ts"), timestamp) || timestamp <= 0 ||
        !readInteger(object, QStringLiteral("ch"), payloadChannel)) {
        error = QStringLiteral("Invalid v, ts or ch field on %1").arg(topic);
        return false;
    }

    const int topicChannel = match.captured(1).toInt();
    if (payloadChannel != topicChannel) {
        error = QStringLiteral("TopView topic/payload channel mismatch on %1").arg(topic);
        return false;
    }

    const QJsonValue objectsValue = object.value(QStringLiteral("objects"));
    if (!objectsValue.isArray()) {
        error = QStringLiteral("Missing objects array on %1").arg(topic);
        return false;
    }

    frame.channelIndex = topicChannel;
    frame.sourceTimestamp = timestamp;
    const QJsonArray objects = objectsValue.toArray();
    frame.objects.reserve(objects.size());

    for (const QJsonValue& objectValue : objects) {
        if (!objectValue.isObject()) {
            error = QStringLiteral("TopView objects must be JSON objects on %1").arg(topic);
            return false;
        }

        const QJsonObject sourceObject = objectValue.toObject();
        const QJsonValue classValue = sourceObject.value(QStringLiteral("cls"));
        const QJsonValue positionValue = sourceObject.value(QStringLiteral("pos"));
        qint64 objectId = 0;
        double confidence = 0.0;
        bool edge = false;

        if (!readInteger(sourceObject, QStringLiteral("id"), objectId) || !classValue.isString() ||
            !positionValue.isObject() || !readFiniteNumber(sourceObject, QStringLiteral("conf"), confidence) ||
            confidence < 0.0 || confidence > 1.0 || !readBoolean(sourceObject, QStringLiteral("edge"), edge)) {
            error = QStringLiteral("Invalid TopView object fields on %1").arg(topic);
            return false;
        }

        const QString objectClass = classValue.toString();
        if (objectClass != QStringLiteral("Human") && objectClass != QStringLiteral("Vehicle")) {
            error = QStringLiteral("Unsupported TopView class on %1: %2").arg(topic, objectClass);
            return false;
        }

        const QJsonObject position = positionValue.toObject();
        double x = 0.0;
        double y = 0.0;
        if (!readFiniteNumber(position, QStringLiteral("x"), x) ||
            !readFiniteNumber(position, QStringLiteral("y"), y)) {
            error = QStringLiteral("Invalid TopView world position on %1").arg(topic);
            return false;
        }

        TopViewObjectData parsedObject;
        parsedObject.id = objectId;
        parsedObject.objectClass = objectClass;
        parsedObject.worldPosition = QPointF(x, y);
        parsedObject.confidence = confidence;
        parsedObject.edge = edge;
        frame.objects.append(std::move(parsedObject));
    }

    return true;
}

bool parseCentralEventPayload(const QByteArray& payload, const QString& topic, CentralEventData& event,
                              QString& error) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Invalid central event JSON on %1: %2").arg(topic, parseError.errorString());
        return false;
    }

    const QJsonObject object = document.object();
    qint64 version = 0;
    qint64 channelId = 0;
    qint64 severity = 0;
    qint64 timestamp = 0;
    bool active = false;
    bool hardwareOk = false;
    const QJsonValue eventType = object.value(QStringLiteral("eventType"));
    const QJsonValue hardwareState = object.value(QStringLiteral("hardwareState"));

    if (!readInteger(object, QStringLiteral("v"), version) || version != protocolVersion ||
        !readInteger(object, QStringLiteral("ts"), timestamp) || timestamp <= 0 ||
        !readInteger(object, QStringLiteral("channelId"), channelId) ||
        !readInteger(object, QStringLiteral("severity"), severity) || severity < 0 || severity > 3 ||
        !readBoolean(object, QStringLiteral("active"), active) ||
        !readBoolean(object, QStringLiteral("hardwareOk"), hardwareOk) || !eventType.isString() ||
        eventType.toString().isEmpty() || !hardwareState.isObject()) {
        error = QStringLiteral("Invalid required central event fields on %1").arg(topic);
        return false;
    }

    event.channelIndex = channelIndexForStatus(channelId);
    if (event.channelIndex < 0) {
        error = QStringLiteral("channelId out of range on %1").arg(topic);
        return false;
    }

    if (!parseOutputState(hardwareState.toObject(), event.hardwareState, error)) {
        error = QStringLiteral("%1 on %2").arg(error, topic);
        return false;
    }

    event.sourceTimestamp = timestamp;
    event.eventType = eventType.toString();
    event.active = active;
    event.severity = static_cast<int>(severity);
    event.eventId = object.value(QStringLiteral("eventId")).toString();
    event.source = object.value(QStringLiteral("source")).toString();
    event.hardwareOk = hardwareOk;
    event.detail = object.value(QStringLiteral("detail")).toString();
    return true;
}
}  // namespace

MqttDeviceStatusConfig MqttDeviceStatusConfig::fromEnvironment() {
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    MqttDeviceStatusConfig config;
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

MqttDeviceStatusGateway::MqttDeviceStatusGateway(MqttDeviceStatusConfig config, QObject* parent)
    : DeviceStatusGateway(parent), config_(std::move(config)) {
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setInterval(reconnectIntervalMsec);
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, &MqttDeviceStatusGateway::connectToBroker);

    topViewDispatcher_ = new TopViewFrameDispatcher(this);
    connect(topViewDispatcher_, &TopViewFrameDispatcher::frameReady, this,
            &DeviceStatusGateway::topViewFrameReceived);
}

void MqttDeviceStatusGateway::start() {
    if (client_) {
        return;
    }

    stopping_ = false;
    lastTopViewDebugLogMsec_.fill(0);
    topViewDispatcher_->start();
    client_ = new QMqttClient(this);
    client_->setHostname(config_.host);
    client_->setPort(config_.port);
    client_->setClientId(config_.clientId);
    client_->setKeepAlive(static_cast<quint16>(config_.keepAliveSeconds));
    client_->setCleanSession(true);

    connect(client_, &QMqttClient::connected, this, [this]() {
        if (config_.debugLogging) {
            qInfo().noquote() << QStringLiteral("[MQTT] Connected host=%1 port=%2")
                                     .arg(config_.host)
                                     .arg(config_.port);
        }
        emit brokerConnectionChanged(true);
        subscribeToTopics();
    });
    connect(client_, &QMqttClient::disconnected, this, [this]() {
        if (config_.debugLogging) {
            qInfo().noquote() << QStringLiteral("[MQTT] Disconnected");
        }
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
    topViewDispatcher_->stop();

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
    if (config_.debugLogging) {
        qInfo().noquote() << QStringLiteral("[MQTT] Connecting host=%1 port=%2 clientId=%3")
                                 .arg(config_.host)
                                 .arg(config_.port)
                                 .arg(config_.clientId);
    }
    client_->connectToHostEncrypted(sslConfiguration);
}

void MqttDeviceStatusGateway::subscribeToTopics() {
    const struct {
        const char* topic;
        quint8 qos;
    } subscriptions[] = {{controllerStatusTopic, statusQos},
                         {centralStatusTopic, statusQos},
                         {sensorAliveTopic, statusQos},
                         {directTopViewTopic, topViewQos},
                         {relayedTopViewTopic, topViewQos},
                         {centralEventTopic, statusQos},
                         {visionDetectionsTopic, topViewQos}};

    for (const auto& subscription : subscriptions) {
        if (!client_->subscribe(QString::fromLatin1(subscription.topic), subscription.qos)) {
            emitProtocolError(QStringLiteral("MQTT subscribe failed: %1").arg(QString::fromLatin1(subscription.topic)));
        } else if (config_.debugLogging) {
            qInfo().noquote() << QStringLiteral("[MQTT SUB] topic=%1 qos=%2")
                                     .arg(QString::fromLatin1(subscription.topic))
                                     .arg(static_cast<int>(subscription.qos));
        }
    }
}

void MqttDeviceStatusGateway::scheduleReconnect() {
    if (!stopping_ && client_ && client_->state() == QMqttClient::Disconnected && !reconnectTimer_->isActive()) {
        reconnectTimer_->start();
    }
}

void MqttDeviceStatusGateway::handleMessage(const QByteArray& payload, const QString& topic) {
    if (config_.debugLogging && !topic.endsWith(QStringLiteral("/topview"))) {
        logReceivedMessage(payload, topic);
    }

    if (isVisionDetectionsTopic(topic)) {
        if (config_.debugLogging) {
            qInfo().noquote() << QStringLiteral("[MQTT VISION] Detection payload received: topic=%1 bytes=%2")
                                     .arg(topic)
                                     .arg(payload.size());
        }
        return;
    }

    if (topic == QString::fromLatin1(centralEventTopic)) {
        CentralEventData event;
        QString error;
        if (!parseCentralEventPayload(payload, topic, event, error)) {
            emitProtocolError(error);
            return;
        }

        emit centralEventReceived(event);

        DeviceStatusReport report;
        report.channelIndex = event.channelIndex;
        report.sourceTimestamp = event.sourceTimestamp;
        report.node = QStringLiteral("central-control-server");
        report.detail = event.detail;
        report.type = event.hardwareOk ? DeviceStatusReportType::FeedbackConfirmed
                                       : DeviceStatusReportType::FeedbackFailed;
        report.hasOutputState = event.hardwareOk;
        report.outputs = event.hardwareState;
        emit reportReceived(std::move(report));
        return;
    }

    if (topic.endsWith(QStringLiteral("/topview"))) {
        TopViewFrameData frame;
        QString error;
        if (!parseTopViewPayload(payload, topic, frame, error)) {
            emitProtocolError(error);
            return;
        }

        logTopViewFrame(topic, frame);
        topViewDispatcher_->submitFrame(std::move(frame));
        return;
    }

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

/**
 * @brief          상태 및 이벤트 MQTT 메시지를 읽기 쉬운 텍스트로 출력합니다.
 * @param payload  MQTT 페이로드
 * @param topic    수신한 MQTT 토픽
 */
void MqttDeviceStatusGateway::logReceivedMessage(const QByteArray& payload, const QString& topic) const {
    qInfo().noquote() << QStringLiteral("[MQTT RX] topic=%1 bytes=%2 payload=%3")
                             .arg(topic)
                             .arg(payload.size())
                             .arg(debugPayloadText(payload));
}

/**
 * @brief        고빈도 TopView 수신 상태를 채널별 제한 주기로 출력합니다.
 * @param topic  수신한 MQTT 토픽
 * @param frame  검증을 통과한 TopView 프레임
 */
void MqttDeviceStatusGateway::logTopViewFrame(const QString& topic, const TopViewFrameData& frame) {
    if (!config_.debugLogging || frame.channelIndex < 0 || frame.channelIndex >= deviceChannelCount) {
        return;
    }

    const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
    qint64& lastLogMsec = lastTopViewDebugLogMsec_[static_cast<std::size_t>(frame.channelIndex)];
    if (lastLogMsec > 0 && nowMsec - lastLogMsec < topViewDebugLogIntervalMsec) {
        return;
    }

    lastLogMsec = nowMsec;
    qInfo().noquote() << QStringLiteral("[MQTT TOPVIEW] topic=%1 channel=%2 ts=%3 objects=%4")
                             .arg(topic)
                             .arg(frame.channelIndex)
                             .arg(frame.sourceTimestamp)
                             .arg(frame.objects.size());
}

void MqttDeviceStatusGateway::emitProtocolError(QString detail) {
    if (config_.debugLogging) {
        qWarning().noquote() << QStringLiteral("[MQTT ERROR] %1").arg(detail);
    }

    DeviceStatusReport report;
    report.type = DeviceStatusReportType::ProtocolError;
    report.sourceTimestamp = QDateTime::currentMSecsSinceEpoch();
    report.detail = std::move(detail);
    emit reportReceived(std::move(report));
}
