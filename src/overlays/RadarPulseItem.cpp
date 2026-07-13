#include "overlays/RadarPulseItem.h"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QString>
#include <QtGlobal>
#include <algorithm>

namespace {
constexpr qreal minimumRadius = 10.0;
constexpr qreal warningMaximumRadius = 92.0;
constexpr qreal dangerMaximumRadius = 124.0;
constexpr qreal boundingPadding = 18.0;
constexpr qreal ringDelay = 0.18;
constexpr int warningAnimationDurationMsec = 1180;
constexpr int dangerAnimationDurationMsec = 1460;

/**
 * @brief        애니메이션 진행률을 0.0~1.0 범위로 제한합니다.
 * @param value  제한 전 진행률
 * @return       제한된 진행률
 */
qreal clampedProgress(qreal value) { return std::clamp(value, static_cast<qreal>(0.0), static_cast<qreal>(1.0)); }
}  // namespace

/**
 * @brief       감지 단계에 맞는 레이더 펄스 아이템을 생성합니다.
 * @param riskLevel  표시할 주의/위험 단계
 * @param parent     부모 QGraphicsItem
 */
RadarPulseItem::RadarPulseItem(DigitalTwinRiskLevel riskLevel, QGraphicsItem* parent)
    : QGraphicsItem(parent), riskLevel_(riskLevel) {
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(3.4);
}

/**
 * @brief   펄스가 그려질 수 있는 최대 영역을 반환합니다.
 */
QRectF RadarPulseItem::boundingRect() const {
    const qreal radius = maximumRadius() + boundingPadding;
    return QRectF(-radius, -radius, radius * 2.0, radius * 2.0);
}

/**
 * @brief       현재 진행률에 맞춰 확산 원과 중심광을 그립니다.
 * @param painter  그리기에 사용할 QPainter
 * @param option   Qt 그래픽스 뷰 스타일 옵션
 * @param widget   렌더링 대상 위젯
 */
void RadarPulseItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    if (!painter) {
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);

    const QColor baseColor = pulseColor();
    const int totalRingCount = ringCount();
    const qreal maxRadius = maximumRadius();

    for (int ringIndex = 0; ringIndex < totalRingCount; ++ringIndex) {
        const qreal phase = ringPhase(ringIndex);

        if (phase <= 0.0) {
            continue;
        }

        const qreal radius = minimumRadius + (maxRadius - minimumRadius) * phase;
        const qreal remaining = 1.0 - phase;
        const int alpha = static_cast<int>((riskLevel_ == DigitalTwinRiskLevel::Danger ? 225.0 : 190.0) * remaining);

        QColor glowColor = baseColor;
        glowColor.setAlpha(std::max(0, alpha / 3));
        QPen glowPen(glowColor, riskLevel_ == DigitalTwinRiskLevel::Danger ? 12.0 : 9.0);
        glowPen.setCosmetic(true);
        painter->setPen(glowPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QPointF(0.0, 0.0), radius, radius);

        QColor lineColor = baseColor;
        lineColor.setAlpha(std::max(0, alpha));
        QPen linePen(lineColor, riskLevel_ == DigitalTwinRiskLevel::Danger ? 2.8 : 2.2);
        linePen.setCosmetic(true);
        painter->setPen(linePen);
        painter->drawEllipse(QPointF(0.0, 0.0), radius, radius);
    }

    if (progress_ < 0.34) {
        QColor centerColor = baseColor;
        centerColor.setAlpha(static_cast<int>(120.0 * (1.0 - progress_ / 0.34)));
        painter->setPen(Qt::NoPen);
        painter->setBrush(centerColor);
        painter->drawEllipse(QPointF(0.0, 0.0), 7.0 + progress_ * 18.0, 7.0 + progress_ * 18.0);
    }
}

/**
 * @brief   현재 애니메이션 진행률을 반환합니다.
 */
qreal RadarPulseItem::progress() const { return progress_; }

/**
 * @brief          애니메이션 진행률을 0.0~1.0 범위로 갱신합니다.
 * @param progress  새 진행률
 */
void RadarPulseItem::setProgress(qreal progress) {
    progress_ = clampedProgress(progress);
    update();
}

/**
 * @brief              경과 시간을 기준으로 진행률을 계산해 갱신합니다.
 * @param elapsedMsec  펄스가 시작된 뒤 지난 시간(ms)
 */
void RadarPulseItem::setElapsedMsec(int elapsedMsec) {
    const int duration = durationMsec();

    if (duration <= 0) {
        setProgress(1.0);
        return;
    }

    setProgress(static_cast<qreal>(elapsedMsec) / static_cast<qreal>(duration));
}

/**
 * @brief   펄스 애니메이션이 끝났는지 확인합니다.
 */
bool RadarPulseItem::isFinished() const { return progress_ >= 1.0; }

/**
 * @brief   위험 단계에 맞는 펄스 색상을 반환합니다.
 */
QColor RadarPulseItem::pulseColor() const {
    if (riskLevel_ == DigitalTwinRiskLevel::Danger) {
        return QColor(QStringLiteral("#ff4d4f"));
    }

    return QColor(QStringLiteral("#ffd43b"));
}

/**
 * @brief   위험 단계에 맞는 원 개수를 반환합니다.
 */
int RadarPulseItem::ringCount() const { return riskLevel_ == DigitalTwinRiskLevel::Danger ? 3 : 2; }

/**
 * @brief          지정한 원의 지연 시간을 반영한 개별 진행률을 계산합니다.
 * @param ringIndex  계산할 원 인덱스
 */
qreal RadarPulseItem::ringPhase(int ringIndex) const {
    const qreal totalDelay = static_cast<qreal>(ringCount() - 1) * ringDelay;

    const qreal normalizedTime = progress_ * (1.0 + totalDelay);

    return clampedProgress(normalizedTime - static_cast<qreal>(ringIndex) * ringDelay);
}

/**
 * @brief   위험 단계에 맞는 애니메이션 시간을 반환합니다.
 */
int RadarPulseItem::durationMsec() const {
    return riskLevel_ == DigitalTwinRiskLevel::Danger ? dangerAnimationDurationMsec : warningAnimationDurationMsec;
}

/**
 * @brief   위험 단계에 맞는 최대 반지름을 반환합니다.
 */
qreal RadarPulseItem::maximumRadius() const {
    return riskLevel_ == DigitalTwinRiskLevel::Danger ? dangerMaximumRadius : warningMaximumRadius;
}
