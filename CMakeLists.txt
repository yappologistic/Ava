cmake_minimum_required(VERSION 3.21)

project(Ava VERSION 0.1 LANGUAGES CXX)

include(CTest)

option(AVA_FORCE_AUTOMATION_MODE "Expose Ava as a focusable window for local UI automation" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Concurrent Core Gui Network Qml Quick QuickControls2 QuickDialogs2 QuickLayouts Test)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(Ava
    src/main.cpp
    src/applauncher.cpp
    src/applauncher.h
    src/codexbridge.cpp
    src/codexbridge.h
    src/islandcontroller.cpp
    src/islandcontroller.h
    src/windowtilingmanager.cpp
    src/windowtilingmanager.h
)

qt_add_library(AvaCodexCore STATIC
    src/codexappserverclient.cpp
    src/codexappserverclient.h
    src/codexchatcontroller.cpp
    src/codexchatcontroller.h
    src/codexgitmanager.cpp
    src/codexgitmanager.h
    src/codexmodels.cpp
    src/codexmodels.h
    src/codexthreadsnapshotstore.cpp
    src/codexthreadsnapshotstore.h
)

target_include_directories(AvaCodexCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(AvaCodexCore
    PUBLIC
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
)

qt_add_executable(AvaChat
    src/chatmain.cpp
    src/chatipcserver.cpp
    src/chatipcserver.h
    src/chattextstyler.cpp
    src/chattextstyler.h
    src/thinkingorbitem.cpp
    src/thinkingorbitem.h
)

qt_add_qml_module(AvaChat
    URI Ava.Chat
    VERSION 1.0
    QML_FILES
        qml/chat/ChatIconButton.qml
        qml/chat/ChatTextButton.qml
        qml/chat/ChatWindowResizeArea.qml
        qml/chat/CodexAttachmentDropLayer.qml
        qml/chat/CodexAttachmentGallery.qml
        qml/chat/CodexAttachmentStrip.qml
        qml/chat/CodexChatWindow.qml
        qml/chat/CodexCodeBlock.qml
        qml/chat/CodexComposer.qml
        qml/chat/CodexDiffInspector.qml
        qml/chat/CodexImageInspector.qml
        qml/chat/CodexMessageDelegate.qml
        qml/chat/CodexPromptNavigator.qml
        qml/chat/CodexRichMessage.qml
        qml/chat/CodexWorkDisclosure.qml
    RESOURCES
        assets/icons/codex-terminal-light.svg
        assets/icons/fluent-chat/LICENSE
        assets/icons/fluent-chat/add.svg
        assets/icons/fluent-chat/attach.svg
        assets/icons/fluent-chat/audio.svg
        assets/icons/fluent-chat/changes.svg
        assets/icons/fluent-chat/chevron-down.svg
        assets/icons/fluent-chat/chevron-right.svg
        assets/icons/fluent-chat/code.svg
        assets/icons/fluent-chat/copy.svg
        assets/icons/fluent-chat/dismiss.svg
        assets/icons/fluent-chat/document.svg
        assets/icons/fluent-chat/download.svg
        assets/icons/fluent-chat/error.svg
        assets/icons/fluent-chat/flash-filled.svg
        assets/icons/fluent-chat/flash.svg
        assets/icons/fluent-chat/image.svg
        assets/icons/fluent-chat/maximize.svg
        assets/icons/fluent-chat/minimize.svg
        assets/icons/fluent-chat/navigation.svg
        assets/icons/fluent-chat/plan.svg
        assets/icons/fluent-chat/reasoning.svg
        assets/icons/fluent-chat/refresh.svg
        assets/icons/fluent-chat/restore.svg
        assets/icons/fluent-chat/search.svg
        assets/icons/fluent-chat/send.svg
        assets/icons/fluent-chat/stop.svg
        assets/fonts/Inter[opsz,wght].ttf
        assets/fonts/OFL-Inter.txt
        assets/fonts/GeistMono[wght].ttf
        assets/fonts/OFL-Geist.txt
)

target_link_libraries(AvaChat
    PRIVATE
        AvaCodexCore
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        Qt6::QuickLayouts
)

qt_add_qml_module(Ava
    URI Ava
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/AppLauncherPanel.qml
        qml/ExpandedPanel.qml
        qml/TimerPanel.qml
        qml/IslandButton.qml
        qml/MotionTokens.qml
        qml/NotchSurface.qml
        qml/RollingDigits.qml
        qml/MorphingLabel.qml
        qml/MorphingIcon.qml
        qml/PlaybackMorphIcon.qml
        qml/DwindleMorphIcon.qml
        qml/CodexPanel.qml
        qml/OpenTuiSpinner.qml
        qml/WallpaperPanel.qml
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
        assets/icons/codex-terminal-blue.svg
        assets/icons/codex-terminal-light.svg
        assets/icons/codex-approval-amber.svg
        assets/icons/codex-complete-green.svg
        assets/icons/wallpaper-light.svg
        assets/icons/wallpaper-dark.svg
        assets/icons/shell-notch-light.svg
        assets/icons/shell-notch-dark.svg
        assets/icons/shell-pill-light.svg
        assets/icons/shell-pill-dark.svg
        assets/wallpapers/runtime/01-alpine-first-light.jpg
        assets/wallpapers/runtime/02-volcanic-coast-after-rain.jpg
        assets/wallpapers/runtime/03-redwood-creek-mist.jpg
        assets/wallpapers/runtime/04-highland-river-clearing.jpg
        assets/wallpapers/runtime/05-glacial-lagoon-blue-hour.jpg
        assets/wallpapers/runtime/06-sandstone-stormlight.jpg
        assets/wallpapers/runtime/07-autumn-larch-valley.jpg
        assets/wallpapers/runtime/08-moonlit-winter-pond.jpg
        assets/wallpapers/runtime/09-volcanic-braided-dusk.jpg
        assets/wallpapers/runtime/10-coastal-dunes-sunrise.jpg
        assets/wallpapers/runtime/11-rainforest-basalt-falls.jpg
        assets/wallpapers/runtime/12-salt-flat-storm-sunset.jpg
        assets/fonts/Inter[opsz,wght].ttf
        assets/fonts/OFL-Inter.txt
        assets/fonts/GeistMono[wght].ttf
        assets/fonts/OFL-Geist.txt
)

target_link_libraries(Ava
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

if (AVA_FORCE_AUTOMATION_MODE)
    target_compile_definitions(Ava PRIVATE AVA_FORCE_AUTOMATION_MODE=1)
endif()

if (WIN32)
    target_link_libraries(Ava PRIVATE user32 shell32 dwmapi ole32 propsys windowsapp)
    target_link_libraries(AvaChat PRIVATE user32 shell32 dwmapi)
    set_target_properties(Ava PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    set_target_properties(AvaChat PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

install(TARGETS Ava AvaChat
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

qt_generate_deploy_qml_app_script(
    TARGET Ava
    OUTPUT_SCRIPT ava_deploy_script
    NO_TRANSLATIONS
)
install(SCRIPT ${ava_deploy_script})

qt_generate_deploy_qml_app_script(
    TARGET AvaChat
    OUTPUT_SCRIPT ava_chat_deploy_script
    NO_TRANSLATIONS
)
install(SCRIPT ${ava_chat_deploy_script})

if (BUILD_TESTING)
    qt_add_executable(tst_codexmodels tests/tst_codexmodels.cpp)
    target_link_libraries(tst_codexmodels PRIVATE AvaCodexCore Qt6::Test)
    add_test(NAME codex_models COMMAND tst_codexmodels)

    qt_add_executable(tst_chattextstyler
        tests/tst_chattextstyler.cpp
        src/chattextstyler.cpp
        src/chattextstyler.h)
    target_include_directories(tst_chattextstyler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(tst_chattextstyler PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME chat_text_styler COMMAND tst_chattextstyler)

    qt_add_executable(tst_codexgitmanager tests/tst_codexgitmanager.cpp)
    target_link_libraries(tst_codexgitmanager PRIVATE AvaCodexCore Qt6::Test)
    add_test(NAME codex_git_manager COMMAND tst_codexgitmanager)

    qt_add_executable(tst_attachmentui tests/tst_attachmentui.cpp)
    target_compile_definitions(tst_attachmentui PRIVATE
        AVA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tst_attachmentui PRIVATE
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    add_test(NAME attachment_ui COMMAND tst_attachmentui)
    set_property(TEST attachment_ui PROPERTY ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen"
        "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>"
        "QT_QUICK_BACKEND=software")

    qt_add_executable(tst_codexlive tests/tst_codexlive.cpp)
    target_link_libraries(tst_codexlive PRIVATE AvaCodexCore Qt6::Test)
    add_test(NAME codex_live_e2e COMMAND tst_codexlive)
    set_tests_properties(codex_live_e2e PROPERTIES
        LABELS live
        TIMEOUT 360)
endif()
