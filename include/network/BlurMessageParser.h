#pragma once

#include <QByteArray>
#include <QString>

#include "model/MqttRealtimeData.h"

class BlurMessageParser final {
public:
    static bool matchesTopic(const QString& topic);
    static bool parse(const QByteArray& payload, const QString& topic, BlurFrameData& frame, QString& error);
};
