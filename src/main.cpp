#include <gst/gst.h>

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QStringList>
#include <memory>
#include <utility>

#include "network/MqttDeviceStatusGatewayFactory.h"
#include "ui/mainwindow.h"
#include "ui/panels/DefaultDashboardPanelFactory.h"
#include "video/GstStreamReceiverFactory.h"

namespace {
/**
 * @brief GStreamer MinGW 런타임에서 선택형 GIO 프록시 모듈을 분리합니다.
 *
 * Qt MinGW와 GStreamer 번들의 서로 다른 C++ 런타임이 libgiolibproxy.dll에서
 * 충돌할 수 있으므로, RTSP 수신에 필요하지 않은 GIO 확장 모듈 탐색을 비활성화합니다.
 */
void configureGstreamerMinGwRuntime() {
#ifdef Q_OS_WIN
    const QString moduleDirectory = QDir(QDir::tempPath()).filePath(QStringLiteral("QtDemo/gio-modules"));
    if (!QDir().mkpath(moduleDirectory)) {
        qWarning().noquote() << QStringLiteral("[GStreamer] Failed to create isolated GIO module directory: %1")
                                    .arg(moduleDirectory);
        return;
    }

    const QByteArray nativeDirectory = QFile::encodeName(moduleDirectory);
    if (!g_setenv("GIO_MODULE_DIR", nativeDirectory.constData(), true)) {
        qWarning().noquote() << QStringLiteral("[GStreamer] Failed to configure isolated GIO module directory");
        return;
    }

    g_unsetenv("GIO_EXTRA_MODULES");
    qInfo().noquote() << QStringLiteral("[GStreamer] Using MinGW runtime with isolated GIO modules: %1")
                             .arg(moduleDirectory);
#endif
}

/**
 * @brief      애플리케이션 QSS를 로드합니다.
 * @param app  스타일을 적용할 QApplication
 */
void loadApplicationStyle(QApplication& app) {
    const QStringList stylePaths = {":/styles/app.qss", "C:/Qtprojects/Qtcctvclient/styles/app.qss"};

    for (const QString& path : stylePaths) {
        QFile styleFile(path);

        if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
            app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
            qDebug().noquote() << "[Style] Loaded" << path;
            return;
        }
    }

    qWarning() << "[Style] Failed to load app.qss";
}
}  // namespace

/**
 * @brief       애플리케이션 진입점입니다.
 * @param argc  명령행 인자 개수
 * @param argv  명령행 인자 목록
 * @return      Qt 이벤트 루프 종료 코드
 */
int main(int argc, char* argv[]) {
    configureGstreamerMinGwRuntime();
    gst_init(&argc, &argv);

    int ret = 0;

    {
        QApplication app(argc, argv);
        app.setWindowIcon(QIcon(QStringLiteral(":/icons/main.png")));
        loadApplicationStyle(app);

        {
            auto streamReceiverFactory = std::make_shared<GstStreamReceiverFactory>();
            auto deviceStatusGatewayFactory = std::make_shared<MqttDeviceStatusGatewayFactory>();
            auto dashboardPanelFactory = std::make_shared<DefaultDashboardPanelFactory>();
            MainWindow window(std::move(streamReceiverFactory), std::move(deviceStatusGatewayFactory),
                              std::move(dashboardPanelFactory));
            window.setWindowIcon(app.windowIcon());
            window.resize(1680, 945);
            window.show();

            ret = app.exec();
        }
    }

    gst_deinit();

    return ret;
}
