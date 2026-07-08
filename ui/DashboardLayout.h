#pragma once

#include <QMainWindow>
#include <QSizePolicy>
#include <QWidget>

namespace DashboardLayout {
/** 대시보드의 크기, 여백, stretch 비율, 고정 컨트롤 치수를 적용합니다. */
template <typename UiMainWindow>
inline void apply(QMainWindow* window, UiMainWindow* ui) {
    if (!window || !ui) {
        return;
    }

    window->setMinimumSize(1280, 720);

    ui->mainContentLayout->setContentsMargins(18, 0, 26, 14);
    ui->topSectionLayout->setContentsMargins(0, 0, 0, 0);
    ui->bottomSectionLayout->setContentsMargins(0, 0, 0, 0);

    ui->topBarFrame->setFixedHeight(62);
    ui->legendFrame->setFixedHeight(46);

    ui->cameraSelectLabel->setFixedWidth(132);
    ui->cameraSelectLabel->setAlignment(Qt::AlignCenter);

    ui->topSectionFrame->setMinimumHeight(0);
    ui->topSectionFrame->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->topSectionFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    ui->bottomSectionFrame->setFixedHeight(300);
    ui->bottomSectionFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    ui->cctvCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->mapCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    ui->eventLogCard->setMinimumWidth(0);
    ui->deviceStatusCard->setMinimumWidth(0);
    ui->riskSummaryCard->setMinimumWidth(0);
    ui->objectListCard->setMinimumWidth(0);
    ui->eventLogBodyFrame->setMinimumWidth(0);
    ui->deviceStatusBodyFrame->setMinimumWidth(0);
    ui->riskSummaryBodyFrame->setMinimumWidth(0);
    ui->objectListBodyFrame->setMinimumWidth(0);

    ui->eventLogCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->deviceStatusCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->riskSummaryCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->objectListCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    ui->mainContentLayout->setStretch(0, 0);
    ui->mainContentLayout->setStretch(1, 1);
    ui->mainContentLayout->setStretch(2, 0);
    ui->mainContentLayout->setStretch(3, 0);

    ui->topSectionLayout->setStretch(0, 9);
    ui->topSectionLayout->setStretch(1, 10);

    ui->bottomSectionLayout->setStretch(0, 8);
    ui->bottomSectionLayout->setStretch(1, 5);
    ui->bottomSectionLayout->setStretch(2, 5);
    ui->bottomSectionLayout->setStretch(3, 7);

    ui->cctvCardLayout->setStretch(0, 0);
    ui->cctvCardLayout->setStretch(1, 1);
    ui->mapCardLayout->setStretch(0, 0);
    ui->mapCardLayout->setStretch(1, 1);

    ui->eventLogLayout->setStretch(0, 0);
    ui->eventLogLayout->setStretch(1, 1);
    ui->deviceStatusLayout->setStretch(0, 0);
    ui->deviceStatusLayout->setStretch(1, 1);
    ui->riskSummaryLayout->setStretch(0, 0);
    ui->riskSummaryLayout->setStretch(1, 1);
    ui->objectListLayout->setStretch(0, 0);
    ui->objectListLayout->setStretch(1, 1);

    ui->videoGridLayout->setRowStretch(0, 1);
    ui->videoGridLayout->setRowStretch(1, 1);
    ui->videoGridLayout->setColumnStretch(0, 1);
    ui->videoGridLayout->setColumnStretch(1, 1);
}
}  // namespace DashboardLayout
