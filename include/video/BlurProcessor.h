#pragma once

#include <gst/video/video-frame.h>

#include <QMutex>
#include <QRectF>
#include <QVector>
#include <atomic>
#include <vector>

#include "model/MqttRealtimeData.h"

class BlurProcessor final {
public:
    explicit BlurProcessor(qint64 defaultSyncOffsetMsec);

    void setTargetsEnabled(bool faceEnabled, bool licensePlateEnabled);
    void submitFrame(BlurFrameData frame);
    void clear();
    void apply(GstVideoFrame& frame);

private:
    QVector<QRectF> regionsFor(qint64 sourceTimestamp) const;

    qint64 syncOffsetMsec_ = 0;
    mutable QMutex mutex_;
    QVector<BlurFrameData> history_;
    std::vector<guint8> scratch_;
    std::atomic_int channelIndex_{-1};
    std::atomic<qint64> sourceToLocalOffsetMsec_{0};
    std::atomic_bool clockOffsetReady_{false};
    std::atomic_bool faceEnabled_{true};
    std::atomic_bool licensePlateEnabled_{true};
    std::atomic<qint64> lastApplyLogMsec_{0};
};
