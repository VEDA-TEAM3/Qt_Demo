#include "network/BlurFrameDispatcher.h"

#include <QTimer>
#include <utility>

namespace {
constexpr int frameFlushIntervalMsec = 10;
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
}

/**
 * @brief        채널의 최신 블러 프레임을 저장하고 전달을 예약합니다.
 * @param frame  파싱과 채널 검증을 마친 블러 프레임
 */
void BlurFrameDispatcher::submitFrame(BlurFrameData frame) {
    if (!running_ || frame.channelIndex < 0 || frame.sourceTimestamp <= 0) {
        return;
    }

    if (frame.sourceTimestamp <= latestSourceTimes_.value(frame.channelIndex, 0)) {
        return;
    }

    const int channelIndex = frame.channelIndex;
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
