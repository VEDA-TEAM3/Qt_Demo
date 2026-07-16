#pragma once

#include <QMap>
#include <QObject>

#include "model/MqttRealtimeData.h"

class QTimer;

class TopViewFrameDispatcher final : public QObject {
    Q_OBJECT

public:
    explicit TopViewFrameDispatcher(QObject* parent = nullptr);

    void start();
    void stop();
    void submitFrame(TopViewFrameData frame);

signals:
    void frameReady(TopViewFrameData frame);

private:
    void flushPendingFrames();

    QMap<int, TopViewFrameData> pendingFrames_;
    QTimer* flushTimer_ = nullptr;
    bool running_ = false;
};
