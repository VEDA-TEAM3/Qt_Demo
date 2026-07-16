#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QVector>
#include <memory>

#include "model/DigitalTwinMapDisplaySettings.h"
#include "model/StreamConfig.h"

class ClickableVideoWidget;
class DashboardPanelCoordinator;
class DashboardPanelFactory;
class DeviceStatusGatewayFactory;
class DeviceStatusPanel;
class DeviceStatusService;
class EventLogPanel;
class ObjectListPanel;
class QEvent;
class QFrame;
class QLabel;
class MapSettingsDialog;
class QResizeEvent;
class QShowEvent;
class StreamReceiverFactory;
class StreamSessionManager;
class QWidget;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::shared_ptr<StreamReceiverFactory> streamReceiverFactory,
                        std::shared_ptr<DeviceStatusGatewayFactory> deviceStatusGatewayFactory,
                        std::shared_ptr<DashboardPanelFactory> dashboardPanelFactory,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupDashboardLayout();
    void setupDashboardPanels();
    void setupDashboardPanelCoordinator();
    void setupDeviceStatusService();
    void setupTopBarStatuses();
    void setupClock();
    void setupStreamConfigs();
    void setupStreamSessionManager(std::shared_ptr<StreamReceiverFactory> receiverFactory);
    void setupVideoViewEvents();
    void openMapSettingsDialog();

    void updateDashboardAdaptiveSizes();
    void updateSystemStatus(bool connected);
    void updateStreamConnectionStatus();
    void updateCurrentDateTime();
    void setTopBarStatus(QLabel* label, const QString& title, const QString& status, const QString& color);

    void toggleExpandVideo(QWidget* targetWidget);
    void expandVideo(QWidget* targetWidget);
    void restoreVideoGrid();
    void updateCameraSelectionLabel(QWidget* targetWidget);

private:
    std::shared_ptr<Ui::MainWindow> ui_;
    std::shared_ptr<DeviceStatusGatewayFactory> deviceStatusGatewayFactory_;
    std::shared_ptr<DashboardPanelFactory> dashboardPanelFactory_;
    std::shared_ptr<DeviceStatusService> deviceStatusService_;

    QVector<QWidget*> videoWidgets_;
    QVector<QFrame*> videoTileFrames_;
    QVector<StreamConfig> streamConfigs_;
    QVector<bool> streamChannelReady_;

    StreamSessionManager* streamSessionManager_ = nullptr;
    DashboardPanelCoordinator* dashboardPanelCoordinator_ = nullptr;
    DeviceStatusPanel* deviceStatusPanel_ = nullptr;
    EventLogPanel* eventLogPanel_ = nullptr;
    ObjectListPanel* objectListPanel_ = nullptr;
    MapSettingsDialog* mapSettingsDialog_ = nullptr;
    QWidget* expandedWidget_ = nullptr;
    QTimer clockTimer_;
    DigitalTwinMapDisplaySettings mapDisplaySettings_;

    bool streamSessionStarted_ = false;
};
