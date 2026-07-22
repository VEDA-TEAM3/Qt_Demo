#pragma once

#include <QByteArray>
#include <QString>

#include "model/MqttRealtimeData.h"

class TopViewMessageParser final {
public:
    static bool matchesTopic(const QString& topic);
    static bool parse(const QByteArray& payload, const QString& topic, TopViewFrameData& frame, QString& error);
};
