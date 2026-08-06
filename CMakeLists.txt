cmake_minimum_required(VERSION 3.21)

project(DynamicIsland VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(DynamicIsland
    src/main.cpp
    src/islandcontroller.cpp
    src/islandcontroller.h
    src/windowtilingmanager.cpp
    src/windowtilingmanager.h
)

qt_add_qml_module(DynamicIsland
    URI DynamicIsland
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/ExpandedPanel.qml
        qml/TimerPanel.qml
        qml/IslandButton.qml
        qml/NotchSurface.qml
    RESOURCES
        assets/icons/play-dark.svg
        assets/icons/pause-dark.svg
        assets/icons/play-light.svg
        assets/icons/pause-light.svg
        assets/icons/previous-light.svg
        assets/icons/next-light.svg
        assets/icons/speaker-light.svg
        assets/icons/speaker-muted-dark.svg
        assets/icons/pin-light.svg
        assets/icons/pin-off-dark.svg
        assets/icons/dismiss-light.svg
        assets/icons/grid-dark.svg
        assets/icons/grid-light.svg
        assets/icons/timer-light.svg
        assets/icons/timer-dark.svg
        assets/icons/timer-orange.svg
        assets/fonts/Geist[wght].ttf
        assets/fonts/OFL-Geist.txt
)

target_link_libraries(DynamicIsland
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

if (WIN32)
    target_link_libraries(DynamicIsland PRIVATE user32 dwmapi ole32 windowsapp)
    set_target_properties(DynamicIsland PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

install(TARGETS DynamicIsland
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
