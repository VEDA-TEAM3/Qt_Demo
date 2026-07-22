#include "network/TopViewMessageParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <utility>

namespace {
constexpr int topViewChannelCount = 4;
constexpr int topViewProtocolVersion = 1;

/**
 * @brief        JSON 객체에서 손실 없는 정수 값을 읽습니다.
 * @param object JSON 객체
 * @param name   필드 이름
 * @param value  읽은 정수
 * @return       유효한 정수이면 true
 */
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

/**
 * @brief        JSON 객체에서 유한한 실수 값을 읽습니다.
 * @param object JSON 객체
 * @param name   필드 이름
 * @param value  읽은 실수
 * @return       유효한 실수이면 true
 */
bool readFiniteNumber(const QJsonObject& object, const QString& name, double& value) {
    const QJsonValue jsonValue = object.value(name);
    if (!jsonValue.isDouble() || !std::isfinite(jsonValue.toDouble())) {
        return false;
    }

    value = jsonValue.toDouble();
    return true;
}

/**
 * @brief        JSON 객체에서 불리언 값을 읽습니다.
 * @param object JSON 객체
 * @param name   필드 이름
 * @param value  읽은 불리언 값
 * @return       유효한 불리언이면 true
 */
bool readBoolean(const QJsonObject& object, const QString& name, bool& value) {
    const QJsonValue jsonValue = object.value(name);
    if (!jsonValue.isBool()) {
        return false;
    }

    value = jsonValue.toBool();
    return true;
}
}  // namespace

/**
 * @brief       토픽이 채널별 TopView 데이터 토픽인지 확인합니다.
 * @param topic MQTT 토픽
 * @return      지원하는 TopView 토픽이면 true
 */
bool TopViewMessageParser::matchesTopic(const QString& topic) {
    static const QRegularExpression topicPattern(QStringLiteral("^veda/(?:qt/)?ch/[0-3]/topview$"));
    return topicPattern.match(topic).hasMatch();
}

/**
 * @brief         공유 wire contract의 TopViewFrame payload를 내부 모델로 변환합니다.
 * @param payload MQTT JSON payload
 * @param topic   수신 토픽
 * @param frame   변환된 TopView 프레임
 * @param error   실패 원인
 * @return        변환에 성공하면 true
 */
bool TopViewMessageParser::parse(const QByteArray& payload, const QString& topic, TopViewFrameData& frame,
                                 QString& error) {
    static const QRegularExpression topicPattern(QStringLiteral("^veda/(?:qt/)?ch/([0-3])/topview$"));
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

    const QJsonObject root = document.object();
    qint64 version = 0;
    qint64 timestamp = 0;
    qint64 payloadChannel = 0;
    if (!readInteger(root, QStringLiteral("v"), version) || version != topViewProtocolVersion ||
        !readInteger(root, QStringLiteral("ts"), timestamp) || timestamp <= 0 ||
        !readInteger(root, QStringLiteral("ch"), payloadChannel) || payloadChannel < 0 ||
        payloadChannel >= topViewChannelCount) {
        error = QStringLiteral("Invalid v, ts or ch field on %1").arg(topic);
        return false;
    }

    const int topicChannel = match.captured(1).toInt();
    if (payloadChannel != topicChannel) {
        error = QStringLiteral("TopView topic/payload channel mismatch on %1").arg(topic);
        return false;
    }

    const QJsonValue objectsValue = root.value(QStringLiteral("objects"));
    if (!objectsValue.isArray()) {
        error = QStringLiteral("Missing objects array on %1").arg(topic);
        return false;
    }

    frame = {};
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
        bool edge = false;
        if (!readInteger(sourceObject, QStringLiteral("id"), objectId) || !classValue.isString() ||
            !positionValue.isObject() || !readBoolean(sourceObject, QStringLiteral("edge"), edge)) {
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
        parsedObject.edge = edge;
        frame.objects.append(std::move(parsedObject));
    }

    return true;
}
