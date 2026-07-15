#pragma once

#include <QMainWindow>
#include <QVector>
#include <memory>

#include "model/StreamConfig.h"

class ClickableVideoWidget;
class DashboardPanelCoordinator;
class DashboardPanelFactory;
class DeviceStatusGatewayFactory;
class DeviceStatusPanel;
class DeviceStatusService;
class EventLogPanel;
class ObjectListPanel;
class QFrame;
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
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupDashboardLayout();
    void setupDashboardPanels();
    void setupDashboardPanelCoordinator();
    void setupDeviceStatusService();
    void setupStreamConfigs();
    void setupStreamSessionManager(std::shared_ptr<StreamReceiverFactory> receiverFactory);
    void setupVideoViewEvents();

    void updateDashboardAdaptiveSizes();

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

    StreamSessionManager* streamSessionManager_ = nullptr;
    DashboardPanelCoordinator* dashboardPanelCoordinator_ = nullptr;
    DeviceStatusPanel* deviceStatusPanel_ = nullptr;
    EventLogPanel* eventLogPanel_ = nullptr;
    ObjectListPanel* objectListPanel_ = nullptr;
    QWidget* expandedWidget_ = nullptr;

    bool streamSessionStarted_ = false;
};
