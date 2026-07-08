#include <gst/gst.h>

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QStringList>

#include "ui/mainwindow.h"

namespace {
/** 리소스에 포함된 QSS를 먼저 찾고, 개발 중에는 파일 시스템 경로의 QSS를 대체 경로로 사용합니다. */
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

int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);

    int ret = 0;

    {
        QApplication app(argc, argv);
        loadApplicationStyle(app);

        {
            MainWindow window;
            window.resize(1680, 945);
            window.show();

            ret = app.exec();
        }
    }

    gst_deinit();

    return ret;
}
