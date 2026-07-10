#include <gst/gst.h>

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QIcon>
#include <QStringList>

#include "ui/mainwindow.h"

namespace {
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
    gst_init(&argc, &argv);

    int ret = 0;

    {
        QApplication app(argc, argv);
        app.setWindowIcon(QIcon(QStringLiteral(":/icons/main.png")));
        loadApplicationStyle(app);

        {
            MainWindow window;
            window.setWindowIcon(app.windowIcon());
            window.resize(1680, 945);
            window.show();

            ret = app.exec();
        }
    }

    gst_deinit();

    return ret;
}
