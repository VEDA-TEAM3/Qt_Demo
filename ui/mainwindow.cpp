#include "mainwindow.h"

#include <QDebug>
#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QMetaObject>
#include <QShowEvent>
#include <QSizePolicy>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <memory>

#include "../network/rtsp.h"
#include "../video/GstRtspReceiver.h"
#include "ClickableVideoWidget.h"
#include "DashboardLayout.h"
#include "ui_mainwindow.h"

namespace {
constexpr int initialReceiverStartDelayMsec = 1000;
constexpr int receiverStartSpacingMsec = 2000;
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(std::make_shared<Ui::MainWindow>()) {
    ui->setupUi(this);

    setupStreamConfigs();
    setupDashboardLayout();
    setupVideoViewEvents();
    setupReceivers();
}

void MainWindow::setupStreamConfigs() {
    streamConfigs_ = {{QStringLiteral("cam-01"), QStringLiteral("제 1구역"), Network::Rtsp::zone1, 0, true},
                      {QStringLiteral("cam-02"), QStringLiteral("제 2구역"), Network::Rtsp::zone2, 1, true},
                      {QStringLiteral("cam-03"), QStringLiteral("제 3구역"), Network::Rtsp::zone3, 2, true},
                      {QStringLiteral("cam-04"), QStringLiteral("제 4구역"), Network::Rtsp::zone4, 3, true}};
}

void MainWindow::setupDashboardLayout() {
    DashboardLayout::apply(this, ui.get());
    updateCameraSelectionLabel(nullptr);
}

MainWindow::~MainWindow() {
    QThread* mainThread = QThread::currentThread();

    for (const auto& worker : receiverWorkers_) {
        const auto& receiver = worker.receiver;

        if (!receiver) {
            continue;
        }

        if (receiver->thread() && receiver->thread() != mainThread && receiver->thread()->isRunning()) {
            QMetaObject::invokeMethod(
                receiver.get(), [receiver = receiver.get(), mainThread]() {
                    receiver->stop();
                    receiver->moveInternalObjectsToThread(mainThread);
                },
                Qt::BlockingQueuedConnection);
        } else {
            receiver->stop();
            receiver->moveInternalObjectsToThread(mainThread);
        }
    }

    for (const auto& worker : receiverWorkers_) {
        const auto& thread = worker.thread;

        if (!thread) {
            continue;
        }

        thread->quit();
        thread->wait();
    }

    receiverWorkers_.clear();
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);

    if (!receiversStarted_) {
        receiversStarted_ = true;

        QTimer::singleShot(initialReceiverStartDelayMsec, this, [this]() { startReceivers(); });
    }
}

void MainWindow::setupVideoViewEvents() {
    videoWidgets_ = {ui->camView1, ui->camView2, ui->camView3, ui->camView4};

    for (auto* widget : videoWidgets_) {
        if (!widget) {
            continue;
        }

        widget->setAttribute(Qt::WA_NativeWindow);

        auto* clickable = qobject_cast<ClickableVideoWidget*>(widget);

        if (!clickable) {
            qDebug() << "[MainWindow] Not ClickableVideoWidget:" << widget->objectName();
            continue;
        }

        connect(clickable, &ClickableVideoWidget::doubleClicked, this,
                [this](ClickableVideoWidget* target) { toggleExpandVideo(target); });
    }
}

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

void MainWindow::expandVideo(QWidget* targetWidget) {
    auto* grid = ui->videoGridLayout;

    if (!grid) {
        qDebug() << "[MainWindow] videoGridLayout is null";
        return;
    }

    for (auto* widget : videoWidgets_) {
        if (widget && widget != targetWidget) {
            widget->hide();
        }
    }

    grid->removeWidget(targetWidget);
    grid->addWidget(targetWidget, 0, 0, 2, 2);

    targetWidget->show();
    targetWidget->raise();

    expandedWidget_ = targetWidget;
    updateCameraSelectionLabel(targetWidget);

    // qDebug() << "[MainWindow] Expanded:" << targetWidget->objectName();
}

