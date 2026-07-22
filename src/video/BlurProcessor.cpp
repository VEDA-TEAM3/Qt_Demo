#include "video/BlurProcessor.h"

#include <gst/video/video-frame.h>

#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <utility>

namespace {
constexpr qint64 blurHistoryMsec = 10000;
constexpr qint64 blurMatchToleranceMsec = 1500;
constexpr qsizetype maximumBlurHistorySize = 300;
constexpr double blurPaddingRatio = 0.18;
constexpr int maximumBlurCornerRadius = 28;

/**
 * @brief               픽셀이 둥근 사각형 블러 영역 안에 포함되는지 확인합니다.
 * @param x             블러 영역 내부 X 좌표
 * @param y             블러 영역 내부 Y 좌표
 * @param width         블러 영역 너비
 * @param height        블러 영역 높이
 * @param cornerRadius  모서리 반지름
 * @return              둥근 사각형 안쪽이면 true
 */
bool isInsideRoundedRegion(int x, int y, int width, int height, int cornerRadius) {
    if (cornerRadius <= 0 || (x >= cornerRadius && x < width - cornerRadius) ||
        (y >= cornerRadius && y < height - cornerRadius)) {
        return true;
    }

    const int centerX = x < cornerRadius ? cornerRadius : width - cornerRadius - 1;
    const int centerY = y < cornerRadius ? cornerRadius : height - cornerRadius - 1;
    const int deltaX = x - centerX;
    const int deltaY = y - centerY;
    return deltaX * deltaX + deltaY * deltaY <= cornerRadius * cornerRadius;
}

/**
 * @brief                    환경 변수 또는 기본값으로 영상-메타데이터 동기화 지연을 결정합니다.
 * @param defaultOffsetMsec  기본 동기화 지연
 * @return                   적용할 동기화 지연
 */
qint64 configuredSyncOffsetMsec(qint64 defaultOffsetMsec) {
    bool valid = false;
    const int configured = qEnvironmentVariableIntValue("QTCCTV_BLUR_SYNC_OFFSET_MS", &valid);
    return valid && configured >= 0 && configured <= 10000 ? static_cast<qint64>(configured) : defaultOffsetMsec;
}

/**
 * @brief            정규화 좌표를 내림 방향 픽셀 좌표로 변환합니다.
 * @param normalized 정규화 좌표
 * @param extent     영상 축 길이
 * @return           영상 범위로 제한한 픽셀 좌표
 */
int normalizedFloorPixel(double normalized, int extent) {
    return qBound(0, static_cast<int>(normalized * static_cast<double>(extent)), extent);
}

/**
 * @brief            정규화 좌표를 올림 방향 픽셀 좌표로 변환합니다.
 * @param normalized 정규화 좌표
 * @param extent     영상 축 길이
 * @return           영상 범위로 제한한 픽셀 좌표
 */
int normalizedCeilPixel(double normalized, int extent) {
    const double scaled = normalized * static_cast<double>(extent);
    int pixel = static_cast<int>(scaled);
    if (static_cast<double>(pixel) < scaled) {
        ++pixel;
    }
    return qBound(0, pixel, extent);
}

/**
 * @brief           BGRA 프레임의 지정 영역에 2-pass box blur를 적용합니다.
 * @param frame     수정할 영상 프레임
 * @param sourceBox 정규화된 블러 영역
 * @param scratch   재사용할 중간 버퍼
 */
void applyBoxBlur(GstVideoFrame& frame, const QRectF& sourceBox, std::vector<guint8>& scratch) {
    if (GST_VIDEO_FRAME_FORMAT(&frame) != GST_VIDEO_FORMAT_BGRA) {
        return;
    }

    const int frameWidth = GST_VIDEO_FRAME_WIDTH(&frame);
    const int frameHeight = GST_VIDEO_FRAME_HEIGHT(&frame);
    if (frameWidth <= 0 || frameHeight <= 0) {
        return;
    }

    const double paddingX = sourceBox.width() * blurPaddingRatio;
    const double paddingY = sourceBox.height() * blurPaddingRatio;
    const QRectF paddedBox =
        sourceBox.adjusted(-paddingX, -paddingY, paddingX, paddingY).intersected(QRectF(0.0, 0.0, 1.0, 1.0));

    const int left = normalizedFloorPixel(paddedBox.left(), frameWidth);
    const int top = normalizedFloorPixel(paddedBox.top(), frameHeight);
    const int right = normalizedCeilPixel(paddedBox.right(), frameWidth);
    const int bottom = normalizedCeilPixel(paddedBox.bottom(), frameHeight);
    const int regionWidth = right - left;
    const int regionHeight = bottom - top;
    if (regionWidth < 2 || regionHeight < 2) {
        return;
    }

    auto* pixels = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
    const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
    const int radius = std::clamp(std::min(regionWidth, regionHeight) / 8, 4, 18);
    const int cornerRadius =
        std::min(maximumBlurCornerRadius, std::max(1, std::min(regionWidth, regionHeight) / 4));
    constexpr int colorChannels = 3;
    const size_t scratchSize = static_cast<size_t>(regionWidth) * regionHeight * colorChannels;
    if (scratch.size() < scratchSize) {
        scratch.resize(scratchSize);
    }

    for (int localY = 0; localY < regionHeight; ++localY) {
        const guint8* sourceRow = pixels + (top + localY) * stride + left * 4;
        for (int channel = 0; channel < colorChannels; ++channel) {
            quint64 sum = 0;
            int windowEnd = std::min(radius, regionWidth - 1);
            for (int x = 0; x <= windowEnd; ++x) {
                sum += sourceRow[x * 4 + channel];
            }

            for (int x = 0; x < regionWidth; ++x) {
                const int windowStart = std::max(0, x - radius);
                windowEnd = std::min(regionWidth - 1, x + radius);
                const int count = windowEnd - windowStart + 1;
                scratch[(static_cast<size_t>(localY) * regionWidth + x) * colorChannels + channel] =
                    static_cast<guint8>(sum / static_cast<quint64>(count));

                const int removeX = x - radius;
                const int addX = x + radius + 1;
                if (removeX >= 0) {
                    sum -= sourceRow[removeX * 4 + channel];
                }
                if (addX < regionWidth) {
                    sum += sourceRow[addX * 4 + channel];
                }
            }
        }
    }

    for (int localX = 0; localX < regionWidth; ++localX) {
        for (int channel = 0; channel < colorChannels; ++channel) {
            quint64 sum = 0;
            int windowEnd = std::min(radius, regionHeight - 1);
            for (int y = 0; y <= windowEnd; ++y) {
                sum += scratch[(static_cast<size_t>(y) * regionWidth + localX) * colorChannels + channel];
            }

            for (int localY = 0; localY < regionHeight; ++localY) {
                const int windowStart = std::max(0, localY - radius);
                windowEnd = std::min(regionHeight - 1, localY + radius);
                const int count = windowEnd - windowStart + 1;
                if (isInsideRoundedRegion(localX, localY, regionWidth, regionHeight, cornerRadius)) {
                    guint8* targetPixel = pixels + (top + localY) * stride + (left + localX) * 4;
                    targetPixel[channel] = static_cast<guint8>(sum / static_cast<quint64>(count));
                }

                const int removeY = localY - radius;
                const int addY = localY + radius + 1;
                if (removeY >= 0) {
                    sum -= scratch[(static_cast<size_t>(removeY) * regionWidth + localX) * colorChannels + channel];
                }
                if (addY < regionHeight) {
                    sum += scratch[(static_cast<size_t>(addY) * regionWidth + localX) * colorChannels + channel];
                }
            }
        }
    }
}
}  // namespace

