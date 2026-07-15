#include "ui/mainwindow.h"

#include <algorithm>

#include <QDebug>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <memory>
#include <utility>

#include "network/DeviceStatusGatewayFactory.h"
#include "network/DeviceStatusService.h"
#include "network/rtsp.h"
#include "ui/ClickableVideoWidget.h"
#include "ui/DashboardLayout.h"
#include "ui/DigitalTwinMapWidget.h"
#include "ui/panels/DashboardPanelCoordinator.h"
#include "ui/panels/DashboardPanelFactory.h"
#include "ui/panels/DeviceStatusPanel.h"
#include "ui/panels/EventLogPanel.h"
#include "ui/panels/ObjectListPanel.h"
#include "ui_mainwindow.h"
#include "video/StreamReceiverFactory.h"
#include "video/StreamSessionManager.h"

namespace {
constexpr int initialStreamStartDelayMsec = 1000;
constexpr int requiredCctvChannelCount = 4;
const QString normalStatusColor = QStringLiteral("#38e86a");
const QString disconnectedStatusColor = QStringLiteral("#ff4b4b");
}  // namespace

/**
 * @brief                       메인 UI를 구성하고 주입된 외부 연동 구현을 연결합니다.
 * @param streamReceiverFactory 영상 수신기 생성 factory
 * @param deviceStatusGatewayFactory 장비 상태 gateway 생성 factory
 * @param dashboardPanelFactory 대시보드 패널 생성 factory
 * @param parent                부모 위젯
 */
MainWindow::MainWindow(std::shared_ptr<StreamReceiverFactory> streamReceiverFactory,
                       std::shared_ptr<DeviceStatusGatewayFactory> deviceStatusGatewayFactory,
                       std::shared_ptr<DashboardPanelFactory> dashboardPanelFactory, QWidget* parent)
    : QMainWindow(parent),
      ui_(std::make_shared<Ui::MainWindow>()),
      deviceStatusGatewayFactory_(std::move(deviceStatusGatewayFactory)),
      dashboardPanelFactory_(std::move(dashboardPanelFactory)) {
    ui_->setupUi(this);

    setupStreamConfigs();
    setupDashboardLayout();
    setupTopBarStatuses();
    setupDashboardPanels();
    setupDashboardPanelCoordinator();
    setupDeviceStatusService();
    setupVideoViewEvents();
    setupStreamSessionManager(std::move(streamReceiverFactory));
}

/**
 * @brief   대시보드에서 사용할 카메라 스트림 설정을 구성합니다.
 */
void MainWindow::setupStreamConfigs() {
    streamConfigs_ = {{QStringLiteral("cam-01"), QStringLiteral("제 1구역"), Network::Rtsp::zone1(), 0, true},
                      {QStringLiteral("cam-02"), QStringLiteral("제 2구역"), Network::Rtsp::zone2(), 1, true},
                      {QStringLiteral("cam-03"), QStringLiteral("제 3구역"), Network::Rtsp::zone3(), 2, true},
                      {QStringLiteral("cam-04"), QStringLiteral("제 4구역"), Network::Rtsp::zone4(), 3, true}};
}

/**
 * @brief   대시보드 레이아웃 정책을 적용하고 카메라 선택 라벨을 초기화합니다.
 */
