#pragma once

#include <QMap>
#include <QObject>

#include "model/MqttRealtimeData.h"

class QTimer;

class BlurFrameDispatcher final : public QObject {
    Q_OBJECT

public:
    explicit BlurFrameDispatcher(QObject* parent = nullptr);

    void start();
    void stop();
    void submitFrame(BlurFrameData frame);

signals:
    void frameReady(BlurFrameData frame);

private:
    void flushPendingFrames();

    QMap<int, BlurFrameData> pendingFrames_;
    QMap<int, qint64> latestSourceTimes_;
    QMap<int, qint64> lastArrivalTimes_;
    QTimer* flushTimer_ = nullptr;
    bool running_ = false;
};