void MainWindow::restoreVideoGrid() {
    auto* grid = ui->videoGridLayout;

    if (!grid) {
        qDebug() << "[MainWindow] videoGridLayout is null";
        return;
    }

    for (auto* widget : videoWidgets_) {
        if (widget) {
            grid->removeWidget(widget);
        }
    }

    grid->addWidget(ui->camView1, 0, 0);
    grid->addWidget(ui->camView2, 0, 1);
    grid->addWidget(ui->camView3, 1, 0);
    grid->addWidget(ui->camView4, 1, 1);

    for (auto* widget : videoWidgets_) {
        if (widget) {
            widget->show();
        }
    }

    expandedWidget_ = nullptr;
    updateCameraSelectionLabel(nullptr);

    // qDebug() << "[MainWindow] Restored 2x2 grid";
}

void MainWindow::updateCameraSelectionLabel(QWidget* targetWidget) {
    if (!ui->cameraSelectLabel) {
        return;
    }

    if (!targetWidget) {
        ui->cameraSelectLabel->setText(QStringLiteral("전체 구역"));
        return;
    }

    const int index = videoWidgets_.indexOf(targetWidget);

    if (index < 0 || index >= streamConfigs_.size()) {
        ui->cameraSelectLabel->setText(QStringLiteral("전체 구역"));
        return;
    }

    ui->cameraSelectLabel->setText(streamConfigs_[index].name);
}

void MainWindow::setupReceivers() {
    const int streamCount = qMin(videoWidgets_.size(), streamConfigs_.size());

    for (int i = 0; i < streamCount; ++i) {
        const auto& config = streamConfigs_[i];

        if (!config.enabled) {
            continue;
        }

        QWidget* outputWidget = videoWidgets_[i];

        if (!outputWidget) {
            continue;
        }

        outputWidget->setAttribute(Qt::WA_NativeWindow);
        outputWidget->setAttribute(Qt::WA_DontCreateNativeAncestors);

        const auto outputWindowHandle = static_cast<guintptr>(outputWidget->winId());
        auto receiverThread = std::make_shared<QThread>();
        receiverThread->setObjectName(QString("%1-worker").arg(config.cameraId));

        auto receiver = std::make_shared<GstRtspReceiver>(outputWindowHandle);
        receiver->setUrl(config.url);
        receiver->moveInternalObjectsToThread(receiverThread.get());

        connect(receiver.get(), &GstRtspReceiver::statusChanged, this, [config](const QString& status) {
            qDebug().noquote() << QString("[%1 Status]").arg(config.cameraId) << config.name << status;
        });

        connect(receiver.get(), &GstRtspReceiver::errorOccurred, this, [config](const QString& error) {
            qDebug().noquote() << QString("[%1 Error]").arg(config.cameraId) << config.name << error;
        });

        if (auto* videoWidget = qobject_cast<ClickableVideoWidget*>(outputWidget)) {
            videoWidget->setLoading(true);

            connect(receiver.get(), &GstRtspReceiver::loadingChanged, videoWidget, &ClickableVideoWidget::setLoading);
        }

        receiverThread->start();

        receiverWorkers_.append({receiverThread, receiver});
    }
}

void MainWindow::startReceivers() {
    startReceiverSequentially(0);
}

void MainWindow::startReceiverSequentially(int receiverIndex) {
    if (receiverIndex >= receiverWorkers_.size()) {
        qDebug().noquote() << "[MainWindow] All receivers requested";
        return;
    }

    const auto receiver = receiverWorkers_[receiverIndex].receiver;

    if (!receiver) {
        QTimer::singleShot(0, this, [this, receiverIndex]() { startReceiverSequentially(receiverIndex + 1); });
        return;
    }

    qDebug().noquote() << QString("[CAM-%1] start").arg(receiverIndex + 1);
    QMetaObject::invokeMethod(receiver.get(), [receiver]() { receiver->start(); }, Qt::QueuedConnection);

    QTimer::singleShot(receiverStartSpacingMsec, this,
                       [this, receiverIndex]() { startReceiverSequentially(receiverIndex + 1); });
}
