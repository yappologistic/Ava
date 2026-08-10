import QtQuick 6.5
import Ava 1.0

Item {
    id: root

    required property var controller
    required property var colors
    property bool reducedMotion: false
    property bool glassEnabled: false
    property var glassBackdrop: null
    property bool active: false
    readonly property bool hovered: pointer.hovered

    visible: opacity > 0.001
    opacity: active ? 1 : 0
    scale: active ? (pointer.hovered ? 1.055 : 1) : 0.72
    transformOrigin: Item.Center

    Accessible.name: controller.timerPaused
                     ? "Timer paused, " + controller.timerRemainingText + " remaining"
                     : "Timer, " + controller.timerRemainingText + " remaining"
    Accessible.role: Accessible.Button

    Behavior on opacity {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : MotionTokens.state
            easing.type: MotionTokens.easeOut
        }
    }
    Behavior on scale {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : MotionTokens.directSettle
            easing.type: MotionTokens.settle
        }
    }

    Rectangle {
        id: halo
        anchors.centerIn: parent
        width: parent.width + 5
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: root.colors.timer
        opacity: root.controller.timerPaused ? 0.14 : 0.34
        scale: 0.94

        SequentialAnimation {
            running: root.active && !root.controller.timerPaused && !root.reducedMotion
            loops: Animation.Infinite
            ParallelAnimation {
                NumberAnimation { target: halo; property: "scale"; from: 0.94; to: 1.05; duration: 760; easing.type: Easing.OutCubic }
                NumberAnimation { target: halo; property: "opacity"; from: 0.38; to: 0.08; duration: 760; easing.type: Easing.OutCubic }
            }
            ScriptAction { script: { halo.scale = 0.94; halo.opacity = 0.38 } }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.colors.black
        border.width: 1
        border.color: pointer.hovered ? root.colors.text : root.colors.timer

        Behavior on border.color {
            ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
        }
    }

    LiquidGlassTexture {
        id: timerTextureSource
        anchors.fill: parent
        backdrop: root.glassBackdrop
        timer: true
        enabled: true
        opacity: 1
        visible: root.active && root.glassBackdrop
                 && root.glassBackdrop.timerFrameAvailable
    }

    Image {
        anchors.centerIn: parent
        width: 16
        height: 16
        source: Qt.resolvedUrl("../assets/icons/timer-orange.svg")
        sourceSize.width: 32
        sourceSize.height: 32
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        opacity: root.controller.timerPaused ? 0.55 : 1

        Behavior on opacity {
            NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state }
        }
    }

    Rectangle {
        readonly property real angle: (-90 + 360 * (1 - root.controller.timerProgress))
                                       * Math.PI / 180
        width: 4
        height: 4
        radius: 2
        color: root.colors.timer
        x: parent.width / 2 + Math.cos(angle) * (parent.width / 2 - 2) - width / 2
        y: parent.height / 2 + Math.sin(angle) * (parent.height / 2 - 2) - height / 2
        opacity: root.controller.timerPaused ? 0.45 : 1

        Behavior on x { NumberAnimation { duration: root.reducedMotion ? 0 : 90; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 90; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
    }

    HoverHandler { id: pointer }

    TapHandler {
        enabled: root.active
        onTapped: {
            root.controller.openTimer()
            root.controller.setExpanded(true)
        }
    }
}
