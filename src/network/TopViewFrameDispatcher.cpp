#include "network/TopViewFrameDispatcher.h"

#include <QTimer>
#include <utility>

namespace {
constexpr int frameFlushIntervalMsec = 100;
}

/**
 * @brief         고빈도 TopView 프레임을 채널별 최신 값으로 병합하는 디스패처를 생성합니다.
 * @param parent  Qt 객체 소유권을 연결할 부모 객체
 */
TopViewFrameDispatcher::TopViewFrameDispatcher(QObject* parent) : QObject(parent) {
    flushTimer_ = new QTimer(this);
    flushTimer_->setInterval(frameFlushIntervalMsec);
    flushTimer_->setSingleShot(true);
    flushTimer_->setTimerType(Qt::CoarseTimer);
    connect(flushTimer_, &QTimer::timeout, this, &TopViewFrameDispatcher::flushPendingFrames);
}

/**
 * @brief 작업자 스레드에서 프레임 병합을 시작합니다.
 */
void TopViewFrameDispatcher::start() { running_ = true; }

/**
 * @brief 예약된 갱신과 대기 중인 프레임을 정리합니다.
 */
void TopViewFrameDispatcher::stop() {
    running_ = false;
    flushTimer_->stop();
    pendingFrames_.clear();
}

/**
 * @brief        채널별 최신 프레임을 저장하고 한 번의 전달 작업을 예약합니다.
 * @param frame  검증을 마친 TopView 프레임
 */
void TopViewFrameDispatcher::submitFrame(TopViewFrameData frame) {
    if (!running_ || frame.channelIndex < 0) {
        return;
    }

    const auto pending = pendingFrames_.constFind(frame.channelIndex);
    if (pending != pendingFrames_.cend() && pending->sourceTimestamp >= frame.sourceTimestamp) {
        return;
    }

    pendingFrames_.insert(frame.channelIndex, std::move(frame));
    if (!flushTimer_->isActive()) {
        flushTimer_->start();
    }
}

/**
 * @brief 같은 주기에 수신한 각 채널의 최신 프레임만 UI 경로로 전달합니다.
 */
void TopViewFrameDispatcher::flushPendingFrames() {
    QMap<int, TopViewFrameData> frames;
    pendingFrames_.swap(frames);

    for (auto iterator = frames.begin(); iterator != frames.end(); ++iterator) {
        emit frameReady(std::move(iterator.value()));
    }
}
