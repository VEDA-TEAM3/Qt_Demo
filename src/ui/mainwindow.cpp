#include "ui/mainwindow.h"

#include <QAbstractItemView>
#include <QDebug>
#include <QEvent>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPainter>
#include <QShowEvent>
#include <QSize>
#include <QSizePolicy>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <memory>
#include <utility>

#include "network/rtsp.h"
#include "ui/ClickableVideoWidget.h"
#include "ui/DashboardLayout.h"
#include "ui/DigitalTwinMapWidget.h"
#include "ui/DigitalTwinObjectTableModel.h"
#include "ui_mainwindow.h"
#include "video/StreamReceiver.h"
#include "video/StreamReceiverFactory.h"

namespace {
constexpr int initialReceiverStartDelayMsec = 1000;
constexpr int receiverStartSpacingMsec = 3000;
constexpr int objectIdColumn = 0;
constexpr int objectTypeColumn = 1;
constexpr int objectPositionColumn = 2;
constexpr int objectAreaColumn = 3;
constexpr int objectIdColumnWidth = 64;
constexpr int objectTypeColumnWidth = 98;
constexpr int objectPositionColumnWidth = 104;
constexpr int objectAreaColumnWidth = 74;
constexpr int objectTypeColumnIndent = 10;

class ObjectTypeItemDelegate final : public QStyledItemDelegate {
public:
    /**
     * @brief        유형 컬럼 전용 delegate를 생성합니다.
     * @param parent  Qt 객체 소유권을 연결할 부모 객체
     */
    explicit ObjectTypeItemDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    /**
     * @brief         유형 컬럼의 아이콘과 텍스트를 살짝 오른쪽으로 옮겨 그립니다.
     * @param painter  셀을 그릴 painter
     * @param option   기본 셀 표시 옵션
     * @param index    그릴 model index
     */
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem adjustedOption(option);
        adjustedOption.rect.adjust(objectTypeColumnIndent, 0, 0, 0);
        QStyledItemDelegate::paint(painter, adjustedOption, index);
    }
};
}  // namespace

/**
 * @brief       메인 UI를 구성하고 스트림 수신기 준비 작업을 수행합니다.
 * @param parent  부모 위젯
 */
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      ui_(std::make_shared<Ui::MainWindow>()),
      streamReceiverFactory_(std::make_shared<GstStreamReceiverFactory>()) {
    ui_->setupUi(this);

    setupStreamConfigs();
    setupDashboardLayout();
    setupObjectListTable();
    setupVideoViewEvents();
    setupReceivers();

    connect(ui_->digitalTwinMapWidget, &DigitalTwinMapWidget::objectListUpdated, this,
            &MainWindow::updateObjectListTable, Qt::QueuedConnection);
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
    updateCameraSelectionLabel(nullptr);
}

/**
 * @brief   실시간 객체 목록 테이블을 생성하고 표시 형식을 설정합니다.
 */
void MainWindow::setupObjectListTable() {
    if (!ui_->objectListBodyFrame || objectListTable_) {
        return;
    }

    objectListLayout_ = std::make_shared<QVBoxLayout>(ui_->objectListBodyFrame);
    objectListLayout_->setContentsMargins(0, 0, 0, 0);
    objectListLayout_->setSpacing(0);

    objectListModel_ = std::make_shared<DigitalTwinObjectTableModel>(ui_->objectListBodyFrame);
    objectListTable_ = std::make_shared<QTableView>(ui_->objectListBodyFrame);
    objectTypeDelegate_ = std::make_shared<ObjectTypeItemDelegate>(objectListTable_.get());

    objectListTable_->setObjectName(QStringLiteral("objectListTable"));
    objectListTable_->setModel(objectListModel_.get());
    objectListTable_->setItemDelegateForColumn(objectTypeColumn, objectTypeDelegate_.get());
    objectListTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    objectListTable_->setFocusPolicy(Qt::NoFocus);
    objectListTable_->setSelectionMode(QAbstractItemView::NoSelection);
    objectListTable_->setShowGrid(false);
    objectListTable_->setAlternatingRowColors(false);
    objectListTable_->setIconSize(QSize(18, 18));
    objectListTable_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    objectListTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    objectListTable_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    objectListTable_->verticalHeader()->setVisible(false);
    objectListTable_->verticalHeader()->setDefaultSectionSize(30);
    objectListTable_->horizontalHeader()->setHighlightSections(false);
    objectListTable_->horizontalHeader()->setStretchLastSection(false);
    objectListTable_->horizontalHeader()->setSectionsMovable(false);
    objectListTable_->horizontalHeader()->setSectionsClickable(false);
    objectListTable_->horizontalHeader()->setMinimumSectionSize(40);
    objectListTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    objectListTable_->setColumnWidth(objectIdColumn, objectIdColumnWidth);
    objectListTable_->setColumnWidth(objectTypeColumn, objectTypeColumnWidth);
    objectListTable_->setColumnWidth(objectPositionColumn, objectPositionColumnWidth);
    objectListTable_->setColumnWidth(objectAreaColumn, objectAreaColumnWidth);

    objectListLayout_->addWidget(objectListTable_.get());
}

