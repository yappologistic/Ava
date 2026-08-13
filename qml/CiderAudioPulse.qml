pragma ComponentBehavior: Bound

import QtQuick 6.5
import Ava 1.0

Item {
    id: root

    required property var provider
    required property color accentColor
    required property bool reducedMotion
    property bool active: false
    readonly property real energy: Math.max(0, Math.min(1,
                                                       provider.audioLevel))

    implicitWidth: 42
    implicitHeight: 10
    visible: active
    opacity: active ? 0.58 + energy * 0.42 : 0

    Behavior on opacity {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : MotionTokens.state
            easing.type: MotionTokens.easeOut
        }
    }

    Row {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Repeater {
            model: [
                { low: 0.18, high: 0.70, rise: 190, fall: 260 },
                { low: 0.38, high: 1.00, rise: 240, fall: 170 },
                { low: 0.24, high: 0.82, rise: 160, fall: 230 },
                { low: 0.46, high: 0.92, rise: 270, fall: 200 },
                { low: 0.20, high: 0.76, rise: 180, fall: 290 },
                { low: 0.34, high: 0.96, rise: 220, fall: 180 },
                { low: 0.16, high: 0.66, rise: 150, fall: 250 },
                { low: 0.30, high: 0.84, rise: 250, fall: 210 }
            ]

            Rectangle {
                id: pulseBar
                required property var modelData
                property real wave: modelData.low
                width: 3
                height: root.reducedMotion
                        ? Math.max(1, root.energy * 8)
                        : Math.max(1, 1 + root.energy * (2 + wave * 7))
                anchors.verticalCenter: parent.verticalCenter
                radius: 1.5
                color: root.accentColor

                SequentialAnimation on wave {
                    running: root.active && !root.reducedMotion
                             && root.energy > 0.004
                    loops: Animation.Infinite
                    NumberAnimation {
                        to: pulseBar.modelData.high
                        duration: pulseBar.modelData.rise
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation {
                        to: pulseBar.modelData.low
                        duration: pulseBar.modelData.fall
                        easing.type: Easing.InOutSine
                    }
                }
                Behavior on color {
                    ColorAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.state
                    }
                }
            }
        }
    }
}
