pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property var colors
    property string uiFont: "Segoe UI Variable"
    property string iconFont: "Segoe Fluent Icons"
    property bool expanded: false
    property bool dragActive: false
    property bool reducedMotion: false
    property bool tilingFeedbackActive: false
    readonly property date currentDate: {
        const clockToken = controller.dateText
        return new Date()
    }
    readonly property int currentDayIndex: currentDate.getDay()

    function weekDate(index) {
        return new Date(currentDate.getFullYear(), currentDate.getMonth(),
                        currentDate.getDate() - currentDayIndex + index)
    }

    enabled: expanded && !dragActive
    opacity: enabled ? 1 : 0
    scale: enabled ? 1 : 0.975
    transformOrigin: Item.Top

    Behavior on opacity {
        SequentialAnimation {
            PauseAnimation { duration: root.enabled && !root.reducedMotion ? 45 : 0 }
            NumberAnimation { duration: root.reducedMotion ? 0 : 135; easing.type: Easing.OutCubic }
        }
    }

    Behavior on scale {
        NumberAnimation { duration: root.reducedMotion ? 0 : 180; easing.type: Easing.OutCubic }
    }

    // The reference is deliberately sparse: artwork and transport controls occupy
    // the left third, while the middle stays visually quiet.
    Item {
        id: mediaPane
        x: 18
        y: 22
        width: 292
        height: 110

        Item {
            anchors.fill: parent
            visible: controller.mediaAvailable

            Rectangle {
                id: artworkFrame
                width: 88
                height: 88
                radius: 14
                color: root.colors.raised
                clip: true

                Image {
                    anchors.fill: parent
                    source: controller.mediaArtworkUrl
                    visible: controller.mediaArtworkUrl.length > 0
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    smooth: true
                    mipmap: true
                }

                Text {
                    anchors.centerIn: parent
                    visible: controller.mediaArtworkUrl.length === 0
                    text: "\uE8D6"
                    color: root.colors.secondary
                    font.family: root.iconFont
                    font.pixelSize: 28
                }
            }

            Column {
                x: 104
                y: 4
                width: 174
                spacing: 2

                Text {
                    width: parent.width
                    text: controller.mediaTitle
                    color: root.colors.text
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: root.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: controller.mediaArtist.length > 0
                          ? controller.mediaArtist : controller.mediaSource
                    color: root.colors.secondary
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: root.uiFont
                    font.pixelSize: 8
                }
                Text {
                    width: parent.width
                    text: controller.mediaSource
                    visible: controller.mediaArtist.length > 0 && controller.mediaSource.length > 0
                    color: root.colors.tertiary
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: root.uiFont
                    font.pixelSize: 8
                }
            }

            Row {
                x: 99
                y: 60
                spacing: 8

                IslandButton {
                    width: 23
                    height: 23
                    iconOnly: true
                    bare: true
                    iconSource: Qt.resolvedUrl("../assets/icons/previous-light.svg")
                    iconSize: 12
                    enabled: controller.mediaCanPrevious
                    accessibleName: "Previous track"
                    onClicked: controller.previousTrack()
                }
                IslandButton {
                    width: 24
                    height: 24
                    iconOnly: true
                    bare: true
                    iconSource: controller.mediaPlaying
                                ? Qt.resolvedUrl("../assets/icons/pause-light.svg")
                                : Qt.resolvedUrl("../assets/icons/play-light.svg")
                    iconSize: 13
                    accessibleName: controller.mediaPlaying ? "Pause" : "Play"
                    onClicked: controller.togglePlayback()
                }
                IslandButton {
                    width: 23
                    height: 23
                    iconOnly: true
                    bare: true
                    iconSource: Qt.resolvedUrl("../assets/icons/next-light.svg")
                    iconSize: 12
                    enabled: controller.mediaCanNext
                    accessibleName: "Next track"
                    onClicked: controller.nextTrack()
                }
            }
        }

        Item {
            anchors.fill: parent
            visible: !controller.mediaAvailable

            Rectangle {
                width: 88
                height: 88
                radius: 14
                color: root.colors.raised

                Text {
                    anchors.centerIn: parent
                    text: "\uE8D6"
                    color: root.colors.secondary
                    font.family: root.iconFont
                    font.pixelSize: 28
                }
            }
            Text {
                x: 104
                y: 20
                text: "Nothing playing"
                color: root.colors.text
                font.family: root.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Text {
                x: 104
                y: 42
                width: 172
                wrapMode: Text.WordWrap
                text: "Start media in a Windows app"
                color: root.colors.tertiary
                font.family: root.uiFont
                font.pixelSize: 9
            }
        }
    }

    // Clock and a real current-week calendar reproduce the small right cluster in
    // the source. Hovering this cluster crossfades to utility actions.
    Item {
        id: clockPane
        x: 390
        y: 17
        width: 175
        height: 106
        Accessible.name: controller.timeText + " " + controller.meridiemText
                         + ". " + (controller.networkName.length > 0
                                   ? controller.networkName + ". " : "")
                         + controller.networkStatus
                         + (controller.batteryAvailable ? ". " + controller.powerText : "")
                         + (tilingManager.enabled ? ". " + tilingManager.statusText : "")

        Row {
            id: clockRow
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 3

            Text {
                text: controller.timeText
                color: root.colors.text
                font.family: root.uiFont
                font.pixelSize: 25
                font.weight: Font.DemiBold
                font.letterSpacing: -1
                font.features: { "tnum": 1 }
            }
            Text {
                anchors.bottom: parent.children[0].bottom
                anchors.bottomMargin: 4
                text: controller.meridiemText
                color: root.colors.secondary
                font.family: root.uiFont
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
        }

        Item {
            id: calendarView
            y: 48
            width: parent.width
            height: 62
            opacity: actionHover.hovered || tilingManager.adjusting
                     || root.tilingFeedbackActive ? 0 : 1
            scale: actionHover.hovered || tilingManager.adjusting
                   || root.tilingFeedbackActive ? 0.96 : 1

            Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 110 } }
            Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Qt.formatDate(root.currentDate, "ddd").toUpperCase()
                color: root.colors.secondary
                font.family: root.uiFont
                font.pixelSize: 8
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1
            }

            Row {
                y: 19
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 1

                Repeater {
                    model: 7

                    Item {
                        id: dayCell
                        required property int index
                        readonly property date calendarDate: root.weekDate(index)
                        width: 20
                        height: 39

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Qt.formatDate(dayCell.calendarDate, "ddd").charAt(0).toUpperCase()
                            color: dayCell.index === root.currentDayIndex
                                   ? root.colors.secondary : root.colors.tertiary
                            font.family: root.uiFont
                            font.pixelSize: 7
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 13
                            width: 17
                            height: 17
                            radius: 9
                            color: dayCell.index === root.currentDayIndex
                                   ? root.colors.text : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: dayCell.calendarDate.getDate()
                                color: dayCell.index === root.currentDayIndex
                                       ? "#050505" : root.colors.secondary
                                font.family: root.uiFont
                                font.pixelSize: 7
                                font.weight: Font.DemiBold
                                font.features: { "tnum": 1 }
                            }
                        }
                    }
                }
            }
        }

        Item {
            id: tilingStatusView
            y: 48
            width: parent.width
            height: 62
            opacity: (tilingManager.adjusting || root.tilingFeedbackActive)
                     && !actionHover.hovered ? 1 : 0
            scale: (tilingManager.adjusting || root.tilingFeedbackActive)
                   && !actionHover.hovered ? 1 : 0.96

            Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 110 } }
            Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }

            Row {
                anchors.centerIn: parent
                spacing: 10

                Item {
                    width: 38
                    height: 28

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: "transparent"
                        border.width: 1
                        border.color: root.colors.divider
                    }
                    Rectangle {
                        x: 3
                        y: 3
                        width: 14
                        height: 22
                        radius: 3
                        color: root.colors.text
                    }
                    Rectangle {
                        x: 20
                        y: 3
                        width: 15
                        height: 10
                        radius: 3
                        color: root.colors.secondary
                    }
                    Rectangle {
                        x: 20
                        y: 16
                        width: 15
                        height: 9
                        radius: 3
                        color: root.colors.tertiary
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        text: "DWINDLE"
                        color: root.colors.text
                        font.family: root.uiFont
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Text {
                        text: tilingManager.adjusting
                              ? "ARRANGING"
                              : (!tilingManager.enabled
                              ? "OFF"
                              : (tilingManager.tiledWindowCount === 0
                              ? "READY"
                              : (tilingManager.tiledWindowCount === 1
                                 ? "1 WINDOW"
                                 : tilingManager.tiledWindowCount + " WINDOWS")))
                        color: root.colors.tertiary
                        font.family: root.uiFont
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                        font.features: { "tnum": 1 }
                    }
                }
            }
        }

        Item {
            id: actionReveal
            x: 2
            y: 49
            width: parent.width - 4
            height: 59

            HoverHandler { id: actionHover }

            Row {
                anchors.centerIn: parent
                spacing: 6
                opacity: actionHover.hovered ? 1 : 0
                scale: actionHover.hovered ? 1 : 0.94
                enabled: opacity > 0.5

                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 110 } }
                Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 150; easing.type: Easing.OutBack } }

                IslandButton {
                    width: 30
                    height: 30
                    iconOnly: true
                    quiet: true
                    glyph: "\uE121"
                    selected: controller.timerPanelOpen
                    accessibleName: "Open timer"
                    onClicked: controller.openTimer()
                }
                IslandButton {
                    width: 30
                    height: 30
                    iconOnly: true
                    quiet: true
                    iconSource: Qt.resolvedUrl("../assets/icons/grid-light.svg")
                    invertedIconSource: Qt.resolvedUrl("../assets/icons/grid-dark.svg")
                    iconSize: 16
                    selected: tilingManager.enabled
                    accessibleName: tilingManager.enabled
                                    ? "Disable Dwindle tiling"
                                    : "Enable Dwindle tiling"
                    onClicked: tilingManager.toggleEnabled()
                }
                IslandButton {
                    width: 30
                    height: 30
                    iconOnly: true
                    quiet: true
                    iconSource: Qt.resolvedUrl("../assets/icons/speaker-light.svg")
                    invertedIconSource: Qt.resolvedUrl("../assets/icons/speaker-muted-dark.svg")
                    iconSize: 16
                    selected: controller.muted
                    accessibleName: (controller.muted ? "Unmute" : "Mute") + ", " + controller.volume + "%"
                    onClicked: controller.toggleMute()
                }
                IslandButton {
                    width: 30
                    height: 30
                    iconOnly: true
                    quiet: true
                    iconSource: Qt.resolvedUrl("../assets/icons/pin-light.svg")
                    invertedIconSource: Qt.resolvedUrl("../assets/icons/pin-off-dark.svg")
                    iconSize: 15
                    selected: controller.pinned
                    accessibleName: controller.pinned ? "Unpin island" : "Keep island open"
                    onClicked: controller.togglePinned()
                }
                IslandButton {
                    width: 30
                    height: 30
                    iconOnly: true
                    bare: true
                    iconSource: Qt.resolvedUrl("../assets/icons/dismiss-light.svg")
                    iconSize: 15
                    accessibleName: "Collapse"
                    onClicked: {
                        controller.setPinned(false)
                        controller.setExpanded(false)
                    }
                }
            }
        }
    }

    TimerPanel {
        z: 20
        anchors.fill: parent
        visible: controller.timerPanelOpen
        colors: root.colors
        uiFont: root.uiFont
        iconFont: root.iconFont
        reducedMotion: root.reducedMotion
    }
}
