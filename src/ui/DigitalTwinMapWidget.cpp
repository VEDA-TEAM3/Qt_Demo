#include "ui/DigitalTwinMapWidget.h"

#include <QFrame>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsSimpleTextItem>
#include <QMetaObject>
#include <QMetaType>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QResizeEvent>
#include <QSet>
#include <QSize>
#include <QThread>
#include <QtGlobal>
#include <cmath>
#include <memory>

#include "model/DigitalTwinSimulationWorker.h"
#include "ui/DigitalTwinMapSceneBuilder.h"
#include "ui/DigitalTwinObjectStyleProvider.h"

namespace {
constexpr int maxTrailPointCount = 96;
constexpr double maxTrailSceneLength = 240.0;
constexpr double movingIconRotationOffsetDegrees = 90.0;

/**
 * @brief        실수 값의 절댓값을 반환합니다.
 * @param value  입력 값
 * @return       절댓값
 */
double absoluteValue(double value) {
    return value < 0.0 ? -value : value;
}

/**
 * @brief           객체 속도 벡터를 화면 아이콘 회전 각도로 근사합니다.
 * @param velocity  정규화 좌표계 기준 이동 벡터
 * @return          degree 단위 회전 각도
 */
double approximateRotationDegrees(const QPointF& velocity) {
    if (qFuzzyIsNull(velocity.x()) && qFuzzyIsNull(velocity.y())) {
        return 0.0;
    }

    const double absoluteX = absoluteValue(velocity.x());
    const double absoluteY = absoluteValue(velocity.y());

    if (absoluteX > absoluteY * 2.0) {
        return velocity.x() >= 0.0 ? 0.0 : 180.0;
    }

    if (absoluteY > absoluteX * 2.0) {
        return velocity.y() >= 0.0 ? 90.0 : 270.0;
    }

    if (velocity.x() >= 0.0 && velocity.y() >= 0.0) {
        return 45.0;
    }

    if (velocity.x() < 0.0 && velocity.y() >= 0.0) {
        return 135.0;
    }

    return velocity.x() < 0.0 ? 225.0 : 315.0;
}

/**
 * @brief              두 scene 좌표 사이의 거리를 계산합니다.
 * @param firstPoint   첫 번째 scene 좌표
 * @param secondPoint  두 번째 scene 좌표
 * @return             두 점 사이 거리
 */
double distanceBetween(const QPointF& firstPoint, const QPointF& secondPoint) {
    return std::hypot(firstPoint.x() - secondPoint.x(), firstPoint.y() - secondPoint.y());
}

/**
 * @brief            이동 경로 polyline의 전체 길이를 계산합니다.
 * @param positions  scene 좌표 목록
 * @return           누적 이동 경로 길이
 */
double trailLength(const QVector<QPointF>& positions) {
    double length = 0.0;

    for (int i = 1; i < positions.size(); ++i) {
        length += distanceBetween(positions[i - 1], positions[i]);
    }

    return length;
}

/**
 * @brief            이동 경로 점 개수와 전체 길이를 path 생성에 필요한 범위로 줄입니다.
 * @param positions  정리할 최근 위치 목록
 */
void trimTrailPositions(QVector<QPointF>* positions) {
    if (!positions) {
        return;
    }

    while (positions->size() > maxTrailPointCount) {
        positions->removeFirst();
    }

    while (positions->size() > 2 && trailLength(*positions) > maxTrailSceneLength * 1.35) {
        positions->removeFirst();
    }
}

/**
 * @brief        scene에서 그래픽 아이템을 분리한 뒤 수명을 정리합니다.
 * @param scene  아이템이 등록된 QGraphicsScene
 * @param item   제거할 QGraphicsItem
 */
void releaseSceneItem(QGraphicsScene* scene, QGraphicsItem* item) {
    if (!item) {
        return;
    }

    if (scene) {
        scene->removeItem(item);
    }

    std::shared_ptr<QGraphicsItem> itemOwner(item);
}
}  // namespace

/**
 * @brief       디지털 트윈 맵 scene, 오버레이 관리자, 시뮬레이션 worker를 초기화합니다.
 * @param parent  부모 위젯
 */