/**
 * @brief                    블러 프레임 처리기를 생성합니다.
 * @param defaultSyncOffsetMsec 영상 지연에 맞춘 기본 동기화 오프셋
 */
BlurProcessor::BlurProcessor(qint64 defaultSyncOffsetMsec)
    : syncOffsetMsec_(configuredSyncOffsetMsec(defaultSyncOffsetMsec)) {}

/**
 * @brief                     블러를 적용할 객체 유형을 설정합니다.
 * @param faceEnabled         얼굴 블러 활성화 여부
 * @param licensePlateEnabled 차량 번호판 블러 활성화 여부
 */
void BlurProcessor::setTargetsEnabled(bool faceEnabled, bool licensePlateEnabled) {
    faceEnabled_.store(faceEnabled, std::memory_order_release);
    licensePlateEnabled_.store(licensePlateEnabled, std::memory_order_release);
}

/**
 * @brief       MQTT에서 수신한 채널별 블러 좌표를 시간순으로 저장합니다.
 * @param frame 블러 메타데이터 프레임
 */
void BlurProcessor::submitFrame(BlurFrameData frame) {
    if (frame.channelIndex < 0 || frame.sourceTimestamp <= 0) {
        return;
    }

    channelIndex_.store(frame.channelIndex, std::memory_order_relaxed);
    sourceToLocalOffsetMsec_.store(QDateTime::currentMSecsSinceEpoch() - frame.sourceTimestamp,
                                   std::memory_order_relaxed);
    clockOffsetReady_.store(true, std::memory_order_release);

    QMutexLocker locker(&mutex_);
    const auto position = std::lower_bound(
        history_.begin(), history_.end(), frame.sourceTimestamp,
        [](const BlurFrameData& stored, qint64 timestamp) { return stored.sourceTimestamp < timestamp; });

    if (position != history_.end() && position->sourceTimestamp == frame.sourceTimestamp) {
        *position = std::move(frame);
    } else {
        history_.insert(position, std::move(frame));
    }

    const qint64 newestTimestamp = history_.constLast().sourceTimestamp;
    while (!history_.isEmpty() && (history_.constFirst().sourceTimestamp < newestTimestamp - blurHistoryMsec ||
                                   history_.size() > maximumBlurHistorySize)) {
        history_.removeFirst();
    }
}

