#pragma once

#include <QWidget>

#include "model/DigitalTwinMapDisplaySettings.h"

class QCheckBox;
class QShowEvent;

class MapSettingsDialog final : public QWidget {
    Q_OBJECT

public:
    explicit MapSettingsDialog(QWidget* parent = nullptr);

    void setSettings(const DigitalTwinMapDisplaySettings& settings);
    DigitalTwinMapDisplaySettings settings() const;

signals:
    void settingsApplied(const DigitalTwinMapDisplaySettings& settings);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QCheckBox* movementTrailsCheckBox_ = nullptr;
    QCheckBox* ledCheckBox_ = nullptr;
    QCheckBox* cctvCheckBox_ = nullptr;
    QCheckBox* alertDeviceCheckBox_ = nullptr;
};
