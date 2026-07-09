#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <memory>

#include "DigitalTwinTypes.h"

/** 디지털 트윈 데모 객체의 이동과 충돌 상태를 UI 스레드 밖에서 계산합니다. */
class DigitalTwinSimulationWorker : public QObject {
    Q_OBJECT

public:
    explicit DigitalTwinSimulationWorker(QObject* parent = nullptr);

public slots:
    /** 데모 객체를 초기화하고 주기적인 상태 계산을 시작합니다. */
    void start();

    /** 주기적인 상태 계산을 중지합니다. */
    void stop();

signals:
    /** 최신 객체 상태를 UI 스레드로 전달합니다. */
    void objectsUpdated(QVector<DigitalTwinObject> objects);

private slots:
    void updateObjects();

private:
    void setupDemoObjects();
    void ensureTimer();
    void updateObjectMotion(DigitalTwinObject* object);
    void updateDangerStates();
    void emitCurrentObjects();

    std::shared_ptr<QTimer> updateTimer_;
    QVector<DigitalTwinObject> objects_;
    QVector<int> dangerHoldTicks_;
};
