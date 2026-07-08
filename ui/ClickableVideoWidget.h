#pragma once

#include <QWidget>
#include <memory>

class QFrame;
class QLabel;
class QMouseEvent;
class QResizeEvent;
class QVBoxLayout;

/** 더블클릭 이벤트와 로딩 오버레이를 제공하는 네이티브 비디오 표시 위젯입니다. */
class ClickableVideoWidget : public QWidget {
    Q_OBJECT

public:
    /** GStreamer 영상 출력에 사용할 네이티브 창 핸들과 로딩 오버레이를 준비합니다. */
    explicit ClickableVideoWidget(QWidget* parent = nullptr);

public slots:
    /** 스트림 준비 상태에 따라 로딩 오버레이와 스피너를 표시하거나 숨깁니다. */
    void setLoading(bool loading);

signals:
    /** 사용자가 비디오 영역을 더블클릭했을 때 자기 자신을 함께 전달합니다. */
    void doubleClicked(ClickableVideoWidget* widget);

protected:
    /** Qt 마우스 더블클릭 이벤트를 확대/복구 신호로 변환합니다. */
    void mouseDoubleClickEvent(QMouseEvent* event) override;

    /** 위젯 크기가 바뀌면 로딩 오버레이가 영상 영역 전체를 덮도록 갱신합니다. */
    void resizeEvent(QResizeEvent* event) override;

#ifdef Q_OS_WIN
    /** Windows 네이티브 더블클릭 메시지도 Qt 신호로 변환합니다. */
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    /** Qt 이벤트와 네이티브 이벤트가 중복으로 들어와도 더블클릭 신호를 한 번만 보냅니다. */
    void emitDoubleClickedOnce();

    /** 로딩 오버레이 위치와 크기를 현재 비디오 위젯 영역에 맞춥니다. */
    void updateLoadingOverlayGeometry();

private:
    std::shared_ptr<QFrame> loadingOverlay_;
    std::shared_ptr<QWidget> loadingSpinner_;
    std::shared_ptr<QLabel> loadingLabel_;
    std::shared_ptr<QVBoxLayout> loadingLayout_;

    qint64 lastDoubleClickMsec_ = 0;
};
