#include "ClickableVideoWidget.h"

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

class SpinnerWidget : public QWidget {
public:
    explicit SpinnerWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(44, 44);

        timer_.setInterval(80);

        connect(&timer_, &QTimer::timeout, this, [this]() {
            angle_ = (angle_ + 30) % 360;
            update();
        });
    }

    void setSpinning(bool spinning) {
        if (spinning) {
            timer_.start();
        } else {
            timer_.stop();
        }

        update();
    }

protected:
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

void ClickableVideoWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    emitDoubleClickedOnce();
    event->accept();
}

void ClickableVideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateLoadingOverlayGeometry();
}

#ifdef Q_OS_WIN
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

void ClickableVideoWidget::emitDoubleClickedOnce() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (now - lastDoubleClickMsec_ < doubleClickDebounceMsec) {
        return;
    }

    lastDoubleClickMsec_ = now;

    emit doubleClicked(this);
}

void ClickableVideoWidget::updateLoadingOverlayGeometry() {
    if (!loadingOverlay_) {
        return;
    }

    loadingOverlay_->setGeometry(rect());
    loadingOverlay_->raise();
}
