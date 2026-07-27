#include "network/DeviceStatusTopicHandler.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <utility>

namespace {
constexpr int deviceChannelCount = 4;
constexpr int protocolVersion = 1;
constexpr auto controllerStatusTopic = "veda/hw/ch/+/status";
constexpr auto centralStatusTopic = "veda/hw/status";
constexpr auto sensorAliveTopic = "veda/ch/+/alive";
constexpr auto centralEventTopic = "veda/qt/event";

enum class StatusProtocol {
    Controller,
    Central,
};

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

int channelIndexForCentralStatus(qint64 channelId) {
    return channelId >= 1 && channelId <= deviceChannelCount ? static_cast<int>(channelId - 1) : -1;
}

int channelIndexForControllerStatus(qint64 channelId) {
    return channelId >= 0 && channelId < deviceChannelCount ? static_cast<int>(channelId) : -1;
}

int channelIndexFromControllerTopic(const QString& topic) {
    static const QRegularExpression topicPattern(QStringLiteral("^veda/hw/ch/([0-3])/status$"),
                                                 QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = topicPattern.match(topic);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
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

    const int topicChannelIndex = protocol == StatusProtocol::Controller ? channelIndexFromControllerTopic(topic) : -1;
    if (object.contains(QStringLiteral("channelId"))) {
        qint64 channelId = 0;
        if (!readInteger(object, QStringLiteral("channelId"), channelId)) {
            error = QStringLiteral("Invalid integer channelId field on %1").arg(topic);
            return false;
        }

        report.channelIndex = protocol == StatusProtocol::Controller ? channelIndexForControllerStatus(channelId)
                                                                     : channelIndexForCentralStatus(channelId);
        if (protocol == StatusProtocol::Controller && topicChannelIndex >= 0 &&
            report.channelIndex != topicChannelIndex) {
            error = QStringLiteral("Topic/payload channel mismatch on %1").arg(topic);
            return false;
        }
    } else if (protocol == StatusProtocol::Controller) {
        report.channelIndex = topicChannelIndex;
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

    event.channelIndex = channelIndexForCentralStatus(channelId);
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

/** @brief 장비 상태, 센서 생존 및 중앙 이벤트 구독 목록을 반환합니다. */
QVector<MqttSubscription> DeviceStatusTopicHandler::subscriptions() const {
    return {{QString::fromLatin1(controllerStatusTopic), 1},
            {QString::fromLatin1(centralStatusTopic), 1},
            {QString::fromLatin1(sensorAliveTopic), 1},
            {QString::fromLatin1(centralEventTopic), 1}};
}

/** @brief 수신 토픽이 장비 상태 계열 계약에 속하는지 확인합니다. */
bool DeviceStatusTopicHandler::matchesTopic(const QString& topic) const {
    static const QRegularExpression controllerPattern(QStringLiteral("^veda/hw/ch/[0-3]/status$"));
    static const QRegularExpression alivePattern(QStringLiteral("^veda/ch/[0-3]/alive$"));
    return topic == QString::fromLatin1(centralStatusTopic) || topic == QString::fromLatin1(centralEventTopic) ||
           controllerPattern.match(topic).hasMatch() || alivePattern.match(topic).hasMatch();
}

/** @brief 상태 계열 payload를 UI 독립 도메인 보고와 중앙 이벤트로 변환합니다. */
bool DeviceStatusTopicHandler::handle(const QByteArray& payload, const QString& topic, MqttMessageBatch& messages,
                                      QString& error) const {
    if (topic == QString::fromLatin1(centralEventTopic)) {
        CentralEventData event;
        if (!parseCentralEventPayload(payload, topic, event, error)) {
            return false;
        }

        DeviceStatusReport report;
        report.channelIndex = event.channelIndex;
        report.sourceTimestamp = event.sourceTimestamp;
        report.node = QStringLiteral("central-control-server");
        report.detail = event.detail;
        report.type =
            event.hardwareOk ? DeviceStatusReportType::FeedbackConfirmed : DeviceStatusReportType::FeedbackFailed;
        report.hasOutputState = event.hardwareOk;
        report.outputs = event.hardwareState;
        messages.centralEvents.append(std::move(event));
        messages.reports.append(std::move(report));
        return true;
    }

    DeviceStatusReport report;
    bool parsed = false;
    if (topic == QString::fromLatin1(centralStatusTopic)) {
        parsed = parseStatusPayload(payload, topic, StatusProtocol::Central, report, error);
    } else if (topic.startsWith(QStringLiteral("veda/hw/")) && topic.endsWith(QStringLiteral("/status"))) {
        parsed = parseStatusPayload(payload, topic, StatusProtocol::Controller, report, error);
    } else if (topic.startsWith(QStringLiteral("veda/ch/")) && topic.endsWith(QStringLiteral("/alive"))) {
        parsed = parseAlivePayload(payload, topic, report, error);
    }

    if (!parsed) {
        if (error.isEmpty()) {
            error = QStringLiteral("Unsupported device status topic: %1").arg(topic);
        }
        return false;
    }

    messages.reports.append(std::move(report));
    return true;
}
