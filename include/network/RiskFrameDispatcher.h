#pragma once

#include <QObject>

#include "model/MqttRealtimeData.h"

class QTimer;

class RiskFrameDispatcher final : public QObject {
    Q_OBJECT

public:
    explicit RiskFrameDispatcher(QObject* parent = nullptr);

    void start();
    void stop();
    void submitFrame(RiskFrameData frame);

signals:
    void frameReady(RiskFrameData frame);

private:
    void flushPendingFrame();

    RiskFrameData pendingFrame_;
    QTimer* flushTimer_ = nullptr;
    qint64 latestSourceTimestamp_ = 0;
    qint64 lastArrivalMsec_ = 0;
    bool hasPendingFrame_ = false;
    bool running_ = false;
};