void MainWindow::setupDashboardLayout() {
    DashboardLayout::apply(this, ui_.get());
    ui_->titleLabel->setTextFormat(Qt::RichText);
    ui_->titleLabel->setText(
        QStringLiteral("<span style=\"color:#a8d4ff;\">Wise AI</span>"
                       "<span style=\"color:#ffffff;\"> 기반 주차장 디지털 트윈 관제 시스템</span>"));

    const QPixmap settingsIcon(QStringLiteral(":/icons/config_icon.png"));
    ui_->settingsLabel->setText({});
    ui_->settingsLabel->setPixmap(settingsIcon.scaled(QSize(32, 32), Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
    ui_->settingsLabel->setAlignment(Qt::AlignCenter);
    ui_->settingsLabel->setFixedSize(72, 54);
    ui_->settingsLabel->setToolTip(QStringLiteral("설정"));
    updateCameraSelectionLabel(nullptr);
}

/**
 * @brief   상단 시스템 및 CCTV 연결 상태를 연결 대기 상태로 초기화합니다.
 */
void MainWindow::setupTopBarStatuses() {
    updateSystemStatus(false);
    streamChannelReady_.fill(false, requiredCctvChannelCount);
    updateStreamConnectionStatus();
}

/**
 * @brief            MQTT 프로토콜 연결 여부를 상단 시스템 상태에 반영합니다.
 * @param connected  MQTT broker와 정상적으로 연결되었다면 true
 */
void MainWindow::updateSystemStatus(bool connected) {
    setTopBarStatus(ui_->systemStatusLabel, QStringLiteral("시스템 상태"),
                    connected ? QStringLiteral("● 정상") : QStringLiteral("● 연결 중"),
                    connected ? normalStatusColor : disconnectedStatusColor);
}

/**
 * @brief   네 CCTV 채널이 모두 첫 프레임을 수신했는지 상단 연결 상태에 반영합니다.
 */
void MainWindow::updateStreamConnectionStatus() {
    const bool allStreamsReady = streamChannelReady_.size() == requiredCctvChannelCount &&
                                 std::all_of(streamChannelReady_.cbegin(), streamChannelReady_.cend(),
                                             [](bool ready) { return ready; });

    setTopBarStatus(ui_->connectionStatusLabel, QStringLiteral("연결 상태"),
                    allStreamsReady ? QStringLiteral("● 연결됨") : QStringLiteral("● 연결 중"),
                    allStreamsReady ? normalStatusColor : disconnectedStatusColor);
}

/**
 * @brief         상단 상태 QLabel에서 제목과 상태 문구를 서로 다른 색으로 표시합니다.
 * @param label   갱신할 상단 상태 QLabel
 * @param title   항상 기본 글자색으로 표시할 상태 제목
 * @param status  상태 점을 포함한 상태 문구
 * @param color   상태 문구에 적용할 RGB 색상 문자열
 */
void MainWindow::setTopBarStatus(QLabel* label, const QString& title, const QString& status, const QString& color) {
    if (!label) {
        return;
    }

    label->setTextFormat(Qt::RichText);
    label->setText(QStringLiteral("<span style=\"color:#d5dfec;font-weight:800;\">%1</span>"
                                  "&nbsp;&nbsp;<span style=\"color:%2;font-weight:800;\">%3</span>")
                       .arg(title, color, status));
}

/**
 * @brief   주입된 factory를 통해 대시보드 패널을 생성하고 placeholder에 배치합니다.
 */
void MainWindow::setupDashboardPanels() {
    if (!dashboardPanelFactory_) {
        qWarning() << "[MainWindow] Dashboard panel factory is not configured";
        return;
    }

    ui_->deviceStatusTitleLabel->setVisible(false);

    DashboardPanelHosts hosts;
    hosts.deviceStatusHost = ui_->deviceStatusBodyFrame;
    hosts.eventLogHost = ui_->eventLogBodyFrame;
    hosts.objectListHost = ui_->objectListBodyFrame;

    const DashboardPanels panels = dashboardPanelFactory_->createPanels(hosts);
    deviceStatusPanel_ = panels.deviceStatusPanel;
    eventLogPanel_ = panels.eventLogPanel;
    objectListPanel_ = panels.objectListPanel;
}

/**
 * @brief   패널 데이터 갱신 경로를 MainWindow 밖의 coordinator에 연결합니다.
 */
void MainWindow::setupDashboardPanelCoordinator() {
    if (dashboardPanelCoordinator_) {
        return;
    }

    dashboardPanelCoordinator_ =
        new DashboardPanelCoordinator(deviceStatusPanel_, eventLogPanel_, objectListPanel_, this);

    if (!ui_->digitalTwinMapWidget) {
        return;
    }

    connect(ui_->digitalTwinMapWidget, &DigitalTwinMapWidget::simulationSnapshotUpdated, dashboardPanelCoordinator_,
            &DashboardPanelCoordinator::consumeDigitalTwinSnapshot, Qt::QueuedConnection);
}

/**
 * @brief   장비 상태 service를 생성해 coordinator에 연결하고 worker를 시작합니다.
 */
void MainWindow::setupDeviceStatusService() {
    if (deviceStatusService_) {
        return;
    }

    if (!deviceStatusGatewayFactory_) {
        qWarning() << "[MainWindow] Device status gateway factory is not configured";
        return;
    }

    deviceStatusService_ = std::make_shared<DeviceStatusService>(deviceStatusGatewayFactory_);

    connect(deviceStatusService_.get(), &DeviceStatusService::brokerConnectionChanged, this,
            &MainWindow::updateSystemStatus, Qt::QueuedConnection);

    if (dashboardPanelCoordinator_) {
        dashboardPanelCoordinator_->bindDeviceStatusService(deviceStatusService_.get());
    }

    deviceStatusService_->start();
}

/**
 * @brief 외부 연동 service와 영상 스트림 세션을 종료합니다.
 */
MainWindow::~MainWindow() {
    if (streamSessionManager_) {
        streamSessionManager_->stop();
    }

    if (deviceStatusService_) {
        deviceStatusService_->stop();
    }
}

/**
 * @brief       창 크기 변화에 맞춰 하단 패널 높이와 목록 컬럼 폭을 보정합니다.
 * @param event  Qt resize 이벤트
 */
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateDashboardAdaptiveSizes();
}

/**
 * @brief       창이 처음 표시된 뒤 RTSP 스트림 세션을 시작합니다.
 * @param event Qt show 이벤트
 */
void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    updateDashboardAdaptiveSizes();

    if (streamSessionStarted_ || !streamSessionManager_) {
        return;
    }

    streamSessionStarted_ = true;

    QTimer::singleShot(initialStreamStartDelayMsec, this, [this]() {
        if (streamSessionManager_) {
            streamSessionManager_->start();
        }
    });
}

