cmake_minimum_required(VERSION 3.14)
project(qtvnc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt5 or Qt6
find_package(Qt6 COMPONENTS Widgets Network QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.12 COMPONENTS Widgets Network REQUIRED)
endif()
message(STATUS "Using Qt ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}.${QT_VERSION_PATCH}")

add_executable(qtvnc
    main.cpp
    connectdialog.cpp
    vncconnection.cpp
    vncviewerwidget.cpp
    rfbconnection.cpp
    rfbcrypto.cpp
)

target_link_libraries(qtvnc PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)
