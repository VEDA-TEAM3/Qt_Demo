#pragma once

#include <QWidget>
#include <memory>

class QFrame;
class QLabel;
class QMouseEvent;
class QResizeEvent;
class QVBoxLayout;

class ClickableVideoWidget : public QWidget {
    Q_OBJECT

public:
    explicit ClickableVideoWidget(QWidget* parent = nullptr);

public slots:
    void setLoading(bool loading);

signals:
    void doubleClicked(ClickableVideoWidget* widget);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    void emitDoubleClickedOnce();
    void updateLoadingOverlayGeometry();

private:
    std::shared_ptr<QFrame> loadingOverlay_;
    std::shared_ptr<QWidget> loadingSpinner_;
    std::shared_ptr<QLabel> loadingLabel_;
    std::shared_ptr<QVBoxLayout> loadingLayout_;

    qint64 lastDoubleClickMsec_ = 0;
};
