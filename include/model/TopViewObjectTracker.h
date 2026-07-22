#pragma once

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include "model/DigitalTwinTypes.h"
#include "model/MqttRealtimeData.h"

class TopViewObjectTracker final {
public:
    explicit TopViewObjectTracker(int channelCount);

    void reset();
    bool submitFrame(TopViewFrameData frame, qint64 arrivalTimeMsec);
    bool expireStaleFrames(qint64 currentTimeMsec, qint64 expiryMsec);
    DigitalTwinSnapshot buildSnapshot(const QVector<DigitalTwinRiskLevel>& channelRiskLevels);

private:
    void updateAutomaticWorldBounds();
    QPointF normalizedWorldPosition(const QPointF& worldPosition) const;

    int channelCount_ = 0;
    QHash<int, TopViewFrameData> frames_;
    QHash<int, qint64> frameArrivalTimes_;
    QHash<int, qint64> latestSourceTimes_;
    QHash<QString, QPointF> previousPositions_;
    QRectF configuredWorldBounds_;
    QRectF automaticWorldBounds_;
    bool hasConfiguredWorldBounds_ = false;
    bool hasAutomaticWorldBounds_ = false;
};
