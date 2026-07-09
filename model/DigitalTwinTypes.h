#pragma once

#include <QColor>
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>

/** 디지털 트윈 맵에 표시할 객체 유형입니다. */
enum class DigitalTwinObjectType {
    Vehicle,
    Pedestrian,
    Motorcycle,
};

/** 디지털 트윈 데모에서 사용하는 객체의 현재 상태입니다. */
struct DigitalTwinObject {
    /** 객체를 구분하기 위한 ID입니다. */
    QString objectId;

    /** 차량, 보행자, 오토바이 등 맵 아이콘의 유형입니다. */
    DigitalTwinObjectType type = DigitalTwinObjectType::Vehicle;

    /** 맵 전체를 0.0~1.0 범위로 정규화한 현재 위치입니다. */
    QPointF position;

    /** 0.2초마다 더해지는 정규화 좌표 기준 이동량입니다. */
    QPointF velocity;

    /** 맵에서 객체를 구분하기 위한 표시 색상입니다. */
    QColor color;

    /** 다른 객체와 충돌 위험 상태이면 true입니다. */
    bool isDanger = false;
};

Q_DECLARE_METATYPE(DigitalTwinObject)
Q_DECLARE_METATYPE(QVector<DigitalTwinObject>)
