#include "DigitalTwinMapWidget.h"

#include <QBrush>
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
#include <QSize>
#include <QtGlobal>
#include <cmath>

#include "../model/DigitalTwinSimulationWorker.h"

namespace {
constexpr int maxTrailPointCount = 22;
constexpr double sceneWidth = 1000.0;
constexpr double sceneHeight = 520.0;
constexpr double maxTrailSceneLength = 240.0;
constexpr double movingIconRotationOffsetDegrees = 90.0;

QString iconPathForObject(const DigitalTwinObject& object) {
    if (object.isDanger) {
        switch (object.type) {
            case DigitalTwinObjectType::Vehicle:
                return QStringLiteral(":/icons/vehicle_danger.png");
            case DigitalTwinObjectType::Pedestrian:
                return QStringLiteral(":/icons/human_danger.png");
            case DigitalTwinObjectType::Motorcycle:
                return QStringLiteral(":/icons/motor_danger.png");
        }
    }

    switch (object.type) {
        case DigitalTwinObjectType::Vehicle:
            return QStringLiteral(":/icons/vehicle.png");
        case DigitalTwinObjectType::Pedestrian:
            return QStringLiteral(":/icons/human.png");
        case DigitalTwinObjectType::Motorcycle:
            return QStringLiteral(":/icons/motor.png");
    }

    return QStringLiteral(":/icons/vehicle.png");
}

QSize iconSizeForType(DigitalTwinObjectType objectType) {
    switch (objectType) {
        case DigitalTwinObjectType::Vehicle:
            return QSize(92, 92);
        case DigitalTwinObjectType::Pedestrian:
            return QSize(60, 60);
        case DigitalTwinObjectType::Motorcycle:
            return QSize(69, 69);
    }

    return QSize(44, 44);
}

double absoluteValue(double value) {
    return value < 0.0 ? -value : value;
}

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

double distanceBetween(const QPointF& firstPoint, const QPointF& secondPoint) {
    return std::hypot(firstPoint.x() - secondPoint.x(), firstPoint.y() - secondPoint.y());
}

double trailLength(const QVector<QPointF>& positions) {
    double length = 0.0;

    for (int i = 1; i < positions.size(); ++i) {
        length += distanceBetween(positions[i - 1], positions[i]);
    }

    return length;
}

void trimTrailPositions(QVector<QPointF>* positions) {
    if (!positions) {
        return;
    }

    while (positions->size() > maxTrailPointCount) {
        positions->removeFirst();
    }

    while (positions->size() > 2 && trailLength(*positions) > maxTrailSceneLength) {
        positions->removeFirst();
    }
}
}  // namespace

DigitalTwinMapWidget::DigitalTwinMapWidget(QWidget* parent) : QGraphicsView(parent) {
    qRegisterMetaType<DigitalTwinObject>("DigitalTwinObject");
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

    setupScene();
    setupSimulationWorker();
    startDemo();
}

DigitalTwinMapWidget::~DigitalTwinMapWidget() {
    stopDemo();

    simulationThread_.quit();
    simulationThread_.wait();
    simulationWorker_ = nullptr;
}

void DigitalTwinMapWidget::startDemo() {
    if (!simulationWorker_ || !simulationThread_.isRunning()) {
        return;
    }

    QMetaObject::invokeMethod(simulationWorker_, &DigitalTwinSimulationWorker::start, Qt::QueuedConnection);
}

void DigitalTwinMapWidget::stopDemo() {
    if (!simulationWorker_ || !simulationThread_.isRunning()) {
        return;
    }

    QMetaObject::invokeMethod(simulationWorker_, &DigitalTwinSimulationWorker::stop, Qt::BlockingQueuedConnection);
}

void DigitalTwinMapWidget::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    fitMapInView();
}

void DigitalTwinMapWidget::setupScene() {
    scene_.clear();

    mapRect_ = QRectF(0.0, 0.0, sceneWidth, sceneHeight);
    scene_.setSceneRect(mapRect_.adjusted(-24.0, -24.0, 24.0, 24.0));
    scene_.setBackgroundBrush(QColor(QStringLiteral("#08111d")));

    QPen wallPen(QColor(QStringLiteral("#3a4c63")), 3.0);
    QPen thinLinePen(QColor(QStringLiteral("#24364c")), 1.4);
    QPen slotPen(QColor(QStringLiteral("#40546d")), 1.4);

    scene_.addRect(mapRect_, QPen(QColor(QStringLiteral("#1d2c3f")), 1.0), QBrush(QColor(QStringLiteral("#0a1523"))));
    scene_.addRect(QRectF(44.0, 54.0, 912.0, 396.0), wallPen, Qt::NoBrush)->setZValue(1.0);
    scene_.addLine(QLineF(72.0, 190.0, 928.0, 190.0), thinLinePen)->setZValue(1.0);
    scene_.addLine(QLineF(72.0, 326.0, 928.0, 326.0), thinLinePen)->setZValue(1.0);
    scene_.addLine(QLineF(500.0, 74.0, 500.0, 430.0), thinLinePen)->setZValue(1.0);

    for (int i = 0; i < 8; ++i) {
        const double leftX = 94.0 + i * 48.0;
        const double rightX = 566.0 + i * 48.0;
        scene_.addRect(QRectF(leftX, 78.0, 40.0, 94.0), slotPen, Qt::NoBrush)->setZValue(1.0);
        scene_.addRect(QRectF(rightX, 78.0, 40.0, 94.0), slotPen, Qt::NoBrush)->setZValue(1.0);
        scene_.addRect(QRectF(leftX, 346.0, 40.0, 78.0), slotPen, Qt::NoBrush)->setZValue(1.0);
        scene_.addRect(QRectF(rightX, 346.0, 40.0, 78.0), slotPen, Qt::NoBrush)->setZValue(1.0);
    }

    auto* titleItem = scene_.addSimpleText(QStringLiteral("B1F DEMO MAP"));
    titleItem->setBrush(QColor(QStringLiteral("#53677f")));
    titleItem->setPos(414.0, 238.0);
    titleItem->setScale(1.8);
    titleItem->setZValue(1.0);
}