DigitalTwinMapWidget::DigitalTwinMapWidget(QWidget* parent)
    : QGraphicsView(parent),
      sceneBuilder_(std::make_shared<DemoParkingMapSceneBuilder>()),
      objectStyleProvider_(std::make_shared<DefaultDigitalTwinObjectStyleProvider>()) {
    qRegisterMetaType<DigitalTwinObject>("DigitalTwinObject");
    qRegisterMetaType<DigitalTwinRiskEvent>("DigitalTwinRiskEvent");
    qRegisterMetaType<QVector<DigitalTwinObject>>("QVector<DigitalTwinObject>");

    setObjectName(QStringLiteral("digitalTwinMapWidget"));
    setScene(&scene_);
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing, true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);

    overlayManager_.setScene(&scene_);

    setupScene();
    setupSimulationWorker();
    startDemo();
}

/**
 * @brief   worker 스레드와 scene 오버레이를 안전하게 정리합니다.
 */
DigitalTwinMapWidget::~DigitalTwinMapWidget() {
    if (simulationWorker_) {
        disconnect(simulationWorker_, nullptr, this, nullptr);
    }

    stopDemo();
    overlayManager_.clear();

    simulationThread_.quit();
    simulationThread_.wait();
    simulationWorker_ = nullptr;
}

/**
 * @brief   시뮬레이션 worker에 데모 시작을 요청합니다.
 */
void DigitalTwinMapWidget::startDemo() {
    if (!simulationWorker_ || !simulationThread_.isRunning()) {
        return;
    }

    QMetaObject::invokeMethod(simulationWorker_, &DigitalTwinSimulationWorker::start, Qt::QueuedConnection);
}

/**
 * @brief   시뮬레이션 worker의 주기 갱신을 중지합니다.
 */
void DigitalTwinMapWidget::stopDemo() {
    if (!simulationWorker_ || !simulationThread_.isRunning()) {
        return;
    }

    QMetaObject::invokeMethod(simulationWorker_, &DigitalTwinSimulationWorker::stop, Qt::BlockingQueuedConnection);
}

/**
 * @brief       위젯 크기 변경 시 scene 전체가 보이도록 뷰를 다시 맞춥니다.
 * @param event  Qt resize 이벤트
 */
void DigitalTwinMapWidget::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    fitMapInView();
}

/**
 * @brief   데모 주차장 맵의 고정 배경 요소를 구성합니다.
 */
void DigitalTwinMapWidget::setupScene() {
    overlayManager_.clear();
    scene_.clear();
    demoItems_.clear();
    visualItemIndexes_.clear();
    mapRect_ = sceneBuilder_->build(&scene_);
}

/**
 * @brief   객체 이동과 위험 판정을 담당하는 worker를 별도 스레드에 연결합니다.
 */
void DigitalTwinMapWidget::setupSimulationWorker() {
    simulationWorker_ = new DigitalTwinSimulationWorker();
    simulationWorker_->moveToThread(&simulationThread_);

    connect(&simulationThread_, &QThread::finished, simulationWorker_, &QObject::deleteLater);
    connect(simulationWorker_, &DigitalTwinSimulationWorker::objectsUpdated, this,
            &DigitalTwinMapWidget::applyObjectUpdates);
    connect(simulationWorker_, &DigitalTwinSimulationWorker::riskEventDetected, this,
            &DigitalTwinMapWidget::showRiskPulse, Qt::QueuedConnection);

    simulationThread_.setObjectName(QStringLiteral("digital-twin-simulation"));
    simulationThread_.start();
}

/**
 * @brief          worker가 계산한 객체 상태를 scene 아이템에 반영합니다.
 * @param objects  최신 객체 상태 목록
 */
void DigitalTwinMapWidget::applyObjectUpdates(const QVector<DigitalTwinObject>& objects) {
    bool createdNewItem = false;

    for (const auto& object : objects) {
        if (!visualItemIndexes_.contains(object.objectId)) {
            createVisualItem(object);
            createdNewItem = true;
            continue;
        }

        const int visualIndex = visualItemIndexes_.value(object.objectId);

        if (visualIndex < 0 || visualIndex >= demoItems_.size()) {
            continue;
        }

        auto& visualItem = demoItems_[visualIndex];
        visualItem.object = object;
        updateVisualItem(&visualItem);
    }

    removeMissingVisualItems(objects);

    if (createdNewItem) {
        fitMapInView();
    }
}

/**
 * @brief       위험 이벤트 위치에 레이더 펄스 오버레이 표시를 요청합니다.
 * @param event  위험/주의 발생 정보
 */