/**
 * @brief   현재 창 크기에 맞춰 대시보드 하단 영역과 객체 목록 폭을 보정합니다.
 */
void MainWindow::updateDashboardAdaptiveSizes() { DashboardLayout::adjustBottomSectionHeight(this, ui_.get()); }

/**
 * @brief 4분할 영상 위젯의 더블클릭 이벤트를 확대/복구 동작에 연결합니다.
 */
void MainWindow::setupVideoViewEvents() {
    videoWidgets_ = {
        ui_->camView1,
        ui_->camView2,
        ui_->camView3,
        ui_->camView4,
    };

    auto* grid = ui_->videoGridLayout;

    if (!grid) {
        qWarning() << "[MainWindow] videoGridLayout is null";
        return;
    }

    videoTileFrames_.reserve(videoWidgets_.size());

    for (qsizetype index = 0; index < videoWidgets_.size(); ++index) {
        auto* widget = videoWidgets_[index];

        if (!widget) {
            continue;
        }

        auto* clickable = qobject_cast<ClickableVideoWidget*>(widget);

        if (!clickable) {
            qDebug() << "[MainWindow] Not ClickableVideoWidget:" << widget->objectName();
            continue;
        }

        grid->removeWidget(widget);

        auto* tileFrame = new QFrame(ui_->cctvCard);
        tileFrame->setObjectName(QStringLiteral("videoTileFrame"));
        tileFrame->setProperty("hovered", false);
        tileFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        auto* tileLayout = new QVBoxLayout(tileFrame);
        tileLayout->setContentsMargins(2, 2, 2, 2);
        tileLayout->setSpacing(0);
        tileLayout->addWidget(widget);

        grid->addWidget(tileFrame, static_cast<int>(index / 2), static_cast<int>(index % 2));
        videoTileFrames_.append(tileFrame);

        connect(clickable, &ClickableVideoWidget::doubleClicked, this,
                [this](ClickableVideoWidget* target) { toggleExpandVideo(target); });
        connect(clickable, &ClickableVideoWidget::hoverChanged, tileFrame, [tileFrame](bool hovered) {
            tileFrame->setProperty("hovered", hovered);
            tileFrame->style()->unpolish(tileFrame);
            tileFrame->style()->polish(tileFrame);
            tileFrame->update();
        });
    }
}

/**
 * @brief                  영상 출력 창과 스트림 설정을 세션 관리자에 연결합니다.
 * @param receiverFactory  채널별 영상 수신기 생성 factory
 */
void MainWindow::setupStreamSessionManager(std::shared_ptr<StreamReceiverFactory> receiverFactory) {
    if (streamSessionManager_) {
        return;
    }

    if (!receiverFactory) {
        qWarning() << "[MainWindow] Stream receiver factory is not configured";
        return;
    }

    streamSessionManager_ = new StreamSessionManager(std::move(receiverFactory), this);

    connect(streamSessionManager_, &StreamSessionManager::loadingChanged, this, [this](int channelIndex, bool loading) {
        if (channelIndex < 0 || channelIndex >= videoWidgets_.size()) {
            qWarning() << "[MainWindow] Invalid loading channel index:" << channelIndex;
            return;
        }

        auto* videoWidget = qobject_cast<ClickableVideoWidget*>(videoWidgets_[channelIndex]);

        if (videoWidget) {
            videoWidget->setLoading(loading);
        }

        if (loading && channelIndex < streamChannelReady_.size()) {
            streamChannelReady_[channelIndex] = false;
            updateStreamConnectionStatus();
        }
    });

    connect(streamSessionManager_, &StreamSessionManager::errorOccurred, this,
            [this](int channelIndex, const QString& error) {
                qWarning().noquote() << QStringLiteral("[Channel %1] %2").arg(channelIndex + 1).arg(error);

                if (channelIndex >= 0 && channelIndex < streamChannelReady_.size()) {
                    streamChannelReady_[channelIndex] = false;
                    updateStreamConnectionStatus();
                }
            });

    connect(streamSessionManager_, &StreamSessionManager::firstFrameReceived, this, [this](int channelIndex) {
        if (channelIndex < 0 || channelIndex >= streamChannelReady_.size()) {
            return;
        }

        streamChannelReady_[channelIndex] = true;
        updateStreamConnectionStatus();
    });

    QVector<StreamOutputBinding> bindings;

    const qsizetype streamCount = qMin(videoWidgets_.size(), streamConfigs_.size());

    bindings.reserve(streamCount);

    for (qsizetype index = 0; index < streamCount; ++index) {
        QWidget* outputWidget = videoWidgets_[index];
        const StreamConfig& config = streamConfigs_[index];

        if (!outputWidget || !config.enabled) {
            continue;
        }

        outputWidget->setAttribute(Qt::WA_NativeWindow);
        outputWidget->setAttribute(Qt::WA_DontCreateNativeAncestors);

        if (auto* videoWidget = qobject_cast<ClickableVideoWidget*>(outputWidget)) {
            videoWidget->setLoading(true);
        }

        StreamOutputBinding binding;
        binding.config = config;
        binding.outputWindowHandle = outputWidget->winId();

        bindings.append(std::move(binding));
    }

    streamSessionManager_->configure(std::move(bindings));
}

