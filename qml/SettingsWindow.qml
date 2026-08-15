import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5
import QtQuick.Window 6.5

ApplicationWindow {
    id: root
    objectName: "settingsWindow"

    required property var controller
    required property var launcher
    required property var tiling
    required property var enhancedTabs
    property string uiFont: "Inter"
    property string iconFont: "Segoe Fluent Icons"
    property string selectedSection: "island"
    property string highlightedSetting: ""
    property bool positionedOnce: false

    readonly property color accentColor: controller.mediaArtworkAccentEnabled
                                                && controller.mediaPlaying
                                                && controller.mediaArtworkAccent.length > 0
                                            ? controller.mediaArtworkAccent
                                            : "#9ad9cc"
    readonly property int sectionIndex: selectedSection === "island" ? 0
                                                : (selectedSection === "media" ? 1
                                                   : (selectedSection === "utilities" ? 2
                                                      : (selectedSection === "launcher" ? 3
                                                         : (selectedSection === "windows" ? 4 : 5))))
    readonly property bool searching: searchField.text.trim().length > 0
    readonly property var searchResults: filterSettings(searchField.text)
    readonly property var searchableSettings: [
        { key: "shape", section: "island", title: "Island shape", description: "Notch or Pill silhouette", keywords: "notch pill shell appearance" },
        { key: "compactWidth", section: "island", title: "Compact width", description: "Collapsed island size", keywords: "size compact width geometry pixels" },
        { key: "monitor", section: "island", title: "System monitor", description: "CPU, GPU, and battery", keywords: "cpu gpu battery performance compact" },
        { key: "artworkAccent", section: "media", title: "Artwork accent", description: "Playing artwork color", keywords: "color music artwork calendar accent mint" },
        { key: "audioPulse", section: "media", title: "Audio pulse", description: "Five-bar output level", keywords: "meter waveform visualization bars sound music" },
        { key: "mediaPeek", section: "media", title: "Automatic media peek", description: "Expand when the track changes", keywords: "track change expand now playing automatic" },
        { key: "timerSatellite", section: "utilities", title: "Timer satellite", description: "Countdown beside the island", keywords: "countdown timer compact satellite" },
        { key: "weekStart", section: "utilities", title: "Week starts on", description: "Calendar week ordering", keywords: "calendar day monday sunday date locale" },
        { key: "recentApps", section: "launcher", title: "Recent app suggestions", description: "History-based result ranking", keywords: "history ranking recent apps suggestions" },
        { key: "directTargets", section: "launcher", title: "Web and file targets", description: "Addresses and local paths", keywords: "url web path folder file direct" },
        { key: "emojiEntry", section: "launcher", title: "Emoji picker entry", description: "Emoji & Symbols result", keywords: "emoji symbols launcher result" },
        { key: "launcherShortcut", section: "launcher", title: "Launcher shortcut", description: "Ctrl+K", keywords: "keyboard shortcut hotkey control k" },
        { key: "dwindle", section: "windows", title: "Dwindle workspace", description: "Window tiling and restoration", keywords: "tiling windows layout restore win alt t" },
        { key: "enhancedTabs", section: "windows", title: "Enhanced Alt-Tab", description: "Native live window switcher", keywords: "alt tab switcher thumbnails windows" },
        { key: "fullscreen", section: "windows", title: "Respect fullscreen apps", description: "Collapse over fullscreen content", keywords: "fullscreen game hide collapse focus" },
        { key: "motionMode", section: "motion", title: "Motion", description: "Windows or reduced motion", keywords: "animation accessibility reduced windows system" },
        { key: "hoverDelay", section: "motion", title: "Open delay", description: "Pointer hover timing", keywords: "hover open expand delay timing" },
        { key: "collapseDelay", section: "motion", title: "Close delay", description: "Pointer leave timing", keywords: "leave close collapse delay timing" }
    ]

    width: 930
    height: 650
    minimumWidth: 820
    minimumHeight: 560
    maximumWidth: 1220
    maximumHeight: 840
    visible: controller.settingsOpen
    color: "transparent"
    title: "Ava Settings"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint

    function sectionTitle(section) {
        if (section === "media") return "Media"
        if (section === "utilities") return "Utilities"
        if (section === "launcher") return "Launcher"
        if (section === "windows") return "Windows"
        if (section === "motion") return "Motion"
        return "Island"
    }

    function sectionDescription(section) {
        if (section === "media") return "Artwork color and playback behavior."
        if (section === "utilities") return "Timer and calendar preferences."
        if (section === "launcher") return "Search scope and launcher suggestions."
        if (section === "windows") return "Dwindle and enhanced window switching."
        if (section === "motion") return "Timing and reduced-motion behavior."
        return "Shape, size, and compact information."
    }

    function filterSettings(query) {
        const normalized = query.trim().toLowerCase()
        if (normalized.length === 0)
            return []
        return searchableSettings.filter(function(entry) {
            return (entry.title + " " + entry.description + " "
                    + entry.section + " " + entry.keywords)
                .toLowerCase().indexOf(normalized) >= 0
        })
    }

    function openSearchResult(entry) {
        selectedSection = entry.section
        highlightedSetting = entry.key
        searchField.clear()
        highlightTimer.restart()
    }

    onVisibleChanged: {
        if (!visible)
            return
        if (!positionedOnce) {
            x = Math.round(Screen.virtualX + (Screen.width - width) / 2)
            y = Math.round(Screen.virtualY + (Screen.height - height) / 2)
            positionedOnce = true
        }
        raise()
        requestActivate()
    }

    onClosing: function(close) {
        controller.closeSettings()
        close.accepted = true
    }

    Shortcut {
        sequence: "Ctrl+F"
        onActivated: searchField.forceActiveFocus()
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (searchField.text.length > 0)
                searchField.clear()
            else
                controller.closeSettings()
        }
    }

    Timer {
        id: highlightTimer
        interval: 1200
        repeat: false
        onTriggered: root.highlightedSetting = ""
    }

    Rectangle {
        id: shell
        anchors.fill: parent
        radius: 14
        color: "#050505"
        border.width: 1
        border.color: "#232326"
        clip: true

        Rectangle {
            id: titleBar
            x: 1
            y: 1
            width: parent.width - 2
            height: 46
            radius: shell.radius - 1
            color: "#080808"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: parent.radius
                color: parent.color
            }

            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 52
                acceptedButtons: Qt.LeftButton
                onPressed: root.startSystemMove()
            }

            Image {
                id: appIcon
                anchors.left: parent.left
                anchors.leftMargin: 15
                anchors.verticalCenter: parent.verticalCenter
                width: 22
                height: 22
                source: Qt.resolvedUrl("../assets/icons/ava-app-icon-light.png")
                sourceSize.width: 44
                sourceSize.height: 44
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }

            Text {
                anchors.left: appIcon.right
                anchors.leftMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                text: "Ava Settings"
                color: "#efeff1"
                font.family: root.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            Button {
                objectName: "settingsClose"
                anchors.right: parent.right
                anchors.rightMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                height: 32
                flat: true
                activeFocusOnTab: true
                Accessible.name: "Close settings"
                onClicked: root.controller.closeSettings()

                contentItem: Image {
                    anchors.centerIn: parent
                    width: 13
                    height: 13
                    source: Qt.resolvedUrl("../assets/icons/dismiss-light.svg")
                    sourceSize.width: 26
                    sourceSize.height: 26
                    fillMode: Image.PreserveAspectFit
                    opacity: parent.hovered ? 0.95 : 0.62
                }
                background: Rectangle {
                    radius: 8
                    color: parent.hovered ? "#1b1b1e" : "transparent"
                }
            }
        }

        Rectangle {
            id: sidebar
            anchors.left: parent.left
            anchors.top: titleBar.bottom
            anchors.bottom: parent.bottom
            width: 220
            radius: shell.radius
            color: "#080808"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: parent.radius
                color: parent.color
            }

            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: parent.radius
                color: parent.color
            }

            TextField {
                id: searchField
                objectName: "settingsSearch"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                height: 36
                leftPadding: 36
                rightPadding: 12
                color: "#ececee"
                placeholderText: "Search settings"
                placeholderTextColor: "#74747c"
                selectByMouse: true
                font.family: root.uiFont
                font.pixelSize: 12
                Accessible.name: "Search settings"
                onAccepted: {
                    if (root.searchResults.length > 0)
                        root.openSearchResult(root.searchResults[0])
                }

                background: Rectangle {
                    radius: 9
                    color: "#111113"
                    border.width: searchField.activeFocus ? 1 : 0
                    border.color: root.accentColor
                }

                Image {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    source: Qt.resolvedUrl("../assets/icons/fluent-chat/search.svg")
                    sourceSize.width: 28
                    sourceSize.height: 28
                    opacity: 0.58
                }
            }

            ListView {
                id: navigation
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: searchField.bottom
                anchors.topMargin: 12
                anchors.bottom: parent.bottom
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 5
                clip: true
                interactive: false
                model: [
                    { key: "island", label: "Island", icon: "../assets/icons/shell-notch-light.svg" },
                    { key: "media", label: "Media", icon: "../assets/icons/speaker-light.svg" },
                    { key: "utilities", label: "Utilities", icon: "../assets/icons/grid-light.svg" },
                    { key: "launcher", label: "Launcher", icon: "../assets/icons/launcher-folder.svg" },
                    { key: "windows", label: "Windows", icon: "../assets/icons/enhanced-tabs-light.svg" },
                    { key: "motion", label: "Motion", icon: "../assets/icons/fluent-chat/refresh.svg" }
                ]

                delegate: Button {
                    id: navigationButton
                    required property var modelData
                    width: navigation.width
                    height: 42
                    flat: true
                    objectName: "settingsNav_" + modelData.key
                    Accessible.name: modelData.label + " settings"
                    onClicked: {
                        root.selectedSection = modelData.key
                        searchField.clear()
                    }

                    background: Rectangle {
                        radius: 9
                        color: root.selectedSection === navigationButton.modelData.key
                               && !root.searching
                               ? "#18181a" : (navigationButton.hovered ? "#101012" : "transparent")
                    }

                    contentItem: Row {
                        leftPadding: 7
                        spacing: 10

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28
                            height: 28
                            radius: 8
                            color: root.selectedSection === navigationButton.modelData.key
                                   && !root.searching
                                   ? Qt.rgba(root.accentColor.r,
                                             root.accentColor.g,
                                             root.accentColor.b, 0.18)
                                   : "#111113"

                            Image {
                                anchors.centerIn: parent
                                width: 15
                                height: 15
                                source: Qt.resolvedUrl(navigationButton.modelData.icon)
                                sourceSize.width: 30
                                sourceSize.height: 30
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                mipmap: true
                                opacity: root.selectedSection
                                         === navigationButton.modelData.key ? 0.96 : 0.58
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: navigationButton.modelData.label
                            color: root.selectedSection === navigationButton.modelData.key
                                   && !root.searching ? "#f0f0f2" : "#aaaab1"
                            font.family: root.uiFont
                            font.pixelSize: 12
                            font.weight: root.selectedSection === navigationButton.modelData.key
                                         && !root.searching ? Font.DemiBold : Font.Normal
                        }
                    }
                }
            }
        }

        Item {
            id: content
            anchors.left: sidebar.right
            anchors.right: parent.right
            anchors.top: titleBar.bottom
            anchors.bottom: parent.bottom

            Column {
                id: contentHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 26
                anchors.rightMargin: 26
                anchors.topMargin: 24
                spacing: 5

                Text {
                    width: parent.width
                    text: root.searching ? "Search" : root.sectionTitle(root.selectedSection)
                    color: "#f4f4f5"
                    font.family: root.uiFont
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }

                Text {
                    width: parent.width
                    text: root.searching
                          ? (root.searchResults.length === 1
                             ? "1 matching setting"
                             : root.searchResults.length + " matching settings")
                          : root.sectionDescription(root.selectedSection)
                    color: "#7e7e87"
                    font.family: root.uiFont
                    font.pixelSize: 12
                }
            }

            ScrollView {
                id: settingsScroll
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: contentHeader.bottom
                anchors.bottom: parent.bottom
                anchors.topMargin: 18
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.bottomMargin: 12
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                Item {
                    width: settingsScroll.availableWidth
                    implicitHeight: root.searching
                                    ? searchColumn.implicitHeight
                                    : sectionStack.implicitHeight

                    Column {
                        id: searchColumn
                        visible: root.searching
                        width: parent.width
                        spacing: 5

                        Repeater {
                            model: root.searchResults

                            Button {
                                id: searchResultButton
                                required property var modelData
                                width: searchColumn.width
                                height: 60
                                flat: true
                                Accessible.name: modelData.title + ", "
                                                 + root.sectionTitle(modelData.section)
                                onClicked: root.openSearchResult(modelData)

                                background: Rectangle {
                                    anchors.fill: parent
                                    radius: 9
                                    color: parent.hovered ? "#101012" : "transparent"
                                }

                                contentItem: Column {
                                    leftPadding: 14
                                    spacing: 4

                                    Text {
                                        width: parent.width - 28
                                        text: searchResultButton.modelData.title
                                        color: "#eeeeef"
                                        font.family: root.uiFont
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        width: parent.width - 28
                                        text: root.sectionTitle(searchResultButton.modelData.section)
                                              + " · " + searchResultButton.modelData.description
                                        color: "#7f7f87"
                                        elide: Text.ElideRight
                                        font.family: root.uiFont
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }

                        Text {
                            visible: root.searchResults.length === 0
                            width: parent.width
                            topPadding: 26
                            text: "No settings match this search."
                            color: "#777780"
                            horizontalAlignment: Text.AlignHCenter
                            font.family: root.uiFont
                            font.pixelSize: 12
                        }
                    }

                    StackLayout {
                        id: sectionStack
                        visible: !root.searching
                        width: parent.width
                        currentIndex: root.sectionIndex

                        ColumnLayout {
                            spacing: 7

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_shape"
                                title: "Island shape"
                                description: "Choose the attached Notch or floating Pill silhouette."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                highlighted: root.highlightedSetting === "shape"

                                SettingsSegmented {
                                    anchors.fill: parent
                                    value: root.controller.pillMode ? "pill" : "notch"
                                    accentColor: root.accentColor
                                    uiFont: root.uiFont
                                    onSelected: function(value) {
                                        root.controller.setPillMode(value === "pill")
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_compactWidth"
                                title: "Compact width"
                                description: "Keep the collapsed island concise or give content more room."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                highlighted: root.highlightedSetting === "compactWidth"

                                SettingsSlider {
                                    anchors.fill: parent
                                    from: 150
                                    to: 210
                                    stepSize: 2
                                    value: root.controller.compactWidth
                                    unit: " px"
                                    accentColor: root.accentColor
                                    uiFont: root.uiFont
                                    reducedMotion: root.controller.reducedMotion
                                    accessibleName: "Compact width"
                                    onMoved: function(value) {
                                        root.controller.setCompactWidth(Math.round(value))
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_monitor"
                                title: "System monitor"
                                description: "Show CPU, GPU, and battery in the compact island."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "monitor"

                                SettingsSwitch {
                                    objectName: "setting_monitor"
                                    anchors.fill: parent
                                    checked: root.controller.monitorEnabled
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "System monitor"
                                    onToggled: root.controller.setMonitorEnabled(checked)
                                }
                            }
                        }

                        ColumnLayout {
                            spacing: 7

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_artworkAccent"
                                title: "Artwork accent"
                                description: "Use the playing artwork color; otherwise use Ava’s calendar mint."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "artworkAccent"

                                SettingsSwitch {
                                    objectName: "setting_artworkAccent"
                                    anchors.fill: parent
                                    checked: root.controller.mediaArtworkAccentEnabled
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "Artwork accent"
                                    onToggled: root.controller.setMediaArtworkAccentEnabled(checked)
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_audioPulse"
                                title: "Audio pulse"
                                description: "Show the measured five-bar output level in compact and expanded media."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "audioPulse"

                                SettingsSwitch {
                                    objectName: "setting_audioPulse"
                                    anchors.fill: parent
                                    checked: root.controller.audioPulseEnabled
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "Audio pulse"
                                    onToggled: root.controller.setAudioPulseEnabled(checked)
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_mediaPeek"
                                title: "Automatic media peek"
                                description: "Briefly expand when the active track changes."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "mediaPeek"

                                SettingsSwitch {
                                    objectName: "setting_mediaPeek"
                                    anchors.fill: parent
                                    checked: root.controller.mediaPeekEnabled
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "Automatic media peek"
                                    onToggled: root.controller.setMediaPeekEnabled(checked)
                                }
                            }
                        }

                        ColumnLayout {
                            spacing: 7

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_timerSatellite"
                                title: "Timer satellite"
                                description: "Keep the active countdown visible beside the compact island."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "timerSatellite"

                                SettingsSwitch {
                                    objectName: "setting_timerSatellite"
                                    anchors.fill: parent
                                    checked: root.controller.timerSatelliteEnabled
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "Timer satellite"
                                    onToggled: root.controller.setTimerSatelliteEnabled(checked)
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_weekStart"
                                title: "Week starts on"
                                description: "Choose how the island calendar orders the week."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 190
                                highlighted: root.highlightedSetting === "weekStart"

                                SettingsComboBox {
                                    objectName: "setting_weekStart"
                                    anchors.fill: parent
                                    model: ["System", "Monday", "Sunday"]
                                    currentIndex: root.controller.weekStartMode === "monday" ? 1
                                                  : (root.controller.weekStartMode === "sunday" ? 2 : 0)
                                    accentColor: root.accentColor
                                    uiFont: root.uiFont
                                    Accessible.name: "Week starts on"
                                    onActivated: function(index) {
                                        root.controller.setWeekStartMode(index === 1
                                            ? "monday" : (index === 2 ? "sunday" : "system"))
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            spacing: 7

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_recentApps"
                                title: "Recent app suggestions"
                                description: "Rank frequently launched apps before alphabetical matches."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "recentApps"

                                SettingsSwitch {
                                    objectName: "setting_recentApps"
                                    anchors.fill: parent
                                    checked: root.launcher.recentSuggestionsEnabled
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "Recent app suggestions"
                                    onToggled: root.launcher.setRecentSuggestionsEnabled(checked)
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_directTargets"
                                title: "Web and file targets"
                                description: "Open valid addresses and existing local paths from search."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "directTargets"

                                SettingsSwitch {
                                    objectName: "setting_directTargets"
                                    anchors.fill: parent
                                    checked: root.launcher.directTargetsEnabled
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "Web and file targets"
                                    onToggled: root.launcher.setDirectTargetsEnabled(checked)
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_emojiEntry"
                                title: "Emoji picker entry"
                                description: "Keep Emoji & Symbols available in launcher results."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "emojiEntry"

                                SettingsSwitch {
                                    objectName: "setting_emojiEntry"
                                    anchors.fill: parent
                                    checked: root.launcher.emojiEntryEnabled
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "Emoji picker entry"
                                    onToggled: root.launcher.setEmojiEntryEnabled(checked)
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_launcherShortcut"
                                title: "Launcher shortcut"
                                description: "The current global shortcut for opening Ava search."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 90
                                highlighted: root.highlightedSetting === "launcherShortcut"

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 7
                                    color: "#151517"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "Ctrl+K"
                                        color: "#b9b9c0"
                                        font.family: root.uiFont
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            spacing: 7

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_dwindle"
                                title: "Dwindle workspace"
                                description: "Tile eligible windows and restore their original placement when disabled."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "dwindle"

                                SettingsSwitch {
                                    objectName: "setting_dwindle"
                                    anchors.fill: parent
                                    checked: root.tiling.enabled
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "Dwindle workspace"
                                    onToggled: root.tiling.setEnabled(checked)
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_enhancedTabs"
                                title: "Enhanced Alt-Tab"
                                description: root.enhancedTabs.available
                                             ? "Use Ava’s native window switcher with live thumbnails."
                                             : "Enhanced Alt-Tab is unavailable on this system."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "enhancedTabs"

                                SettingsSwitch {
                                    objectName: "setting_enhancedTabs"
                                    anchors.fill: parent
                                    enabled: root.enhancedTabs.available
                                    opacity: enabled ? 1 : 0.44
                                    checked: root.enhancedTabs.enabled
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "Enhanced Alt-Tab"
                                    onToggled: root.enhancedTabs.setEnabled(checked)
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_fullscreen"
                                title: "Respect fullscreen apps"
                                description: "Collapse Ava over fullscreen content unless attention is required."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 46
                                highlighted: root.highlightedSetting === "fullscreen"

                                SettingsSwitch {
                                    objectName: "setting_fullscreen"
                                    anchors.fill: parent
                                    checked: root.controller.respectFullscreenApps
                                    accentColor: root.accentColor
                                    reducedMotion: root.controller.reducedMotion
                                    Accessible.name: "Respect fullscreen apps"
                                    onToggled: root.controller.setRespectFullscreenApps(checked)
                                }
                            }
                        }

                        ColumnLayout {
                            spacing: 7

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_motionMode"
                                title: "Motion"
                                description: "Follow Windows by default, with explicit reduced or full motion."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                controlWidth: 190
                                highlighted: root.highlightedSetting === "motionMode"

                                SettingsComboBox {
                                    objectName: "setting_motionMode"
                                    anchors.fill: parent
                                    model: ["Follow Windows", "Reduced", "Full"]
                                    currentIndex: root.controller.motionMode === "reduced" ? 1
                                                  : (root.controller.motionMode === "full" ? 2 : 0)
                                    accentColor: root.accentColor
                                    uiFont: root.uiFont
                                    Accessible.name: "Motion"
                                    onActivated: function(index) {
                                        root.controller.setMotionMode(index === 1
                                            ? "reduced" : (index === 2 ? "full" : "system"))
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_hoverDelay"
                                title: "Open delay"
                                description: "How long the pointer rests before the island expands."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                highlighted: root.highlightedSetting === "hoverDelay"

                                SettingsSlider {
                                    objectName: "setting_hoverDelay"
                                    anchors.fill: parent
                                    from: 120
                                    to: 480
                                    stepSize: 20
                                    value: root.controller.hoverOpenDelay
                                    unit: " ms"
                                    accentColor: root.accentColor
                                    uiFont: root.uiFont
                                    reducedMotion: root.controller.reducedMotion
                                    accessibleName: "Open delay"
                                    onMoved: function(value) {
                                        root.controller.setHoverOpenDelay(Math.round(value))
                                    }
                                }
                            }

                            SettingsRow {
                                Layout.fillWidth: true
                                objectName: "settingRow_collapseDelay"
                                title: "Close delay"
                                description: "How long Ava waits before collapsing after the pointer leaves."
                                uiFont: root.uiFont
                                accentColor: root.accentColor
                                highlighted: root.highlightedSetting === "collapseDelay"

                                SettingsSlider {
                                    objectName: "setting_collapseDelay"
                                    anchors.fill: parent
                                    from: 240
                                    to: 900
                                    stepSize: 20
                                    value: root.controller.leaveCloseDelay
                                    unit: " ms"
                                    accentColor: root.accentColor
                                    uiFont: root.uiFont
                                    reducedMotion: root.controller.reducedMotion
                                    accessibleName: "Close delay"
                                    onMoved: function(value) {
                                        root.controller.setLeaveCloseDelay(Math.round(value))
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        WindowResizeArea {
            targetWindow: root
        }
    }
}