/**
 * @brief 저장된 블러 좌표와 동기화 상태를 초기화합니다.
 */
void BlurProcessor::clear() {
    QMutexLocker locker(&mutex_);
    history_.clear();
    clockOffsetReady_.store(false, std::memory_order_release);
}

/**
 * @brief       GStreamer가 제공한 쓰기 가능한 영상 프레임에 현재 시각과 가장 가까운 블러 좌표를 적용합니다.
 * @param frame BGRA 영상 프레임
 */
void BlurProcessor::apply(GstVideoFrame& frame) {
    if (!clockOffsetReady_.load(std::memory_order_acquire)) {
        return;
    }

    const qint64 sourceToLocalOffset = sourceToLocalOffsetMsec_.load(std::memory_order_relaxed);
    const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
    const qint64 targetTimestamp = nowMsec - sourceToLocalOffset - syncOffsetMsec_;
    const QVector<QRectF> regions = regionsFor(targetTimestamp);
    if (regions.isEmpty()) {
        return;
    }

    if (GST_VIDEO_FRAME_FORMAT(&frame) != GST_VIDEO_FORMAT_BGRA) {
        return;
    }

    for (const QRectF& region : regions) {
        applyBoxBlur(frame, region, scratch_);
    }

    qint64 lastLogMsec = lastApplyLogMsec_.load(std::memory_order_relaxed);
    if (nowMsec - lastLogMsec >= 1000 &&
        lastApplyLogMsec_.compare_exchange_strong(lastLogMsec, nowMsec, std::memory_order_relaxed)) {
        qInfo().noquote() << QStringLiteral("[BLUR APPLY] channel=%1 regions=%2 frame=%3x%4 targetTs=%5")
                                 .arg(channelIndex_.load(std::memory_order_relaxed))
                                 .arg(regions.size())
                                 .arg(GST_VIDEO_FRAME_WIDTH(&frame))
                                 .arg(GST_VIDEO_FRAME_HEIGHT(&frame))
                                 .arg(targetTimestamp);
    }
}

/**
 * @brief                  지정 시각과 가장 가까운 블러 영역을 조회합니다.
 * @param sourceTimestamp  영상에 대응시킬 원본 시각
 * @return                 정규화된 블러 영역 목록
 */
QVector<QRectF> BlurProcessor::regionsFor(qint64 sourceTimestamp) const {
    QMutexLocker locker(&mutex_);
    if (history_.isEmpty()) {
        return {};
    }

    const auto after = std::lower_bound(
        history_.cbegin(), history_.cend(), sourceTimestamp,
        [](const BlurFrameData& frame, qint64 timestamp) { return frame.sourceTimestamp < timestamp; });

    const BlurFrameData* nearest = after == history_.cend() ? nullptr : &*after;
    if (after != history_.cbegin()) {
        const BlurFrameData& before = *std::prev(after);
        if (nearest == nullptr ||
            sourceTimestamp - before.sourceTimestamp <= nearest->sourceTimestamp - sourceTimestamp) {
            nearest = &before;
        }
    }
    if (nearest == nullptr || std::llabs(nearest->sourceTimestamp - sourceTimestamp) > blurMatchToleranceMsec) {
        return {};
    }

    QVector<QRectF> regions;
    regions.reserve(nearest->regions.size());
    for (const BlurRegionData& region : nearest->regions) {
        const bool enabled = region.targetType == BlurTargetType::Face
                                 ? faceEnabled_.load(std::memory_order_acquire)
                                 : licensePlateEnabled_.load(std::memory_order_acquire);
        if (!enabled) {
            continue;
        }
        regions.append(region.normalizedBox);
    }
    return regions;
}
