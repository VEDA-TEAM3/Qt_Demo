#include "ui/ClickableVideoWidget.h"

#include <QDateTime>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr qint64 doubleClickDebounceMsec = 250;

/**
 * @brief   영상 로딩 중 중앙에 표시되는 회전 인디케이터 위젯입니다.
 */
class SpinnerWidget : public QWidget {
public:
    /**
     * @brief       스피너 크기와 회전 타이머를 초기화합니다.
     * @param parent  부모 위젯
     */
    explicit SpinnerWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(44, 44);

        timer_.setInterval(80);

        connect(&timer_, &QTimer::timeout, this, [this]() {
            angle_ = (angle_ + 30) % 360;
            update();
        });
    }

    /**
     * @brief          스피너 회전 상태를 변경합니다.
     * @param spinning  true이면 회전 타이머를 시작합니다.
     */
    void setSpinning(bool spinning) {
        if (spinning) {
            timer_.start();
        } else {
            timer_.stop();
        }

        update();
    }

protected:
    /**
     * @brief   현재 회전 각도에 맞춰 원형 로딩 표시를 그립니다.
     */
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRectF ring = rect().adjusted(6, 6, -6, -6);

        QPen basePen(QColor(48, 65, 86), 4, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(basePen);
        painter.drawArc(ring, 0, 360 * 16);

        QPen accentPen(QColor(46, 130, 255), 4, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(accentPen);
        painter.drawArc(ring, angle_ * 16, 115 * 16);
    }

private:
    QTimer timer_;
    int angle_ = 0;
};
}  // namespace

/**
 * @brief       네이티브 영상 출력 영역과 로딩 오버레이를 초기화합니다.
 * @param parent  부모 위젯
 */
ClickableVideoWidget::ClickableVideoWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setProperty("loading", true);

    loadingOverlay_ = std::make_shared<QFrame>(this);
    loadingOverlay_->setObjectName("videoLoadingOverlay");
    loadingOverlay_->setAttribute(Qt::WA_TransparentForMouseEvents);

    loadingLayout_ = std::make_shared<QVBoxLayout>(loadingOverlay_.get());
    loadingLayout_->setContentsMargins(16, 16, 16, 16);
    loadingLayout_->setSpacing(10);
    loadingLayout_->setAlignment(Qt::AlignCenter);

    auto spinner = std::make_shared<SpinnerWidget>(loadingOverlay_.get());
    spinner->setObjectName("videoLoadingSpinner");
    loadingSpinner_ = spinner;

    loadingLabel_ = std::make_shared<QLabel>("Loading stream", loadingOverlay_.get());
    loadingLabel_->setObjectName("videoLoadingLabel");
    loadingLabel_->setAlignment(Qt::AlignCenter);

    loadingLayout_->addWidget(spinner.get(), 0, Qt::AlignCenter);
    loadingLayout_->addWidget(loadingLabel_.get(), 0, Qt::AlignCenter);

    spinner->setSpinning(true);

    updateLoadingOverlayGeometry();
}

/**
 * @brief         스트림 로딩 오버레이의 표시 상태를 변경합니다.
 * @param loading  true이면 로딩 오버레이와 스피너를 표시합니다.
 */
void ClickableVideoWidget::setLoading(bool loading) {
    setProperty("loading", loading);

    if (loadingOverlay_) {
        loadingOverlay_->setVisible(loading);
        loadingOverlay_->raise();
    }

    if (loadingSpinner_) {
        static_cast<SpinnerWidget*>(loadingSpinner_.get())->setSpinning(loading);
    }

    style()->unpolish(this);
    style()->polish(this);
    update();
}

/**
 * @brief       Qt 더블클릭 이벤트를 수신해 중복 방지 후 시그널을 발생시킵니다.
 * @param event  마우스 더블클릭 이벤트
 */
void ClickableVideoWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    emitDoubleClickedOnce();
    event->accept();
}

/**
 * @brief       위젯 크기 변경 시 로딩 오버레이를 영상 영역에 맞춥니다.
 * @param event  Qt resize 이벤트
 */
void ClickableVideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateLoadingOverlayGeometry();
}

#ifdef Q_OS_WIN
/**
 * @brief        Windows 네이티브 더블클릭 메시지를 Qt 시그널로 변환합니다.
 * @param eventType  네이티브 이벤트 종류
 * @param message    Windows MSG 포인터
 * @param result     이벤트 처리 결과 저장 위치
 * @return           처리한 메시지이면 true
 */
bool ClickableVideoWidget::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(eventType)
    Q_UNUSED(result)

    MSG* msg = static_cast<MSG*>(message);

    if (msg && msg->message == WM_LBUTTONDBLCLK) {
        emitDoubleClickedOnce();
        return true;
    }

    return QWidget::nativeEvent(eventType, message, result);
}
#endif

/**
 * @brief   짧은 시간에 중복 발생한 더블클릭을 걸러낸 뒤 시그널을 발생시킵니다.
 */
void ClickableVideoWidget::emitDoubleClickedOnce() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (now - lastDoubleClickMsec_ < doubleClickDebounceMsec) {
        return;
    }

    lastDoubleClickMsec_ = now;

    emit doubleClicked(this);
}

/**
 * @brief   로딩 오버레이를 현재 위젯 영역 전체에 맞추고 최상단으로 올립니다.
 */
void ClickableVideoWidget::updateLoadingOverlayGeometry() {
    if (!loadingOverlay_) {
        return;
    }

    loadingOverlay_->setGeometry(rect());
    loadingOverlay_->raise();
}
