#include "network/TopViewFrameDispatcher.h"

#include <QDateTime>
#include <QTimer>
#include <utility>

namespace {
constexpr int frameFlushIntervalMsec = 100;
constexpr int sourceRestartGapMsec = 5000;
}

/**
 * @brief         고빈도 TopView 프레임을 채널별 최신 값으로 병합하는 디스패처를 생성합니다.
 * @param parent  Qt 객체 소유권을 연결할 부모 객체
 */
TopViewFrameDispatcher::TopViewFrameDispatcher(QObject* parent) : QObject(parent) {
    flushTimer_ = new QTimer(this);
    flushTimer_->setInterval(frameFlushIntervalMsec);
    flushTimer_->setSingleShot(true);
    flushTimer_->setTimerType(Qt::PreciseTimer);
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
    latestSourceTimes_.clear();
    lastArrivalTimes_.clear();
}

/**
 * @brief        채널별 최신 프레임을 저장하고 한 번의 전달 작업을 예약합니다.
 * @param frame  검증을 마친 TopView 프레임
 */
void TopViewFrameDispatcher::submitFrame(TopViewFrameData frame) {
    if (!running_ || frame.channelIndex < 0) {
        return;
    }

    const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
    const int channelIndex = frame.channelIndex;
    const qint64 lastArrivalMsec = lastArrivalTimes_.value(channelIndex, 0);
    if (lastArrivalMsec > 0 && nowMsec - lastArrivalMsec > sourceRestartGapMsec) {
        latestSourceTimes_.remove(channelIndex);
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
 * @brief 같은 주기에 수신한 각 채널의 최신 프레임만 UI 경로로 전달합니다.
 */
void TopViewFrameDispatcher::flushPendingFrames() {
    QMap<int, TopViewFrameData> frames;
    pendingFrames_.swap(frames);

    for (auto iterator = frames.begin(); iterator != frames.end(); ++iterator) {
        emit frameReady(std::move(iterator.value()));
    }
}