void DigitalTwinMapWidget::setupSimulationWorker() {
    simulationWorker_ = new DigitalTwinSimulationWorker();
    simulationWorker_->moveToThread(&simulationThread_);

    connect(&simulationThread_, &QThread::finished, simulationWorker_, &QObject::deleteLater);
    connect(simulationWorker_, &DigitalTwinSimulationWorker::objectsUpdated, this,
            &DigitalTwinMapWidget::applyObjectUpdates);

    simulationThread_.setObjectName(QStringLiteral("digital-twin-simulation"));
    simulationThread_.start();
}

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

    if (createdNewItem) {
        fitMapInView();
    }
}

void DigitalTwinMapWidget::createVisualItem(const DigitalTwinObject& object) {
    DemoVisualItem visualItem;
    visualItem.object = object;
    visualItem.marker = scene_.addPixmap(QPixmap());
    visualItem.marker->setTransformationMode(Qt::SmoothTransformation);
    visualItem.marker->setZValue(4.0);

    QPen trailPen(object.color, 2.0, Qt::DashLine);
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

void DigitalTwinMapWidget::updateVisualItem(DemoVisualItem* visualItem) {
    if (!visualItem || !visualItem->marker || !visualItem->label || !visualItem->trail) {
        return;
    }

    if (visualItem->dangerIconVisible != visualItem->object.isDanger) {
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

    visualItem->label->setBrush(visualItem->object.isDanger ? QColor(QStringLiteral("#ff5a5f"))
                                                            : QColor(QStringLiteral("#d8e3f2")));
    visualItem->label->setPos(scenePosition + QPointF(18.0, -31.0));

    visualItem->recentPositions.append(scenePosition);
    trimTrailPositions(&visualItem->recentPositions);

    visualItem->trail->setPath(createTrailPath(visualItem->recentPositions));
}

void DigitalTwinMapWidget::updateMarkerPixmap(DemoVisualItem* visualItem) {
    if (!visualItem || !visualItem->marker) {
        return;
    }

    const QSize iconSize = iconSizeForType(visualItem->object.type);
    QPixmap iconPixmap(iconPathForObject(visualItem->object));

    if (iconPixmap.isNull()) {
        iconPixmap = QPixmap(iconSize);
        iconPixmap.fill(Qt::transparent);

        QPainter fallbackPainter(&iconPixmap);
        fallbackPainter.setRenderHint(QPainter::Antialiasing, true);
        fallbackPainter.setPen(QPen(QColor(QStringLiteral("#dce9f8")), 1.2));
        fallbackPainter.setBrush(visualItem->object.isDanger ? QColor(QStringLiteral("#ff4d4f"))
                                                             : visualItem->object.color);
        fallbackPainter.drawEllipse(iconPixmap.rect().adjusted(3, 3, -3, -3));
    }

    const QPixmap scaledIcon = iconPixmap.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    visualItem->marker->setPixmap(scaledIcon);
    visualItem->marker->setOffset(-scaledIcon.width() / 2.0, -scaledIcon.height() / 2.0);
    visualItem->dangerIconVisible = visualItem->object.isDanger;
}

QPointF DigitalTwinMapWidget::scenePointFromNormalized(const QPointF& normalizedPosition) const {
    return QPointF(mapRect_.left() + normalizedPosition.x() * mapRect_.width(),
                   mapRect_.top() + normalizedPosition.y() * mapRect_.height());
}

QPainterPath DigitalTwinMapWidget::createTrailPath(const QVector<QPointF>& positions) const {
    QPainterPath path;

    if (positions.isEmpty()) {
        return path;
    }

    path.moveTo(positions.first());

    for (int i = 1; i < positions.size(); ++i) {
        path.lineTo(positions[i]);
    }

    return path;
}

void DigitalTwinMapWidget::fitMapInView() {
    if (!scene()) {
        return;
    }

    fitInView(scene_.sceneRect(), Qt::KeepAspectRatio);
}
