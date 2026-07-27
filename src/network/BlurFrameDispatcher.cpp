#include "network/BlurFrameDispatcher.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <utility>

namespace {
constexpr int frameFlushIntervalMsec = 10;
constexpr int sourceRestartGapMsec = 5000;
constexpr qint64 sourceTimestampRestartThresholdMsec = 2000;
}

/**
 * @brief         채널별 최신 블러 프레임만 전달하는 병합기를 생성합니다.
 * @param parent  Qt 객체 소유권을 연결할 부모 객체
 */
BlurFrameDispatcher::BlurFrameDispatcher(QObject* parent) : QObject(parent) {
    flushTimer_ = new QTimer(this);
    flushTimer_->setInterval(frameFlushIntervalMsec);
    flushTimer_->setSingleShot(true);
    flushTimer_->setTimerType(Qt::PreciseTimer);
    connect(flushTimer_, &QTimer::timeout, this, &BlurFrameDispatcher::flushPendingFrames);
}

/**
 * @brief 블러 프레임 병합을 시작합니다.
 */
void BlurFrameDispatcher::start() { running_ = true; }

/**
 * @brief 예약된 블러 프레임과 timestamp 이력을 정리합니다.
 */
void BlurFrameDispatcher::stop() {
    running_ = false;
    flushTimer_->stop();
    pendingFrames_.clear();
    latestSourceTimes_.clear();
    lastArrivalTimes_.clear();
}

/**
 * @brief        채널의 최신 블러 프레임을 저장하고 전달을 예약합니다.
 * @param frame  파싱과 채널 검증을 마친 블러 프레임
 */
void BlurFrameDispatcher::submitFrame(BlurFrameData frame) {
    if (!running_ || frame.channelIndex < 0 || frame.sourceTimestamp <= 0) {
        return;
    }

    const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
    const int channelIndex = frame.channelIndex;
    const qint64 lastArrivalMsec = lastArrivalTimes_.value(channelIndex, 0);
    const qint64 latestSourceTimestamp = latestSourceTimes_.value(channelIndex, 0);
    const bool arrivalRestart = lastArrivalMsec > 0 && nowMsec - lastArrivalMsec > sourceRestartGapMsec;
    const bool timestampRestart = latestSourceTimestamp > frame.sourceTimestamp &&
                                  latestSourceTimestamp - frame.sourceTimestamp >=
                                      sourceTimestampRestartThresholdMsec;
    if (arrivalRestart || timestampRestart) {
        latestSourceTimes_.remove(channelIndex);
        pendingFrames_.remove(channelIndex);

        if (timestampRestart) {
            qWarning().noquote()
                << QStringLiteral("[MQTT BLUR] Channel %1 source timestamp restarted: %2 -> %3")
                       .arg(channelIndex)
                       .arg(latestSourceTimestamp)
                       .arg(frame.sourceTimestamp);
        }
    }
    lastArrivalTimes_.insert(channelIndex, nowMsec);

    if (frame.sourceTimestamp <= latestSourceTimes_.value(channelIndex, 0)) {
        return;
    }

    latestSourceTimes_.insert(channelIndex, frame.sourceTimestamp);
    pendingFrames_.insert(channelIndex, std::move(frame));

    if (!flushTimer_->isActive()) {
        flushTimer_->start();
    }
}

/**
 * @brief 같은 주기에 수신된 각 채널의 최신 프레임을 독립적으로 전달합니다.
 */
void BlurFrameDispatcher::flushPendingFrames() {
    QMap<int, BlurFrameData> frames;
    pendingFrames_.swap(frames);

    for (auto iterator = frames.begin(); iterator != frames.end(); ++iterator) {
        emit frameReady(std::move(iterator.value()));
    }
}
