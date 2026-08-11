import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Window 6.5
import Ava 1.0

Window {
    id: root

    required property var manager
    property bool reducedMotion: false
    property int previousSelectedIndex: -1
    property int outgoingIndex: -1
    property int hiddenIndex: -1
    property bool recyclingCard: false

    objectName: "enhancedAltTabWindow"
    visible: manager.active
    x: manager.virtualLeft
    y: manager.virtualTop
    width: manager.virtualWidth
    height: manager.virtualHeight
    color: "#030507"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | Qt.Tool | Qt.NoDropShadowWindowHint
    title: "Ava Enhanced Alt-Tab"

    function forwardDistance(index) {
        if (manager.windowCount <= 0 || manager.selectedIndex < 0)
            return 0
        let distance = index - manager.selectedIndex
        if (distance < 0)
            distance += manager.windowCount
        return distance
    }

    onVisibleChanged: {
        if (visible) {
            previousSelectedIndex = manager.selectedIndex
            outgoingIndex = -1
            hiddenIndex = -1
            requestActivate()
            keyboardSurface.forceActiveFocus()
        } else {
            previousSelectedIndex = -1
            outgoingIndex = -1
            hiddenIndex = -1
        }
    }
    onClosing: function(close) {
        manager.cancel()
        close.accepted = true
    }

    Image {
        anchors.fill: parent
        source: root.manager.wallpaperUrl
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        visible: source.toString().length > 0
    }

    Rectangle {
        anchors.fill: parent
        color: "#18000000"
        opacity: root.manager.committing ? 0 : 1

        Behavior on opacity {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : 170
                easing.type: Easing.OutCubic
            }
        }
    }

    Connections {
        target: root.manager

        function onSelectedIndexChanged() {
            const nextIndex = root.manager.selectedIndex
            if (!root.visible || nextIndex < 0) {
                root.previousSelectedIndex = nextIndex
                return
            }
            if (root.previousSelectedIndex >= 0
                    && root.previousSelectedIndex !== nextIndex) {
                root.outgoingIndex = root.previousSelectedIndex
                recycleTimer.restart()
            }
            root.previousSelectedIndex = nextIndex
        }
    }

    Timer {
        id: recycleTimer
        interval: root.reducedMotion ? 1 : 190
        onTriggered: {
            root.recyclingCard = true
            root.hiddenIndex = root.outgoingIndex
            root.outgoingIndex = -1
            Qt.callLater(function() {
                root.recyclingCard = false
                revealTimer.restart()
            })
        }
    }

    Timer {
        id: revealTimer
        interval: root.reducedMotion ? 1 : 45
        onTriggered: root.hiddenIndex = -1
    }

    Item {
        id: keyboardSurface
        anchors.fill: parent
        focus: true

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                root.manager.cancel()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.manager.accept()
                event.accepted = true
            } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Backtab) {
                root.manager.step(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_Right || event.key === Qt.Key_Tab) {
                root.manager.step(1)
                event.accepted = true
            }
        }

        Repeater {
            model: root.manager

            delegate: Item {
                id: card
                required property int index
                required property string windowKey
                required property string windowTitle
                required property string applicationName
                required property bool windowMinimized
                required property real windowAspectRatio
                required property bool captureReady

                readonly property int slot: root.forwardDistance(index)
                readonly property bool selected: index === root.manager.selectedIndex
                readonly property bool outgoing: index === root.outgoingIndex
                                                   && !selected
                readonly property bool onStage: slot <= Math.min(6,
                                                                  root.manager.windowCount - 1)
                readonly property real boundedAspect: Math.max(0.55,
                                                                Math.min(2.4,
                                                                         windowAspectRatio))
                readonly property real maximumWidth: Math.min(root.width * 0.36, 690)
                readonly property real maximumHeight: Math.min(root.height * 0.50, 590)
                readonly property real cardWidth: Math.min(maximumWidth,
                                                            maximumHeight * boundedAspect)
                readonly property real cardHeight: cardWidth / boundedAspect
                readonly property real stageSpacing: Math.min(root.width * 0.067, 142)
                readonly property real stageCenterX: root.width * 0.655
                readonly property real stageCenterY: root.height * 0.585
                readonly property real commitScale: Math.max(root.width / width,
                                                               root.height / height) * 1.025

                x: root.manager.committing && selected
                   ? root.width / 2 - width / 2
                   : outgoing
                     ? root.width * 0.79 - width / 2
                     : stageCenterX - width / 2 - slot * stageSpacing
                y: root.manager.committing && selected
                   ? root.height / 2 - height / 2
                   : outgoing
                     ? root.height * 0.62 - height / 2
                     : stageCenterY - height / 2
                       - slot * Math.min(root.height * 0.038, 35)
                width: cardWidth
                height: cardHeight
                z: root.manager.committing && selected ? 4000
                   : outgoing ? 3000 : 1500 - slot * 20
                scale: root.manager.committing && selected ? commitScale
                       : outgoing ? 0.94 : Math.max(0.62, 1 - slot * 0.072)
                opacity: root.manager.committing ? (selected ? 1 : 0)
                         : outgoing || index === root.hiddenIndex ? 0
                         : onStage ? Math.max(0.58, 1 - slot * 0.065) : 0
                visible: opacity > 0.01
                Accessible.role: Accessible.Button
                Accessible.name: applicationName + ": " + windowTitle
                Accessible.focused: selected

                transform: Rotation {
                    origin.x: card.width / 2
                    origin.y: card.height / 2
                    axis.x: 0
                    axis.y: 1
                    axis.z: 0
                    angle: root.manager.committing && card.selected ? 0
                           : card.outgoing ? 24
                           : 38

                    Behavior on angle {
                        enabled: !root.recyclingCard
                        NumberAnimation {
                            duration: root.reducedMotion ? 0 : 185
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                Behavior on x {
                    enabled: !root.recyclingCard
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : 185
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on y {
                    enabled: !root.recyclingCard
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : 185
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on scale {
                    enabled: !root.recyclingCard
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : 185
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on opacity {
                    enabled: !root.recyclingCard
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : 155
                        easing.type: Easing.OutCubic
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.topMargin: 10
                    z: -1
                    radius: cardSurface.radius
                    color: "#52000000"
                    visible: !root.manager.committing
                }

                Rectangle {
                    id: cardSurface
                    anchors.fill: parent
                    radius: Math.max(5, 8 / card.scale)
                    color: "#f2171b22"
                    clip: true

                    Rectangle {
                        anchors.fill: parent
                        color: "#161b23"

                        Column {
                            anchors.centerIn: parent
                            spacing: 12
                            opacity: card.captureReady ? 0 : 1

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 58
                                height: 58
                                radius: 15
                                color: "#26313d"
                                border.width: 1
                                border.color: "#3effffff"

                                Text {
                                    anchors.centerIn: parent
                                    text: card.applicationName.length > 0
                                          ? card.applicationName.charAt(0).toUpperCase() : "•"
                                    color: "#f4f7fb"
                                    font.family: "Inter"
                                    font.pixelSize: 24
                                    font.weight: Font.DemiBold
                                }
                            }

                            Text {
                                width: Math.min(card.width - 60, 420)
                                text: card.windowMinimized ? "Minimized window" : "Preparing preview"
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                color: "#9da8b7"
                                font.family: "Inter"
                                font.pixelSize: 13
                            }
                        }
                    }

                    EnhancedTabTexture {
                        anchors.fill: parent
                        manager: root.manager
                        windowKey: card.windowKey
                        opacity: card.captureReady ? 1 : 0

                        Behavior on opacity {
                            NumberAnimation { duration: root.reducedMotion ? 0 : 100 }
                        }
                    }

                }

                TapHandler {
                    onTapped: {
                        if (card.selected)
                            root.manager.accept()
                        else
                            root.manager.select(card.index)
                    }
                }
            }
        }

    }
}
