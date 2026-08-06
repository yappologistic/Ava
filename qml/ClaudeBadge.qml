import QtQuick 6.5

// Claude Code's presence on the island: the mark, and a pulse while it works.
Item {
    id: badge

    property real iconSize: 14
    property bool reducedMotion: false
    property color workingColor: "#63e6a5"
    property color waitingColor: "#d97757"
    property color shellColor: "#000000"
    readonly property bool waiting: claude.requestActive
    readonly property bool active: claude.busy || waiting

    implicitWidth: iconSize + 6
    implicitHeight: iconSize + 6
    visible: opacity > 0.001
    opacity: active ? 1 : 0
    scale: active ? 1 : 0.72
    transformOrigin: Item.Center

    Accessible.role: Accessible.Indicator
    Accessible.name: waiting ? "Claude Code needs an answer" : "Claude Code is working"

    Behavior on opacity {
        NumberAnimation {
            duration: badge.reducedMotion ? 0 : MotionTokens.state
            easing.type: MotionTokens.easeOut
        }
    }
    Behavior on scale {
        NumberAnimation {
            duration: badge.reducedMotion ? 0 : MotionTokens.activityHandoff
            easing.type: MotionTokens.settle
        }
    }

    Image {
        id: mark
        anchors.centerIn: parent
        width: badge.iconSize
        height: badge.iconSize
        source: Qt.resolvedUrl("../assets/icons/claude.svg")
        sourceSize.width: Math.ceil(badge.iconSize * 3)
        sourceSize.height: Math.ceil(badge.iconSize * 3)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    // The shell-coloured ring keeps the pulse legible over the mark's rays.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        width: 8
        height: 8
        radius: 4
        color: badge.shellColor

        Rectangle {
            id: dot
            anchors.centerIn: parent
            width: 5
            height: 5
            radius: 2.5
            color: badge.waiting ? badge.waitingColor : badge.workingColor

            Behavior on color {
                ColorAnimation { duration: badge.reducedMotion ? 0 : MotionTokens.content }
            }

            SequentialAnimation on opacity {
                running: badge.active && !badge.reducedMotion
                loops: Animation.Infinite
                NumberAnimation { to: 0.2; duration: 620; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: 620; easing.type: Easing.InOutSine }
            }
        }
    }
}