/**
 * @brief              현재 확대 상태에 따라 대상 영상을 확대하거나 4분할로 복구합니다.
 * @param targetWidget  더블클릭된 영상 위젯
 */
void MainWindow::toggleExpandVideo(QWidget* targetWidget) {
    if (!targetWidget) {
        return;
    }

    if (expandedWidget_ == nullptr) {
        expandVideo(targetWidget);
    } else {
        restoreVideoGrid();
    }
}

/**
 * @brief              선택한 영상 위젯을 CCTV 영역 전체로 확장합니다.
 * @param targetWidget  확대할 영상 위젯
 */
void MainWindow::expandVideo(QWidget* targetWidget) {
    auto* grid = ui_->videoGridLayout;

    if (!grid) {
        qDebug() << "[MainWindow] videoGridLayout is null";
        return;
    }

    const qsizetype targetIndex = videoWidgets_.indexOf(targetWidget);

    if (targetIndex < 0 || targetIndex >= videoTileFrames_.size()) {
        qWarning() << "[MainWindow] Video tile frame is not configured";
        return;
    }

    QFrame* targetFrame = videoTileFrames_[targetIndex];

    for (auto* frame : videoTileFrames_) {
        if (frame && frame != targetFrame) {
            frame->hide();
        }
    }

    grid->removeWidget(targetFrame);
    grid->addWidget(targetFrame, 0, 0, 2, 2);

    targetFrame->show();
    targetFrame->raise();

    expandedWidget_ = targetWidget;
    updateCameraSelectionLabel(targetWidget);
}

/**
 * @brief   확대된 영상을 원래 2x2 영상 그리드로 복구합니다.
 */
void MainWindow::restoreVideoGrid() {
    auto* grid = ui_->videoGridLayout;

    if (!grid) {
        qDebug() << "[MainWindow] videoGridLayout is null";
        return;
    }

    for (auto* frame : videoTileFrames_) {
        if (frame) {
            grid->removeWidget(frame);
        }
    }

    if (videoTileFrames_.size() == 4) {
        grid->addWidget(videoTileFrames_[0], 0, 0);
        grid->addWidget(videoTileFrames_[1], 0, 1);
        grid->addWidget(videoTileFrames_[2], 1, 0);
        grid->addWidget(videoTileFrames_[3], 1, 1);
    }

    for (auto* frame : videoTileFrames_) {
        if (frame) {
            frame->show();
        }
    }

    expandedWidget_ = nullptr;
    updateCameraSelectionLabel(nullptr);
}

/**
 * @brief              현재 선택된 영상 구역명을 헤더 라벨에 반영합니다.
 * @param targetWidget  선택된 영상 위젯, nullptr이면 전체 구역
 */
void MainWindow::updateCameraSelectionLabel(QWidget* targetWidget) {
    if (!ui_->cameraSelectLabel) {
        return;
    }

    if (!targetWidget) {
        ui_->cameraSelectLabel->setText(QStringLiteral("전체 구역"));
        return;
    }

    const qsizetype index = videoWidgets_.indexOf(targetWidget);

    if (index < 0 || index >= streamConfigs_.size()) {
        ui_->cameraSelectLabel->setText(QStringLiteral("전체 구역"));
        return;
    }

    ui_->cameraSelectLabel->setText(streamConfigs_[index].name);
}
