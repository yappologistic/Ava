pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Effects 6.5

Item {
    id: root

    property var colors
    property string uiFont: "Inter"
    property string iconFont: "Segoe Fluent Icons"
    property bool reducedMotion: false
    property bool open: false
    property real shellCornerRadius: 28
    property int pendingIndex: controller.wallpaperIndex
    property real handoffProgress: 0
    readonly property int wallpaperCount: wallpaperModel.count

    function revealIndex(index) {
        const boundedIndex = Math.max(0, Math.min(wallpaperCount - 1, index))
        wallpaperGrid.currentIndex = boundedIndex
        wallpaperGrid.positionViewAtIndex(boundedIndex, GridView.Contain)
    }

    function chooseWallpaper(index) {
        if (index < 0 || index >= wallpaperCount)
            return
        if (root.reducedMotion) {
            root.pendingIndex = index
            controller.setWallpaper(index)
            revealIndex(index)
            return
        }
        if (wallpaperTransition.running)
            return
        root.pendingIndex = index
        wallpaperTransition.restart()
    }

    focus: open
    Keys.onLeftPressed: chooseWallpaper(Math.max(0, controller.wallpaperIndex - 1))
    Keys.onRightPressed: chooseWallpaper(Math.min(wallpaperCount - 1,
                                                   controller.wallpaperIndex + 1))
    Keys.onUpPressed: chooseWallpaper(Math.max(0, controller.wallpaperIndex - 4))
    Keys.onDownPressed: chooseWallpaper(Math.min(wallpaperCount - 1,
                                                 controller.wallpaperIndex + 4))
    Keys.onEscapePressed: controller.closeWallpaperPanel()

    onOpenChanged: {
        if (open)
            revealTimer.restart()
    }

    Timer {
        id: revealTimer
        interval: 40
        repeat: false
        onTriggered: root.revealIndex(controller.wallpaperIndex)
    }

    ListModel {
        id: wallpaperModel
        ListElement { name: "Alpine First Light"; sourcePath: "../assets/wallpapers/runtime/01-alpine-first-light.jpg" }
        ListElement { name: "Volcanic Coast"; sourcePath: "../assets/wallpapers/runtime/02-volcanic-coast-after-rain.jpg" }
        ListElement { name: "Redwood Creek"; sourcePath: "../assets/wallpapers/runtime/03-redwood-creek-mist.jpg" }
        ListElement { name: "Highland River"; sourcePath: "../assets/wallpapers/runtime/04-highland-river-clearing.jpg" }
        ListElement { name: "Glacial Lagoon"; sourcePath: "../assets/wallpapers/runtime/05-glacial-lagoon-blue-hour.jpg" }
        ListElement { name: "Sandstone Storm"; sourcePath: "../assets/wallpapers/runtime/06-sandstone-stormlight.jpg" }
        ListElement { name: "Autumn Larch"; sourcePath: "../assets/wallpapers/runtime/07-autumn-larch-valley.jpg" }
        ListElement { name: "Moonlit Winter"; sourcePath: "../assets/wallpapers/runtime/08-moonlit-winter-pond.jpg" }
        ListElement { name: "Volcanic Dusk"; sourcePath: "../assets/wallpapers/runtime/09-volcanic-braided-dusk.jpg" }
        ListElement { name: "Coastal Dunes"; sourcePath: "../assets/wallpapers/runtime/10-coastal-dunes-sunrise.jpg" }
        ListElement { name: "Basalt Falls"; sourcePath: "../assets/wallpapers/runtime/11-rainforest-basalt-falls.jpg" }
        ListElement { name: "Salt Flat Sunset"; sourcePath: "../assets/wallpapers/runtime/12-salt-flat-storm-sunset.jpg" }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        width: parent.width - 54
        height: 34
        radius: Math.min(height / 2, root.shellCornerRadius * 0.62)
        color: "#0dffffff"
        border.width: 1
        border.color: "#0dffffff"
        opacity: 0.65
    }

    Text {
        x: 30
        y: 19
        text: "Wallpaper"
        color: root.colors.text
        font.family: root.uiFont
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }

    Text {
        anchors.right: closeButton.left
        anchors.rightMargin: 11
        y: 20
        text: controller.wallpaperStatus.length > 0
              ? controller.wallpaperStatus
              : (controller.wallpaperIndex + 1) + " / " + root.wallpaperCount
        color: controller.wallpaperStatus.indexOf("Couldn") === 0
               ? "#ff9f8f" : root.colors.tertiary
        font.family: root.uiFont
        font.pixelSize: 9
        font.weight: Font.Medium
        font.features: { "tnum": 1 }
    }

    IslandButton {
        id: closeButton
        anchors.right: parent.right
        anchors.rightMargin: 24
        y: 12
        width: 27
        height: 27
        iconOnly: true
        bare: true
        glyph: "\uE711"
        accessibleName: "Close wallpaper menu"
        onClicked: controller.closeWallpaperPanel()
    }

    GridView {
        id: wallpaperGrid
        anchors.horizontalCenter: parent.horizontalCenter
        y: 50
        width: 536
        height: parent.height - 64
        cellWidth: 134
        cellHeight: 82
        clip: true
        boundsBehavior: Flickable.DragOverBounds
        flickDeceleration: 3400
        maximumFlickVelocity: 2100
        model: wallpaperModel
        currentIndex: controller.wallpaperIndex
        cacheBuffer: 220
        keyNavigationEnabled: false

        delegate: Item {
            id: wallpaperDelegate
            required property int index
            required property string name
            required property string sourcePath
            width: wallpaperGrid.cellWidth
            height: wallpaperGrid.cellHeight
            readonly property bool applied: index === controller.wallpaperIndex
            readonly property bool pending: wallpaperTransition.running
                                            && index === root.pendingIndex
            readonly property bool emphasized: applied || pending
            // Match the wallpaper shell's corner treatment without turning
            // the shorter thumbnail cards into capsules.
            readonly property real cardRadius: Math.min(height / 2,
                                                         root.shellCornerRadius * 0.62)

            HoverHandler { id: wallpaperHover }
            TapHandler {
                id: wallpaperTap
                onTapped: root.chooseWallpaper(wallpaperDelegate.index)
            }

            Accessible.name: name + (applied ? ", selected" : "")
            Accessible.role: Accessible.Button

            Rectangle {
                id: wallpaperFrame
                anchors.horizontalCenter: parent.horizontalCenter
                y: wallpaperHover.hovered ? 0 : 2
                width: 128
                height: 72
                radius: wallpaperDelegate.cardRadius
                color: wallpaperDelegate.emphasized
                       ? root.colors.calendarAccent : root.colors.divider
                antialiasing: true
                scale: wallpaperTap.pressed ? 0.975
                       : (wallpaperDelegate.emphasized ? 1.025
                          : (wallpaperHover.hovered ? 1.018 : 0.97))
                transformOrigin: Item.Center

                Behavior on y {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.hover
                        easing.type: MotionTokens.easeOut
                    }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.hover
                        easing.type: MotionTokens.settle
                    }
                }
                Behavior on color {
                    ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state }
                }

                Item {
                    id: thumbnailViewport
                    anchors.fill: parent
                    anchors.margins: 2

                    Item {
                        id: thumbnailSource
                        anchors.fill: parent
                        visible: false
                        layer.enabled: true

                        Rectangle {
                            anchors.fill: parent
                            color: root.colors.raised
                        }

                        Image {
                            anchors.fill: parent
                            source: wallpaperDelegate.sourcePath
                            // Decode a thumbnail-sized preview from the exact
                            // embedded 4K PNG. The wallpaper backend still
                            // materializes the untouched full-resolution file.
                            sourceSize: Qt.size(512, 288)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: false
                            cache: true
                            smooth: true
                            mipmap: true
                        }

                        Rectangle {
                            anchors.fill: parent
                            color: "black"
                            opacity: wallpaperDelegate.emphasized ? 0
                                     : (wallpaperHover.hovered ? 0.02 : 0.14)
                            Behavior on opacity {
                                NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
                            }
                        }

                        Rectangle {
                            visible: wallpaperDelegate.pending
                            width: 28
                            height: parent.height * 1.6
                            rotation: 16
                            x: -width + (parent.width + width * 2) * root.handoffProgress
                            y: -parent.height * 0.3
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "transparent" }
                                GradientStop { position: 0.5; color: "#70ffffff" }
                                GradientStop { position: 1; color: "transparent" }
                            }
                        }
                    }

                    Rectangle {
                        id: thumbnailMask
                        anchors.fill: parent
                        radius: Math.max(0, wallpaperFrame.radius - 2)
                        color: "white"
                        antialiasing: true
                        visible: false
                        layer.enabled: true
                        layer.smooth: true
                        layer.samples: 4
                    }

                    MultiEffect {
                        anchors.fill: parent
                        source: thumbnailSource
                        maskEnabled: true
                        maskSource: thumbnailMask
                        // A zero threshold leaves fully transparent mask pixels
                        // uncut, which lets the rectangular source cover the
                        // rounded ring. Cut at mid-alpha and retain a soft,
                        // antialiased transition along the curve.
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 77
                width: wallpaperDelegate.emphasized ? 20 : 4
                height: 2
                radius: 1
                color: wallpaperDelegate.emphasized
                       ? root.colors.calendarAccent : root.colors.divider
                opacity: wallpaperDelegate.emphasized ? 1 : 0.48
                Behavior on width {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.state
                        easing.type: MotionTokens.easeOut
                    }
                }
                Behavior on color {
                    ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state }
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
            id: wallpaperScrollBar
            policy: ScrollBar.AsNeeded
            width: 3
            opacity: wallpaperGrid.moving || hovered || pressed ? 0.72 : 0.18
            contentItem: Rectangle {
                implicitWidth: 3
                radius: 1.5
                color: root.colors.secondary
            }
            background: Item { }
            Behavior on opacity {
                NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state }
            }
        }
    }

    SequentialAnimation {
        id: wallpaperTransition
        ParallelAnimation {
            NumberAnimation { target: wallpaperGrid; property: "opacity"; to: 0.78; duration: root.reducedMotion ? 0 : MotionTokens.press; easing.type: Easing.InCubic }
            NumberAnimation { target: wallpaperGrid; property: "scale"; to: 0.992; duration: root.reducedMotion ? 0 : MotionTokens.press; easing.type: Easing.InCubic }
            NumberAnimation { target: root; property: "handoffProgress"; from: 0; to: 0.42; duration: root.reducedMotion ? 0 : MotionTokens.press }
        }
        ScriptAction {
            script: {
                wallpaperGrid.currentIndex = root.pendingIndex
                wallpaperGrid.positionViewAtIndex(root.pendingIndex, GridView.Contain)
                controller.setWallpaper(root.pendingIndex)
            }
        }
        ParallelAnimation {
            NumberAnimation { target: wallpaperGrid; property: "opacity"; to: 1; duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut }
            NumberAnimation { target: wallpaperGrid; property: "scale"; to: 1; duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.settle }
            NumberAnimation { target: root; property: "handoffProgress"; to: 1; duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut }
        }
        ScriptAction { script: root.handoffProgress = 0 }
    }
}