void DigitalTwinMapWidget::showRiskPulse(const DigitalTwinRiskEvent& event) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this,
            [this, event]() {
                showRiskPulse(event);
            },
            Qt::QueuedConnection);
        return;
    }

    overlayManager_.showRiskPulse(scenePointFromNormalized(event.position), event.riskLevel);
}

/**
 * @brief        객체 하나에 대한 아이콘, 라벨, 이동 경로 아이템을 생성합니다.
 * @param object  생성할 데모 객체
 */
void DigitalTwinMapWidget::createVisualItem(const DigitalTwinObject& object) {
    DemoVisualItem visualItem;
    visualItem.object = object;
    visualItem.marker = scene_.addPixmap(QPixmap());
    visualItem.marker->setTransformationMode(Qt::SmoothTransformation);
    visualItem.marker->setZValue(4.0);

    const DigitalTwinObjectVisualStyle visualStyle = objectStyleProvider_->styleFor(object);
    QPen trailPen(visualStyle.trailColor, 2.0, Qt::DashLine);
    trailPen.setCosmetic(true);
    visualItem.trail = scene_.addPath(QPainterPath(), trailPen);
    visualItem.trail->setOpacity(0.55);
    visualItem.trail->setZValue(3.0);

    visualItem.label = scene_.addSimpleText(object.objectId);
    visualItem.label->setScale(0.9);
    visualItem.label->setZValue(5.0);

    demoItems_.append(visualItem);
    visualItemIndexes_.insert(object.objectId, demoItems_.size() - 1);

    updateMarkerPixmap(&demoItems_.last());
    updateVisualItem(&demoItems_.last());
}

/**
 * @brief            객체의 위치, 회전, 아이콘 상태, 이동 경로를 갱신합니다.
 * @param visualItem  갱신할 scene 표시 항목
 */
void DigitalTwinMapWidget::updateVisualItem(DemoVisualItem* visualItem) {
    if (!visualItem || !visualItem->marker || !visualItem->label || !visualItem->trail) {
        return;
    }

    if (visualItem->visibleRiskLevel != visualItem->object.riskLevel) {
        updateMarkerPixmap(visualItem);
    }

    const QPointF scenePosition = scenePointFromNormalized(visualItem->object.position);
    visualItem->marker->setPos(scenePosition);

    const bool isMoving =
        !qFuzzyIsNull(visualItem->object.velocity.x()) || !qFuzzyIsNull(visualItem->object.velocity.y());

    if (visualItem->object.type == DigitalTwinObjectType::Pedestrian) {
        visualItem->marker->setRotation(0.0);
    } else if (isMoving) {
        visualItem->marker->setRotation(approximateRotationDegrees(visualItem->object.velocity) +
                                        movingIconRotationOffsetDegrees);
    }

    const DigitalTwinObjectVisualStyle visualStyle = objectStyleProvider_->styleFor(visualItem->object);
    visualItem->label->setBrush(visualStyle.labelColor);
    visualItem->label->setPos(scenePosition + QPointF(18.0, -31.0));

    visualItem->recentPositions.append(scenePosition);
    trimTrailPositions(&visualItem->recentPositions);

    visualItem->trail->setPath(createTrailPath(visualItem->recentPositions));
}

/**
 * @brief            객체 종류와 위험 단계에 맞는 아이콘 이미지를 적용합니다.
 * @param visualItem  아이콘을 갱신할 scene 표시 항목
 */
void DigitalTwinMapWidget::updateMarkerPixmap(DemoVisualItem* visualItem) {
    if (!visualItem || !visualItem->marker) {
        return;
    }

    const DigitalTwinObjectVisualStyle visualStyle = objectStyleProvider_->styleFor(visualItem->object);
    const QSize iconSize = visualStyle.iconSize;
    QPixmap iconPixmap(visualStyle.iconPath);

    if (iconPixmap.isNull()) {
        iconPixmap = QPixmap(iconSize);
        iconPixmap.fill(Qt::transparent);

        QPainter fallbackPainter(&iconPixmap);
        fallbackPainter.setRenderHint(QPainter::Antialiasing, true);
        fallbackPainter.setPen(QPen(QColor(QStringLiteral("#dce9f8")), 1.2));
        fallbackPainter.setBrush(visualStyle.fallbackColor);
        fallbackPainter.drawEllipse(iconPixmap.rect().adjusted(3, 3, -3, -3));
    }

    const QPixmap scaledIcon = iconPixmap.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    visualItem->marker->setPixmap(scaledIcon);
    visualItem->marker->setOffset(-scaledIcon.width() / 2.0, -scaledIcon.height() / 2.0);
    visualItem->visibleRiskLevel = visualItem->object.riskLevel;
}

