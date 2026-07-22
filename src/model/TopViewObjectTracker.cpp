#include "model/TopViewObjectTracker.h"

#include <QProcessEnvironment>
#include <QtGlobal>
#include <utility>

namespace {
bool readConfiguredWorldBounds(QRectF& bounds) {
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    bool minXOk = false;
    bool minYOk = false;
    bool maxXOk = false;
    bool maxYOk = false;
    const double minX = environment.value(QStringLiteral("VEDA_MAP_MIN_X")).toDouble(&minXOk);
    const double minY = environment.value(QStringLiteral("VEDA_MAP_MIN_Y")).toDouble(&minYOk);
    const double maxX = environment.value(QStringLiteral("VEDA_MAP_MAX_X")).toDouble(&maxXOk);
    const double maxY = environment.value(QStringLiteral("VEDA_MAP_MAX_Y")).toDouble(&maxYOk);

    if (!minXOk || !minYOk || !maxXOk || !maxYOk || maxX <= minX || maxY <= minY) {
        return false;
    }

    bounds = QRectF(minX, minY, maxX - minX, maxY - minY);
    return true;
}
}  // namespace

/**
 * @brief              TopView 프레임을 디지털 트윈 객체 상태로 변환하는 추적기를 생성합니다.
 * @param channelCount 수신 가능한 채널 수
 */
TopViewObjectTracker::TopViewObjectTracker(int channelCount) : channelCount_(channelCount) {
    hasConfiguredWorldBounds_ = readConfiguredWorldBounds(configuredWorldBounds_);
}

/**
 * @brief 수신 프레임과 객체 이동 이력을 초기화합니다.
 */
void TopViewObjectTracker::reset() {
    frames_.clear();
    frameArrivalTimes_.clear();
    latestSourceTimes_.clear();
    previousPositions_.clear();
    automaticWorldBounds_ = QRectF();
    hasAutomaticWorldBounds_ = false;
}

/**
 * @brief                  채널의 최신 TopView 프레임을 저장합니다.
 * @param frame            검증을 마친 TopView 프레임
 * @param arrivalTimeMsec  로컬 수신 시각
 * @return                 프레임이 최신 상태로 반영되면 true
 */
bool TopViewObjectTracker::submitFrame(TopViewFrameData frame, qint64 arrivalTimeMsec) {
    if (frame.channelIndex < 0 || frame.channelIndex >= channelCount_ || frame.sourceTimestamp <= 0 ||
        arrivalTimeMsec <= 0) {
        return false;
    }

    if (frame.sourceTimestamp <= latestSourceTimes_.value(frame.channelIndex, 0)) {
        return false;
    }

    const int channelIndex = frame.channelIndex;
    latestSourceTimes_.insert(channelIndex, frame.sourceTimestamp);
    frameArrivalTimes_.insert(channelIndex, arrivalTimeMsec);
    frames_.insert(channelIndex, std::move(frame));
    return true;
}

/**
 * @brief                  일정 시간 동안 갱신되지 않은 채널 프레임을 제거합니다.
 * @param currentTimeMsec  현재 로컬 시각
 * @param expiryMsec       프레임 만료 기준 시간
 * @return                 표시 대상 프레임이 제거되면 true
 */
bool TopViewObjectTracker::expireStaleFrames(qint64 currentTimeMsec, qint64 expiryMsec) {
    bool changed = false;

    for (auto iterator = frameArrivalTimes_.begin(); iterator != frameArrivalTimes_.end();) {
        if (currentTimeMsec - iterator.value() <= expiryMsec) {
            ++iterator;
            continue;
        }

        const int channelIndex = iterator.key();
        iterator = frameArrivalTimes_.erase(iterator);
        frames_.remove(channelIndex);
        latestSourceTimes_.remove(channelIndex);
        changed = true;
    }

    return changed;
}

/**
 * @brief                    최신 프레임을 좌표, 속도, 채널 위험 단계를 포함한 스냅샷으로 변환합니다.
 * @param channelRiskLevels  채널별 현재 위험 단계
 * @return                   UI가 바로 렌더링할 수 있는 디지털 트윈 스냅샷
 */
