cmake_minimum_required(VERSION 3.16)

project(Qtcctvclient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(GSTREAMER_ROOT "C:/Program Files/gstreamer/1.0/mingw_x86_64")

qt_standard_project_setup()

qt_add_executable(Qtcctvclient
    .clang-format
    .clang-tidy
    C_CppCodingConvention.md

    main.cpp

    ui/mainwindow.h
    ui/mainwindow.cpp
    ui/mainwindow.ui
    ui/ClickableVideoWidget.h
    ui/ClickableVideoWidget.cpp
    ui/DashboardLayout.h

    styles/app.qrc

    video/GstRtspReceiver.h
    video/GstRtspReceiver.cpp

    model/StreamConfig.h

    network/rtsp.h
)

target_include_directories(Qtcctvclient PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui"

    "${GSTREAMER_ROOT}/include/gstreamer-1.0"
    "${GSTREAMER_ROOT}/include/glib-2.0"
    "${GSTREAMER_ROOT}/lib/glib-2.0/include"
)

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
