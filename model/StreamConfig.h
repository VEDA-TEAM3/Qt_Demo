#pragma once

#include <QString>

/** 대시보드에서 하나의 카메라 스트림을 구성하는 정적 설정입니다. */
struct StreamConfig {
    /** 로그와 내부 식별에 사용하는 카메라 ID입니다. */
    QString cameraId;

    /** UI에 표시할 구역 이름입니다. */
    QString name;

    /** GStreamer 수신기에 전달할 RTSP 주소입니다. */
    QString url;

    /** 2x2 비디오 그리드에서 이 스트림이 연결될 칸 인덱스입니다. */
    int channelIndex = 0;

    /** false이면 수신기를 만들지 않고 해당 스트림을 건너뜁니다. */
    bool enabled = true;
};
