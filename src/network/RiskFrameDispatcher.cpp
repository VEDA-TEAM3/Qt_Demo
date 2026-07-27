#include "network/RiskFrameDispatcher.h"

#include <QDateTime>
#include <QTimer>
#include <utility>

namespace {
constexpr int frameFlushIntervalMsec = 50;
constexpr int sourceRestartGapMsec = 5000;
}  // namespace

/**
 * @brief         통합 위험 프레임의 최신값 병합 디스패처를 생성합니다.
 * @param parent  Qt 객체 소유권을 연결할 부모 객체
 */
RiskFrameDispatcher::RiskFrameDispatcher(QObject* parent) : QObject(parent) {
    flushTimer_ = new QTimer(this);
    flushTimer_->setInterval(frameFlushIntervalMsec);
    flushTimer_->setSingleShot(true);
    flushTimer_->setTimerType(Qt::PreciseTimer);
    connect(flushTimer_, &QTimer::timeout, this, &RiskFrameDispatcher::flushPendingFrame);
}

/** @brief 위험 프레임 전달을 시작합니다. */
void RiskFrameDispatcher::start() { running_ = true; }

/** @brief 예약된 위험 프레임과 시간 상태를 정리합니다. */
void RiskFrameDispatcher::stop() {
    running_ = false;
    flushTimer_->stop();
    pendingFrame_ = {};
    latestSourceTimestamp_ = 0;
    lastArrivalMsec_ = 0;
    hasPendingFrame_ = false;
}

/**
 * @brief        가장 최신인 통합 위험 프레임만 전달 대기열에 보관합니다.
 * @param frame  계약 검증을 통과한 위험 프레임
 */
void RiskFrameDispatcher::submitFrame(RiskFrameData frame) {
    if (!running_ || frame.sourceTimestamp <= 0) {
        return;
    }

    const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
    if (lastArrivalMsec_ > 0 && nowMsec - lastArrivalMsec_ > sourceRestartGapMsec) {
        latestSourceTimestamp_ = 0;
    }
    lastArrivalMsec_ = nowMsec;

    if (frame.sourceTimestamp <= latestSourceTimestamp_) {
        return;
    }

    latestSourceTimestamp_ = frame.sourceTimestamp;
    pendingFrame_ = std::move(frame);
    hasPendingFrame_ = true;
    if (!flushTimer_->isActive()) {
        flushTimer_->start();
    }
}

/** @brief 현재 전달 구간에서 가장 최신인 위험 프레임 하나를 방출합니다. */
void RiskFrameDispatcher::flushPendingFrame() {
    if (!hasPendingFrame_) {
        return;
    }

    hasPendingFrame_ = false;
    emit frameReady(std::exchange(pendingFrame_, {}));
}
