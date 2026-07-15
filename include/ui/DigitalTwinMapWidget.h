#pragma once

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHash>
#include <QRectF>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <memory>

#include "model/DigitalTwinTypes.h"
#include "model/MqttRealtimeData.h"
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

public slots:
    void applyTopViewFrame(TopViewFrameData frame);
    void applyCentralEvent(CentralEventData event);

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
    void rebuildLiveSnapshot();
    QPointF normalizedWorldPosition(const QPointF& worldPosition) const;
    int activeSeverityForChannel(int channelIndex) const;
    bool hasActiveCentralDanger() const;
    void expireStaleLiveFrames();
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
    QHash<int, TopViewFrameData> liveFrames_;
    QHash<int, qint64> liveFrameArrivalTimes_;
    QHash<int, qint64> liveFrameSourceTimes_;
    QHash<QString, CentralEventData> activeCentralEvents_;
    QHash<QString, QPointF> previousLivePositions_;
    QTimer liveFrameExpiryTimer_;
    QRectF mapRect_;
    QRectF configuredWorldBounds_;
    QRectF automaticWorldBounds_;
    bool hasConfiguredWorldBounds_ = false;
    bool hasAutomaticWorldBounds_ = false;
    bool liveMode_ = false;
};
