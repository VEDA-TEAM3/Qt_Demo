#pragma once

#include <QString>

/** 카메라 RTSP 주소입니다. */
namespace Network::Rtsp {

inline const QString zone1 = QStringLiteral("rtsp://admin:PASSWORD@192.168.0.6:554/0/profile4/media.smp");

inline const QString zone2 = QStringLiteral("rtsp://admin:PASSWORD@192.168.0.6:554/1/profile4/media.smp");

inline const QString zone3 = QStringLiteral("rtsp://admin:PASSWORD@192.168.0.6:554/2/profile4/media.smp");

inline const QString zone4 = QStringLiteral("rtsp://admin:PASSWORD@192.168.0.6:554/3/profile4/media.smp");
}  // namespace Network::Rtsp
