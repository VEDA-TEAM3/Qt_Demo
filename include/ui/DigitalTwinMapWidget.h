#pragma once

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHash>
#include <QRectF>
#include <QThread>
#include <QVector>
#include <memory>

#include "model/DigitalTwinTypes.h"
#include "overlays/OverlayManager.h"

class DigitalTwinSimulationWorker;
class DigitalTwinMapSceneBuilder;
class DigitalTwinObjectStyleProvider;
class DangerAlertOverlay;
class QGraphicsPathItem;
class QGraphicsPixmapItem;
class QGraphicsSimpleTextItem;
class QPainterPath;
class QResizeEvent;

class DigitalTwinMapWidget : public QGraphicsView {
    Q_OBJECT

public:
    explicit DigitalTwinMapWidget(QWidget* parent = nullptr);
    ~DigitalTwinMapWidget() override;

    void startDemo();
    void stopDemo();

signals:
    void simulationSnapshotUpdated(DigitalTwinSnapshot snapshot);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct DemoVisualItem {
        DigitalTwinObject object;
        QGraphicsPixmapItem* marker = nullptr;
        QGraphicsSimpleTextItem* label = nullptr;
        QGraphicsPathItem* trail = nullptr;
        QVector<QPointF> recentPositions;
        DigitalTwinRiskLevel visibleRiskLevel = DigitalTwinRiskLevel::Normal;
    };

    void setupScene();
    void setupSimulationWorker();
    void applySimulationSnapshot(const DigitalTwinSnapshot& snapshot);
    void applyObjectUpdates(const QVector<DigitalTwinObject>& objects);
    void showRiskPulse(const DigitalTwinRiskEvent& event);
    void createVisualItem(const DigitalTwinObject& object);
    void updateVisualItem(DemoVisualItem* visualItem);
    void updateMarkerPixmap(DemoVisualItem* visualItem);
    void removeMissingVisualItems(const QVector<DigitalTwinObject>& objects);
    void removeVisualItemAt(qsizetype visualIndex);
    void rebuildVisualItemIndexes();
    QPointF scenePointFromNormalized(const QPointF& normalizedPosition) const;
    QPainterPath createTrailPath(const QVector<QPointF>& positions) const;
    void fitMapInView();

    QGraphicsScene scene_;
    QThread simulationThread_;
    OverlayManager overlayManager_;
    DangerAlertOverlay* dangerAlertOverlay_ = nullptr;
    std::shared_ptr<DigitalTwinMapSceneBuilder> sceneBuilder_;
    std::shared_ptr<DigitalTwinObjectStyleProvider> objectStyleProvider_;
    std::shared_ptr<DigitalTwinSimulationWorker> simulationWorker_;
    QVector<DemoVisualItem> demoItems_;
    QHash<QString, qsizetype> visualItemIndexes_;
    QRectF mapRect_;
};
