cmake_minimum_required(VERSION 3.16)

project(Qtcctvclient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ProcessorCount)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(GSTREAMER_ROOT "C:/Program Files/gstreamer/1.0/mingw_x86_64")

option(QTCCTV_ENABLE_PCH "Enable precompiled headers for faster Qt rebuilds" ON)
option(QTCCTV_ENABLE_UNITY_BUILD "Enable unity build for faster full rebuilds" OFF)

qt_standard_project_setup()

qt_add_executable(Qtcctvclient
    .clang-format
    .clang-tidy
    C_CppCodingConvention.md

    src/main.cpp

    include/ui/mainwindow.h
    src/ui/mainwindow.cpp
    src/ui/mainwindow.ui
    include/ui/ClickableVideoWidget.h
    src/ui/ClickableVideoWidget.cpp
    include/ui/DashboardLayout.h
    include/ui/DigitalTwinMapWidget.h
    src/ui/DigitalTwinMapWidget.cpp
    include/ui/DigitalTwinMapSceneBuilder.h
    src/ui/DigitalTwinMapSceneBuilder.cpp
    include/ui/DigitalTwinObjectStyleProvider.h
    src/ui/DigitalTwinObjectStyleProvider.cpp

    include/overlays/RadarPulseItem.h
    src/overlays/RadarPulseItem.cpp
    include/overlays/OverlayManager.h
    src/overlays/OverlayManager.cpp

    include/video/StreamReceiver.h
    include/video/StreamReceiverFactory.h
    src/video/GstStreamReceiverFactory.cpp
    include/video/GstRtspReceiver.h
    src/video/GstRtspReceiver.cpp

    include/model/DigitalTwinTypes.h
    include/model/DigitalTwinObjectSpawner.h
    src/model/RandomEdgeObjectSpawner.cpp
    include/model/DigitalTwinRiskPolicy.h
    include/model/DigitalTwinSimulationWorker.h
    src/model/DigitalTwinSimulationWorker.cpp
    src/model/DistanceRiskPolicy.cpp
    include/model/StreamConfig.h

    include/network/rtsp.h
)

qt_add_resources(Qtcctvclient "style_resources"
    PREFIX "/styles"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/styles"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/styles/app.qss"
)

qt_add_resources(Qtcctvclient "icon_resources"
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons"
    BIG_RESOURCES
    OPTIONS -no-compress
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/human.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/human_danger.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/human_warning.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/main.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/motor.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/motor_danger.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/motor_warning.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vehicle.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vehicle_danger.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vehicle_warning.png"
)

target_include_directories(Qtcctvclient PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"

    "${GSTREAMER_ROOT}/include/gstreamer-1.0"
    "${GSTREAMER_ROOT}/include/glib-2.0"
    "${GSTREAMER_ROOT}/lib/glib-2.0/include"
)

ProcessorCount(QTCCTV_CPU_COUNT)

if(QTCCTV_CPU_COUNT)
    set_property(TARGET Qtcctvclient PROPERTY AUTOGEN_PARALLEL "${QTCCTV_CPU_COUNT}")
endif()

if(QTCCTV_ENABLE_PCH)
    target_precompile_headers(Qtcctvclient PRIVATE
        <algorithm>
        <memory>

        <QApplication>
        <QBrush>
        <QColor>
        <QDebug>
        <QFrame>
        <QGraphicsItem>
        <QGraphicsPathItem>
        <QGraphicsScene>
        <QGraphicsSimpleTextItem>
        <QGraphicsView>
        <QLabel>
        <QMainWindow>
        <QObject>
        <QPainter>
        <QPainterPath>
        <QPen>
        <QPointF>
        <QRectF>
        <QShowEvent>
        <QSizePolicy>
        <QString>
        <QThread>
        <QTimer>
        <QVector>
        <QWidget>
    )
endif()

if(QTCCTV_ENABLE_UNITY_BUILD)
    set_property(TARGET Qtcctvclient PROPERTY UNITY_BUILD ON)
    set_property(TARGET Qtcctvclient PROPERTY UNITY_BUILD_BATCH_SIZE 8)
endif()

find_program(SCCACHE_PROGRAM sccache)
find_program(CCACHE_PROGRAM ccache)

if(SCCACHE_PROGRAM)
    set_property(TARGET Qtcctvclient PROPERTY CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
elseif(CCACHE_PROGRAM)
    set_property(TARGET Qtcctvclient PROPERTY CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

target_link_directories(Qtcctvclient PRIVATE
    "${GSTREAMER_ROOT}/lib"
)

target_link_libraries(Qtcctvclient PRIVATE
    Qt6::Widgets

    gstreamer-1.0
    gstrtsp-1.0
    gstvideo-1.0
    gobject-2.0
    glib-2.0
    intl
)