DigitalTwinSnapshot TopViewObjectTracker::buildSnapshot(
    const QVector<DigitalTwinRiskLevel>& channelRiskLevels) {
    updateAutomaticWorldBounds();

    DigitalTwinSnapshot snapshot;
    QHash<QString, QPointF> currentPositions;

    for (auto frameIterator = frames_.cbegin(); frameIterator != frames_.cend(); ++frameIterator) {
        const int channelIndex = frameIterator.key();
        const DigitalTwinRiskLevel riskLevel =
            channelIndex < channelRiskLevels.size() ? channelRiskLevels[channelIndex] : DigitalTwinRiskLevel::Normal;

        for (const TopViewObjectData& sourceObject : frameIterator.value().objects) {
            DigitalTwinObject object;
            object.objectId = QStringLiteral("CH%1-%2").arg(channelIndex + 1).arg(sourceObject.id);
            object.channelIndex = channelIndex;
            object.type = sourceObject.objectClass == QStringLiteral("Human")
                              ? DigitalTwinObjectType::Pedestrian
                              : DigitalTwinObjectType::Vehicle;
            object.position = normalizedWorldPosition(sourceObject.worldPosition);
            object.velocity = object.position - previousPositions_.value(object.objectId, object.position);
            object.riskLevel = riskLevel;
            snapshot.objects.append(object);
            currentPositions.insert(object.objectId, object.position);
        }
    }

    previousPositions_ = std::move(currentPositions);
    return snapshot;
}

/**
 * @brief 설정 좌표 범위가 없을 때 관측 좌표를 누적하여 자동 좌표 범위를 갱신합니다.
 */
void TopViewObjectTracker::updateAutomaticWorldBounds() {
    if (hasConfiguredWorldBounds_) {
        return;
    }

    QVector<QPointF> observedPositions;
    for (auto frameIterator = frames_.cbegin(); frameIterator != frames_.cend(); ++frameIterator) {
        for (const TopViewObjectData& object : frameIterator.value().objects) {
            observedPositions.append(object.worldPosition);
        }
    }

    if (observedPositions.isEmpty()) {
        return;
    }

    double minX = observedPositions.first().x();
    double maxX = minX;
    double minY = observedPositions.first().y();
    double maxY = minY;
    bool normalizedCoordinates = true;

    for (const QPointF& position : observedPositions) {
        minX = qMin(minX, position.x());
        maxX = qMax(maxX, position.x());
        minY = qMin(minY, position.y());
        maxY = qMax(maxY, position.y());
        normalizedCoordinates = normalizedCoordinates && position.x() >= 0.0 && position.x() <= 1.0 &&
                                position.y() >= 0.0 && position.y() <= 1.0;
    }

    QRectF observedBounds;
    if (normalizedCoordinates) {
        observedBounds = QRectF(0.0, 0.0, 1.0, 1.0);
    } else {
        const double width = qMax(1.0, maxX - minX);
        const double height = qMax(1.0, maxY - minY);
        const double horizontalMargin = width * 0.08;
        const double verticalMargin = height * 0.08;
        observedBounds = QRectF(minX - horizontalMargin, minY - verticalMargin, width + horizontalMargin * 2.0,
                                height + verticalMargin * 2.0);
    }

    if (!hasAutomaticWorldBounds_) {
        automaticWorldBounds_ = observedBounds;
        hasAutomaticWorldBounds_ = true;
    } else {
        automaticWorldBounds_ = automaticWorldBounds_.united(observedBounds);
    }
}

/**
 * @brief                 실제 좌표를 0.0~1.0 범위의 지도 좌표로 변환합니다.
 * @param worldPosition   TopView 원본 좌표
 * @return                정규화된 지도 좌표
 */
QPointF TopViewObjectTracker::normalizedWorldPosition(const QPointF& worldPosition) const {
    const QRectF bounds = hasConfiguredWorldBounds_ ? configuredWorldBounds_ : automaticWorldBounds_;
    if (bounds.width() <= 0.0 || bounds.height() <= 0.0) {
        return QPointF(0.5, 0.5);
    }

    return QPointF(qBound(0.0, (worldPosition.x() - bounds.left()) / bounds.width(), 1.0),
                   qBound(0.0, (worldPosition.y() - bounds.top()) / bounds.height(), 1.0));
}
