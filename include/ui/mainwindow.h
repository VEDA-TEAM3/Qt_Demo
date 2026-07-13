#pragma once

#include <QMainWindow>
#include <QVector>
#include <memory>

#include "model/DigitalTwinTypes.h"
#include "model/StreamConfig.h"

class ClickableVideoWidget;
class DigitalTwinObjectTableModel;
class QShowEvent;
class QStyledItemDelegate;
class QTableView;
class QThread;
class QVBoxLayout;
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
    void setupObjectListTable();
    void setupStreamConfigs();
    void setupReceivers();
    void startReceivers();
    void startReceiverSequentially(int receiverIndex);
    void setupVideoViewEvents();
    void updateObjectListTable(QVector<DigitalTwinObject> objects);
    void toggleExpandVideo(QWidget* targetWidget);
    void expandVideo(QWidget* targetWidget);
    void restoreVideoGrid();
    void updateCameraSelectionLabel(QWidget* targetWidget);

private:
    std::shared_ptr<Ui::MainWindow> ui_;
    std::shared_ptr<StreamReceiverFactory> streamReceiverFactory_;

    QVector<ReceiverWorker> receiverWorkers_;
    bool receiversStarted_ = false;

    QVector<QWidget*> videoWidgets_;
    QVector<StreamConfig> streamConfigs_;
    std::shared_ptr<QVBoxLayout> objectListLayout_;
    std::shared_ptr<DigitalTwinObjectTableModel> objectListModel_;
    std::shared_ptr<QTableView> objectListTable_;
    std::shared_ptr<QStyledItemDelegate> objectTypeDelegate_;
    QWidget* expandedWidget_ = nullptr;
};
