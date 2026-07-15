#pragma once

#include <QMetaType>
#include <QString>

#include "model/DeviceStatus.h"

enum class DeviceStatusReportType {
    ControllerOnline,
    FeedbackConfirmed,
    FeedbackFailed,
    ProtocolError,
};

struct DeviceStatusReport {
    DeviceStatusReportType type = DeviceStatusReportType::ProtocolError;
    int channelIndex = -1;
    qint64 sourceTimestamp = 0;
    QString node;
    QString detail;
    bool hasOutputState = false;
    DeviceOutputState outputs;
};

Q_DECLARE_METATYPE(DeviceStatusReport)
