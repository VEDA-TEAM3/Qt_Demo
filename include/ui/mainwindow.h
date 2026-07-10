#pragma once

#include <QMainWindow>
#include <QVector>
#include <memory>

#include "model/StreamConfig.h"

class ClickableVideoWidget;
class QShowEvent;
class QThread;
class StreamReceiver;
class StreamReceiverFactory;
class QWidget;

struct ReceiverWorker {
    std::shared_ptr<QThread> thread;
    std::shared_ptr<StreamReceiver> receiver;
};

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void setupDashboardLayout();
    void setupStreamConfigs();
    void setupReceivers();
    void startReceivers();
    void startReceiverSequentially(int receiverIndex);
    void setupVideoViewEvents();
    void toggleExpandVideo(QWidget* targetWidget);
    void expandVideo(QWidget* targetWidget);
    void restoreVideoGrid();
    void updateCameraSelectionLabel(QWidget* targetWidget);

private:
    std::shared_ptr<Ui::MainWindow> ui;
    std::shared_ptr<StreamReceiverFactory> streamReceiverFactory_;

    QVector<ReceiverWorker> receiverWorkers_;
    bool receiversStarted_ = false;

    QVector<QWidget*> videoWidgets_;
    QVector<StreamConfig> streamConfigs_;
    QWidget* expandedWidget_ = nullptr;
};
