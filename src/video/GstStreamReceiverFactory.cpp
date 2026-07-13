#include "video/GstRtspReceiver.h"
#include "video/StreamReceiverFactory.h"

/**
 * @brief                    GStreamer 기반 RTSP 수신기를 생성합니다.
 * @param outputWindowHandle  영상을 출력할 네이티브 윈도우 핸들
 * @return                   StreamReceiver 인터페이스로 반환되는 수신기
 */
std::shared_ptr<StreamReceiver> GstStreamReceiverFactory::create(guintptr outputWindowHandle) const {
    return std::make_shared<GstRtspReceiver>(outputWindowHandle);
}
