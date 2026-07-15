#pragma once

#include <QObject>

#include "model/DeviceStatusReport.h"

class DeviceStatusGateway : public QObject {
    Q_OBJECT

public:
    explicit DeviceStatusGateway(QObject* parent = nullptr);
    ~DeviceStatusGateway() override;

    virtual void start() = 0;
    virtual void stop() = 0;

signals:
    void reportReceived(DeviceStatusReport report);
    void brokerConnectionChanged(bool connected);
};
