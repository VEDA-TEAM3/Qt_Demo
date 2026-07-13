#pragma once

#include <QString>

namespace Network::Rtsp {

inline QString zone1() { return QStringLiteral("rtsp://admin:5hanwha!@192.168.0.6:554/0/profile4/media.smp"); }

inline QString zone2() { return QStringLiteral("rtsp://admin:5hanwha!@192.168.0.6:554/1/profile4/media.smp"); }

inline QString zone3() { return QStringLiteral("rtsp://admin:5hanwha!@192.168.0.6:554/2/profile4/media.smp"); }

inline QString zone4() { return QStringLiteral("rtsp://admin:5hanwha!@192.168.0.6:554/3/profile4/media.smp"); }
}  // namespace Network::Rtsp