/**
 * @brief   실행 중인 RTSP 수신기와 worker 스레드를 안전하게 종료합니다.
 */
MainWindow::~MainWindow() {
    QThread* mainThread = QThread::currentThread();

    for (const auto& worker : receiverWorkers_) {
        const auto& receiver = worker.receiver;

        if (!receiver) {
            continue;
        }

        if (receiver->thread() && receiver->thread() != mainThread && receiver->thread()->isRunning()) {
            QMetaObject::invokeMethod(
                receiver.get(),
                [receiver = receiver.get(), mainThread]() {
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

/**
 * @brief       창이 처음 표시된 뒤 일정 간격으로 RTSP 수신기를 시작합니다.
 * @param event  Qt show 이벤트
 */
void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);

    if (!receiversStarted_) {
        receiversStarted_ = true;

        QTimer::singleShot(initialReceiverStartDelayMsec, this, [this]() { startReceivers(); });
    }
}

/**
 * @brief   4분할 영상 위젯의 더블클릭 이벤트를 확대/복구 동작에 연결합니다.
 */
void MainWindow::setupVideoViewEvents() {
    videoWidgets_ = {ui_->camView1, ui_->camView2, ui_->camView3, ui_->camView4};

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

/**
 * @brief          디지털 트윈 demo 객체 목록을 실시간 객체 목록 테이블에 반영합니다.
 * @param objects  현재 맵에 살아있는 객체 목록
 */
void MainWindow::updateObjectListTable(QVector<DigitalTwinObject> objects) {
    if (!objectListModel_) {
        return;
    }

    objectListModel_->updateObjects(std::move(objects));
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

    for (auto* widget : videoWidgets_) {
        if (widget) {
            grid->removeWidget(widget);
        }
    }

    grid->addWidget(ui_->camView1, 0, 0);
    grid->addWidget(ui_->camView2, 0, 1);
    grid->addWidget(ui_->camView3, 1, 0);
    grid->addWidget(ui_->camView4, 1, 1);

    for (auto* widget : videoWidgets_) {
        if (widget) {
            widget->show();
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

/**
 * @brief   활성화된 스트림 설정을 기준으로 RTSP 수신기와 전용 worker 스레드를 생성합니다.
 */
void MainWindow::setupReceivers() {
    const qsizetype streamCount = qMin(videoWidgets_.size(), streamConfigs_.size());

    for (qsizetype i = 0; i < streamCount; ++i) {
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

        auto receiver = streamReceiverFactory_->create(outputWindowHandle);
        receiver->setUrl(config.url);
        receiver->moveInternalObjectsToThread(receiverThread.get());

        connect(receiver.get(), &StreamReceiver::statusChanged, this, [config](const QString& status) {
            qDebug().noquote() << QString("[%1 Status]").arg(config.cameraId) << config.name << status;
        });

        connect(receiver.get(), &StreamReceiver::errorOccurred, this, [config](const QString& error) {
            qDebug().noquote() << QString("[%1 Error]").arg(config.cameraId) << config.name << error;
        });

        if (auto* videoWidget = qobject_cast<ClickableVideoWidget*>(outputWidget)) {
            videoWidget->setLoading(true);

            connect(receiver.get(), &StreamReceiver::loadingChanged, videoWidget, &ClickableVideoWidget::setLoading);
        }

        receiverThread->start();

        receiverWorkers_.append({receiverThread, receiver});
    }
}

/**
 * @brief   준비된 RTSP 수신기를 첫 번째 채널부터 순차적으로 시작합니다.
 */
void MainWindow::startReceivers() { startReceiverSequentially(0); }

/**
 * @brief                지정된 수신기를 시작하고 다음 수신기 시작을 예약합니다.
 * @param receiverIndex  시작할 수신기 인덱스
 */
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
