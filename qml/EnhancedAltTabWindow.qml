import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Window 6.5
import Ava 1.0

Window {
    id: root

    required property var manager
    property bool reducedMotion: false
    property bool presented: false

    objectName: "enhancedAltTabWindow"
    visible: manager.active
    x: manager.virtualLeft
    y: manager.virtualTop
    width: manager.virtualWidth
    height: manager.virtualHeight
    color: "transparent"
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
            presented = false
            presentationTimer.restart()
            requestActivate()
            keyboardSurface.forceActiveFocus()
        } else {
            presentationTimer.stop()
            presented = false
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
        opacity: root.presented ? 1 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.state
                easing.type: MotionTokens.easeOut
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#18000000"
        opacity: root.manager.committing ? 0 : (root.presented ? 1 : 0)

        Behavior on opacity {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.state
                easing.type: MotionTokens.easeOut
            }
        }
    }

    Timer {
        id: presentationTimer
        interval: root.reducedMotion ? 0 : 16
        onTriggered: root.presented = true
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
                required property int windowFrameX
                required property int windowFrameY
                required property int windowFrameWidth
                required property int windowFrameHeight

                readonly property int slot: root.forwardDistance(index)
                readonly property bool selected: index === root.manager.selectedIndex
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
                readonly property real localFrameX: windowFrameX
                                                     - root.manager.virtualLeft
                readonly property real localFrameY: windowFrameY
                                                     - root.manager.virtualTop
                readonly property real clippedFrameLeft: Math.max(0, localFrameX)
                readonly property real clippedFrameTop: Math.max(0, localFrameY)
                readonly property real clippedFrameRight: Math.min(
                                                               root.width,
                                                               localFrameX
                                                               + windowFrameWidth)
                readonly property real clippedFrameBottom: Math.min(
                                                                root.height,
                                                                localFrameY
                                                                + windowFrameHeight)
                readonly property bool commitFrameUsable: !windowMinimized
                                                           && clippedFrameRight
                                                              - clippedFrameLeft > 80
                                                           && clippedFrameBottom
                                                              - clippedFrameTop > 60
                readonly property real commitX: commitFrameUsable
                                                ? clippedFrameLeft : 0
                readonly property real commitY: commitFrameUsable
                                                ? clippedFrameTop : 0
                readonly property real commitWidth: commitFrameUsable
                                                    ? clippedFrameRight
                                                      - clippedFrameLeft
                                                    : root.width
                readonly property real commitHeight: commitFrameUsable
                                                     ? clippedFrameBottom
                                                       - clippedFrameTop
                                                     : root.height
                readonly property real finalX: stageCenterX - cardWidth / 2
                                                - slot * stageSpacing
                readonly property real finalY: stageCenterY - cardHeight / 2
                                                - slot * Math.min(root.height * 0.038,
                                                                  35)
                readonly property real finalScale: Math.max(0.62,
                                                             1 - slot * 0.072)
                readonly property real finalOpacity: onStage
                                                      ? Math.max(0.58,
                                                                 1 - slot * 0.065)
                                                      : 0
                property bool selectionInitialized: false
                property bool wasSelected: false
                property bool retiring: false
                property bool teleporting: false
                property real entranceProgress: root.presented ? 1 : 0
                property real commitProgress: root.manager.committing && selected
                                              ? 1 : 0

                Component.onCompleted: {
                    wasSelected = selected
                    selectionInitialized = true
                }
                onSelectedChanged: {
                    if (!selectionInitialized)
                        return
                    if (selected) {
                        retireTimer.stop()
                        retiring = false
                        teleporting = false
                    } else if (wasSelected && root.visible) {
                        retiring = true
                        retireTimer.restart()
                    }
                    wasSelected = selected
                }

                Timer {
                    id: retireTimer
                    interval: root.reducedMotion ? 1 : MotionTokens.state
                    onTriggered: {
                        card.teleporting = true
                        card.retiring = false
                        Qt.callLater(function() {
                            card.teleporting = false
                        })
                    }
                }

                Behavior on entranceProgress {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.content
                        easing.type: MotionTokens.easeOut
                    }
                }

                Behavior on commitProgress {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : 190
                        easing.type: MotionTokens.settle
                    }
                }

                x: retiring
                     ? root.width * 0.80 - width / 2
                     : selected
                       ? finalX + (commitX - finalX) * commitProgress
                       : finalX
                y: retiring
                     ? root.height * 0.615 - height / 2
                     : selected
                       ? finalY + (commitY - finalY) * commitProgress
                       : finalY
                width: cardWidth + (commitWidth - cardWidth) * commitProgress
                height: cardHeight + (commitHeight - cardHeight) * commitProgress
                z: root.manager.committing && selected ? 4000
                   : retiring ? 3000 : 1500 - slot * 20
                scale: retiring ? 0.955
                       : selected
                         ? finalScale + (1 - finalScale) * commitProgress
                         : finalScale
                opacity: root.manager.committing ? (selected ? 1 : 0)
                         : retiring || teleporting ? 0
                         : finalOpacity * (selected
                                           ? 0.56 + entranceProgress * 0.44
                                           : entranceProgress)
                visible: opacity > 0.01
                Accessible.role: Accessible.Button
                Accessible.name: applicationName + ": " + windowTitle
                Accessible.focused: selected

                transform: [
                    Rotation {
                        origin.x: card.width / 2
                        origin.y: card.height / 2
                        axis.x: 0
                        axis.y: 1
                        axis.z: 0
                        readonly property real restingAngle:
                            38 - (card.selected ? 8 : 4)
                            * (1 - card.entranceProgress)
                        angle: card.retiring ? 24
                               : restingAngle * (1 - card.commitProgress)

                        Behavior on angle {
                            enabled: !card.teleporting
                                     && !root.manager.committing
                                     && (card.entranceProgress >= 0.999
                                         || card.retiring)
                            NumberAnimation {
                                duration: root.reducedMotion ? 0
                                          : MotionTokens.state
                                easing.type: MotionTokens.easeOut
                            }
                        }
                    },
                    Scale {
                        origin.x: card.width / 2
                        origin.y: card.height / 2
                        xScale: 1 + (card.selected ? 0.045 : 0.022)
                                * (1 - card.entranceProgress)
                        yScale: xScale
                    },
                    Translate {
                        x: (card.selected ? card.stageSpacing * 0.22
                                          : card.stageSpacing
                                            * (0.34 + Math.min(card.slot, 6) * 0.025))
                           * (1 - card.entranceProgress)
                        y: (card.selected ? 14 : 10 + Math.min(card.slot, 6) * 4)
                           * (1 - card.entranceProgress)
                    }
                ]

                Behavior on x {
                    enabled: !card.teleporting && !root.manager.committing
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.state
                        easing.type: MotionTokens.easeOut
                    }
                }
                Behavior on y {
                    enabled: !card.teleporting && !root.manager.committing
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.state
                        easing.type: MotionTokens.easeOut
                    }
                }
                Behavior on scale {
                    enabled: !card.teleporting && !root.manager.committing
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.state
                        easing.type: MotionTokens.easeOut
                    }
                }
                Behavior on opacity {
                    enabled: !card.teleporting
                             && (card.entranceProgress >= 0.999
                                 || root.manager.committing
                                 || card.retiring)
                    NumberAnimation {
                        duration: root.reducedMotion ? 0
                                  : (root.manager.committing
                                     ? 105 : MotionTokens.hover)
                        easing.type: MotionTokens.easeOut
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.topMargin: 10
                    z: -1
                    radius: cardSurface.radius
                    color: "#52000000"
                    opacity: 1 - card.commitProgress
                    visible: opacity > 0.01
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
                            opacity: card.captureReady ? 0
                                     : Math.max(0,
                                                Math.min(1,
                                                         (card.entranceProgress - 0.45)
                                                         / 0.55))

                            Behavior on opacity {
                                enabled: card.entranceProgress >= 0.999
                                NumberAnimation {
                                    duration: root.reducedMotion ? 0 : 100
                                    easing.type: MotionTokens.easeOut
                                }
                            }

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
                            NumberAnimation {
                                duration: root.reducedMotion ? 0 : MotionTokens.hover
                                easing.type: MotionTokens.easeOut
                            }
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
