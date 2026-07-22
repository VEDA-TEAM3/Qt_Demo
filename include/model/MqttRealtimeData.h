#pragma once

#include <QMetaType>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include "model/DeviceStatus.h"

struct TopViewObjectData {
    qint64 id = 0;
    QString objectClass;
    QPointF worldPosition;
    double confidence = 0.0;
    bool edge = false;
};

struct TopViewFrameData {
    int channelIndex = -1;
    qint64 sourceTimestamp = 0;
    QVector<TopViewObjectData> objects;
};

enum class BlurTargetType {
    Face,
    LicensePlate,
};

struct BlurRegionData {
    qint64 id = 0;
    BlurTargetType targetType = BlurTargetType::Face;
    QRectF normalizedBox;
};

struct BlurFrameData {
    int channelIndex = -1;
    qint64 sourceTimestamp = 0;
    QVector<BlurRegionData> regions;
};

struct CentralEventData {
    int channelIndex = -1;
    qint64 sourceTimestamp = 0;
    QString eventType;
    bool active = false;
    int severity = 0;
    QString eventId;
    QString source;
    bool hardwareOk = false;
    QString detail;
    DeviceOutputState hardwareState;
};

Q_DECLARE_METATYPE(TopViewObjectData)
Q_DECLARE_METATYPE(TopViewFrameData)
Q_DECLARE_METATYPE(BlurTargetType)
Q_DECLARE_METATYPE(BlurRegionData)
Q_DECLARE_METATYPE(BlurFrameData)
Q_DECLARE_METATYPE(CentralEventData)
Q_DECLARE_METATYPE(QVector<TopViewObjectData>)
