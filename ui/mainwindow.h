#pragma once

#include <QMainWindow>
#include <QVector>
#include <memory>

#include "../model/StreamConfig.h"

class GstRtspReceiver;
class QShowEvent;
class QWidget;
class ClickableVideoWidget;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

/** 대시보드 UI와 카메라 스트림 수신기를 소유하는 메인 윈도우입니다. */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /** 대시보드 레이아웃, 스트림 설정, 수신기를 초기화합니다. */
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    /** 창이 실제로 표시된 뒤 네이티브 비디오 핸들이 준비되면 스트림 수신을 시작합니다. */
    void showEvent(QShowEvent* event) override;

private:
    /** 대시보드 화면의 고정 높이, 여백, stretch 비율을 적용합니다. */
    void setupDashboardLayout();

    /** RTSP 주소와 표시 이름을 한 카메라 설정 목록으로 구성합니다. */
    void setupStreamConfigs();

    /** 활성화된 카메라 설정마다 GStreamer 수신기를 생성하고 UI 신호를 연결합니다. */
    void setupReceivers();

    /** 여러 카메라가 동시에 붙지 않도록 첫 프레임 도착 기준으로 수신기를 순차 시작합니다. */
    void startReceivers();

    /** 지정한 순번의 수신기를 시작하고 안정화되면 다음 수신기로 넘어갑니다. */
    void startReceiverSequentially(int receiverIndex);

    /** 비디오 위젯 목록을 구성하고 더블클릭 확대 이벤트를 연결합니다. */
    void setupVideoViewEvents();

    /** 현재 2x2 보기와 단일 확대 보기를 전환합니다. */
    void toggleExpandVideo(QWidget* targetWidget);

    /** 선택된 비디오 위젯 하나를 CCTV 카드 전체 영역으로 확대합니다. */
    void expandVideo(QWidget* targetWidget);

    /** 확대된 비디오 위젯을 원래 2x2 그리드 배치로 되돌립니다. */
    void restoreVideoGrid();

    /** 현재 보기 상태에 맞춰 카메라 선택 라벨을 갱신합니다. */
    void updateCameraSelectionLabel(QWidget* targetWidget);

private:
    std::shared_ptr<Ui::MainWindow> ui;

    QVector<std::shared_ptr<GstRtspReceiver>> receivers_;
    bool receiversStarted_ = false;

    QVector<QWidget*> videoWidgets_;
    QVector<StreamConfig> streamConfigs_;
    QWidget* expandedWidget_ = nullptr;
};
