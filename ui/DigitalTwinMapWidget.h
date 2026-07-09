#pragma once

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHash>
#include <QRectF>
#include <QThread>
#include <QVector>

#include "../model/DigitalTwinTypes.h"

class DigitalTwinSimulationWorker;
class QGraphicsPathItem;
class QGraphicsPixmapItem;
class QGraphicsSimpleTextItem;
class QPainterPath;
class QResizeEvent;

/** 2D 주차장 맵 위에 실시간 객체 아이콘을 표시하는 디지털 트윈 데모 위젯입니다. */
class DigitalTwinMapWidget : public QGraphicsView {
    Q_OBJECT

public:
    /** 맵 화면과 시뮬레이션 worker를 초기화합니다. */
    explicit DigitalTwinMapWidget(QWidget* parent = nullptr);
    ~DigitalTwinMapWidget() override;

    /** 0.2초 주기의 데모 객체 이동을 시작합니다. */
    void startDemo();

    /** 데모 객체 이동을 중지합니다. */
    void stopDemo();

protected:
    /** 창 크기가 바뀔 때 맵이 카드 영역 안에 맞도록 배율을 조정합니다. */
    void resizeEvent(QResizeEvent* event) override;

private:
    /** 하나의 디지털 트윈 객체와 화면 표시 아이템을 묶어 관리합니다. */
    struct DemoVisualItem {
        DigitalTwinObject object;
        QGraphicsPixmapItem* marker = nullptr;
        QGraphicsSimpleTextItem* label = nullptr;
        QGraphicsPathItem* trail = nullptr;
        QVector<QPointF> recentPositions;
        bool dangerIconVisible = false;
    };

    /** 데모용 2D 주차장 맵 배경을 구성합니다. */
    void setupScene();

    /** 데모 객체의 이동과 충돌 판단을 맡는 worker를 별도 스레드에 준비합니다. */
    void setupSimulationWorker();

    /** worker에서 전달받은 객체 상태를 화면 아이템에 반영합니다. */
    void applyObjectUpdates(const QVector<DigitalTwinObject>& objects);

    /** 디지털 트윈 객체 하나에 대응되는 아이콘, 라벨, 이동 궤적을 생성합니다. */
    void createVisualItem(const DigitalTwinObject& object);

    /** 객체 상태를 기준으로 아이콘, 라벨, 이동 궤적의 위치를 갱신합니다. */
    void updateVisualItem(DemoVisualItem* visualItem);

    /** 객체 위험 상태에 맞는 일반/위험 아이콘을 적용합니다. */
    void updateMarkerPixmap(DemoVisualItem* visualItem);

    /** 정규화된 0.0~1.0 좌표를 현재 맵 화면 좌표로 변환합니다. */
    QPointF scenePointFromNormalized(const QPointF& normalizedPosition) const;

    /** 최근 이동 좌표 목록으로 이동 궤적 경로를 만듭니다. */
    QPainterPath createTrailPath(const QVector<QPointF>& positions) const;

    /** 현재 맵 화면이 위젯 영역 안에 비율 유지 상태로 맞도록 조정합니다. */
    void fitMapInView();

    QGraphicsScene scene_;
    QThread simulationThread_;
    DigitalTwinSimulationWorker* simulationWorker_ = nullptr;
    QVector<DemoVisualItem> demoItems_;
    QHash<QString, int> visualItemIndexes_;
    QRectF mapRect_;
};