/**
 * @brief          worker 목록에서 사라진 객체의 scene item을 제거합니다.
 * @param objects  현재 살아있는 객체 목록
 */
void DigitalTwinMapWidget::removeMissingVisualItems(const QVector<DigitalTwinObject>& objects) {
    QSet<QString> activeObjectIds;

    for (const auto& object : objects) {
        activeObjectIds.insert(object.objectId);
    }

    bool removedItem = false;

    for (int index = demoItems_.size() - 1; index >= 0; --index) {
        if (!activeObjectIds.contains(demoItems_[index].object.objectId)) {
            removeVisualItemAt(index);
            removedItem = true;
        }
    }

    if (removedItem) {
        rebuildVisualItemIndexes();
    }
}

/**
 * @brief              지정한 visual item과 그 하위 scene item을 제거합니다.
 * @param visualIndex  제거할 visual item 인덱스
 */
void DigitalTwinMapWidget::removeVisualItemAt(int visualIndex) {
    if (visualIndex < 0 || visualIndex >= demoItems_.size()) {
        return;
    }

    auto& visualItem = demoItems_[visualIndex];
    releaseSceneItem(&scene_, visualItem.marker);
    releaseSceneItem(&scene_, visualItem.label);
    releaseSceneItem(&scene_, visualItem.trail);

    demoItems_.removeAt(visualIndex);
}

/**
 * @brief   visual item 제거 이후 객체 ID와 인덱스 매핑을 다시 구성합니다.
 */
void DigitalTwinMapWidget::rebuildVisualItemIndexes() {
    visualItemIndexes_.clear();

    for (int index = 0; index < demoItems_.size(); ++index) {
        visualItemIndexes_.insert(demoItems_[index].object.objectId, index);
    }
}

/**
 * @brief                    0.0~1.0 정규화 좌표를 scene 좌표로 변환합니다.
 * @param normalizedPosition  정규화된 객체 위치
 * @return                   scene 좌표계 위치
 */
QPointF DigitalTwinMapWidget::scenePointFromNormalized(const QPointF& normalizedPosition) const {
    return QPointF(mapRect_.left() + normalizedPosition.x() * mapRect_.width(),
                   mapRect_.top() + normalizedPosition.y() * mapRect_.height());
}

/**
 * @brief           최근 위치 목록을 이동 경로 path로 변환합니다.
 * @param positions  scene 좌표계 기준 최근 위치 목록
 * @return          이동 경로 QPainterPath
 */
QPainterPath DigitalTwinMapWidget::createTrailPath(const QVector<QPointF>& positions) const {
    QPainterPath path;

    if (positions.isEmpty()) {
        return path;
    }

    QVector<QPointF> visiblePositions;
    visiblePositions.prepend(positions.last());

    double accumulatedLength = 0.0;

    for (int index = positions.size() - 1; index > 0; --index) {
        const QPointF currentPoint = positions[index];
        const QPointF previousPoint = positions[index - 1];
        const double segmentLength = distanceBetween(previousPoint, currentPoint);

        if (qFuzzyIsNull(segmentLength)) {
            continue;
        }

        if (accumulatedLength + segmentLength >= maxTrailSceneLength) {
            const double remainingLength = maxTrailSceneLength - accumulatedLength;
            const double ratio = remainingLength / segmentLength;
            const QPointF clippedStartPoint = currentPoint + (previousPoint - currentPoint) * ratio;
            visiblePositions.prepend(clippedStartPoint);
            break;
        }

        visiblePositions.prepend(previousPoint);
        accumulatedLength += segmentLength;
    }

    path.moveTo(visiblePositions.first());

    for (int index = 1; index < visiblePositions.size(); ++index) {
        path.lineTo(visiblePositions[index]);
    }

    return path;
}

/**
 * @brief   현재 scene 영역을 위젯 안에 비율 유지로 맞춥니다.
 */
void DigitalTwinMapWidget::fitMapInView() {
    if (!scene()) {
        return;
    }

    fitInView(scene_.sceneRect(), Qt::KeepAspectRatio);
}
