#include "DigitalTwinSimulationWorker.h"

#include <QRandomGenerator>
#include <QtGlobal>
#include <algorithm>

namespace {
constexpr int updateIntervalMsec = 200;
constexpr int dangerHoldTickCount = 3;
constexpr double objectMinX = 0.08;
constexpr double objectMaxX = 0.92;
constexpr double objectMinY = 0.14;
constexpr double objectMaxY = 0.86;
constexpr double maxVelocityPerTick = 0.026;
constexpr double collisionAxisThreshold = 0.05;

double randomRange(double minimumValue, double maximumValue) {
    return minimumValue + (maximumValue - minimumValue) * QRandomGenerator::global()->generateDouble();
}

double simulationAbsoluteValue(double value) {
    return value < 0.0 ? -value : value;
}

QPointF limitedVelocity(const QPointF& velocity) {
    const double maxComponent = std::max(simulationAbsoluteValue(velocity.x()), simulationAbsoluteValue(velocity.y()));

    if (maxComponent <= maxVelocityPerTick || qFuzzyIsNull(maxComponent)) {
        return velocity;
    }

    const double scale = maxVelocityPerTick / maxComponent;
    return QPointF(velocity.x() * scale, velocity.y() * scale);
}

bool isCollisionCandidate(const DigitalTwinObject& firstObject, const DigitalTwinObject& secondObject) {
    const double xDistance = simulationAbsoluteValue(firstObject.position.x() - secondObject.position.x());
    const double yDistance = simulationAbsoluteValue(firstObject.position.y() - secondObject.position.y());

    return xDistance <= collisionAxisThreshold && yDistance <= collisionAxisThreshold;
}
}  // namespace

DigitalTwinSimulationWorker::DigitalTwinSimulationWorker(QObject* parent) : QObject(parent) {}

void DigitalTwinSimulationWorker::start() {
    ensureTimer();
    setupDemoObjects();
    updateDangerStates();
    emitCurrentObjects();

    if (!updateTimer_->isActive()) {
        updateTimer_->start();
    }
}

void DigitalTwinSimulationWorker::stop() {
    if (updateTimer_) {
        updateTimer_->stop();
    }
}

void DigitalTwinSimulationWorker::updateObjects() {
    for (auto& object : objects_) {
        updateObjectMotion(&object);
    }

    updateDangerStates();
    emitCurrentObjects();
}

void DigitalTwinSimulationWorker::setupDemoObjects() {
    objects_ = {
        {QStringLiteral("V-102"), DigitalTwinObjectType::Vehicle, QPointF(0.20, 0.42), QPointF(0.018, 0.006),
         QColor(QStringLiteral("#23d8ff"))},
        {QStringLiteral("P-205"), DigitalTwinObjectType::Pedestrian, QPointF(0.245, 0.455), QPointF(-0.008, 0.014),
         QColor(QStringLiteral("#45f23a"))},
        {QStringLiteral("M-301"), DigitalTwinObjectType::Motorcycle, QPointF(0.68, 0.36), QPointF(0.012, -0.010),
         QColor(QStringLiteral("#f5ed00"))},
        {QStringLiteral("V-098"), DigitalTwinObjectType::Vehicle, QPointF(0.78, 0.64), QPointF(-0.016, -0.004),
         QColor(QStringLiteral("#23d8ff"))},
        {QStringLiteral("P-210"), DigitalTwinObjectType::Pedestrian, QPointF(0.34, 0.70), QPointF(0.010, -0.012),
         QColor(QStringLiteral("#45f23a"))},
    };

    dangerHoldTicks_.fill(0, objects_.size());
}

void DigitalTwinSimulationWorker::ensureTimer() {
    if (updateTimer_) {
        return;
    }

    updateTimer_ = std::make_shared<QTimer>();
    updateTimer_->setInterval(updateIntervalMsec);
    updateTimer_->setTimerType(Qt::PreciseTimer);

    connect(updateTimer_.get(), &QTimer::timeout, this, &DigitalTwinSimulationWorker::updateObjects);
}

void DigitalTwinSimulationWorker::updateObjectMotion(DigitalTwinObject* object) {
    if (!object) {
        return;
    }

    if (QRandomGenerator::global()->bounded(100) < 24) {
        object->velocity =
            limitedVelocity(object->velocity + QPointF(randomRange(-0.010, 0.010), randomRange(-0.010, 0.010)));
    }

    QPointF nextPosition = object->position + object->velocity;

    if (nextPosition.x() < objectMinX || nextPosition.x() > objectMaxX) {
        object->velocity.setX(-object->velocity.x());
        nextPosition.setX(std::clamp(nextPosition.x(), objectMinX, objectMaxX));
    }

    if (nextPosition.y() < objectMinY || nextPosition.y() > objectMaxY) {
        object->velocity.setY(-object->velocity.y());
        nextPosition.setY(std::clamp(nextPosition.y(), objectMinY, objectMaxY));
    }

    object->position = nextPosition;
}

void DigitalTwinSimulationWorker::updateDangerStates() {
    if (dangerHoldTicks_.size() != objects_.size()) {
        dangerHoldTicks_.fill(0, objects_.size());
    }

    for (int& tickCount : dangerHoldTicks_) {
        tickCount = std::max(0, tickCount - 1);
    }

    for (int firstIndex = 0; firstIndex < objects_.size(); ++firstIndex) {
        for (int secondIndex = firstIndex + 1; secondIndex < objects_.size(); ++secondIndex) {
            if (!isCollisionCandidate(objects_[firstIndex], objects_[secondIndex])) {
                continue;
            }

            dangerHoldTicks_[firstIndex] = dangerHoldTickCount;
            dangerHoldTicks_[secondIndex] = dangerHoldTickCount;
        }
    }

    for (int i = 0; i < objects_.size(); ++i) {
        objects_[i].isDanger = dangerHoldTicks_[i] > 0;
    }
}

void DigitalTwinSimulationWorker::emitCurrentObjects() {
    emit objectsUpdated(objects_);
}
