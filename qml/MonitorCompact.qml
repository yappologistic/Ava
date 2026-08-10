pragma ComponentBehavior: Bound

import QtQuick 6.5
import Ava 1.0

Item {
    id: root

    required property var controller
    required property var colors
    property string uiFont: "Inter"
    property bool reducedMotion: false
    signal cpuDetailsRequested()

    function containsCpuPoint(item, x, y) {
        const origin = cpuReadout.mapToItem(item, 0, 0)
        return x >= origin.x && x <= origin.x + cpuReadout.width
            && y >= origin.y && y <= origin.y + cpuReadout.height
    }

    function usageIcon(prefix, value) {
        const state = value >= 90 ? "danger" : (value >= 75 ? "warning" : "neutral")
        return Qt.resolvedUrl("../assets/icons/monitor-" + prefix + "-" + state + ".svg")
    }

    function batteryIcon() {
        if (controller.batteryCharging)
            return Qt.resolvedUrl("../assets/icons/monitor-battery-charging.svg")
        if (controller.batteryAvailable && controller.batteryPercent <= 15)
            return Qt.resolvedUrl("../assets/icons/monitor-battery-danger.svg")
        if (controller.batteryAvailable && controller.batteryPercent <= 30)
            return Qt.resolvedUrl("../assets/icons/monitor-battery-warning.svg")
        return Qt.resolvedUrl("../assets/icons/monitor-battery-neutral.svg")
    }

    component Divider: Rectangle {
        width: 1
        height: 13
        radius: 0.5
        color: root.colors.divider
    }

    component Readout: Item {
        id: readout

        required property string accessibleName
        required property url iconSource
        required property string value
        property string suffix: ""
        property bool charging: false
        property bool interactive: false
        readonly property bool hovered: pointer.containsMouse
        readonly property bool pressed: pointer.pressed
        signal activated()

        width: iconHost.width + 4 + valueHost.width
        height: 18
        scale: interactive
               ? (pressed ? 0.965 : ((hovered || activeFocus) ? 1.025 : 1))
               : 1
        transformOrigin: Item.Center
        Accessible.name: accessibleName + " " + value + suffix
        Accessible.description: interactive ? "Open system monitor details" : ""
        Accessible.role: interactive ? Accessible.Button : Accessible.StaticText
        activeFocusOnTab: interactive

        Behavior on scale {
            NumberAnimation {
                duration: root.reducedMotion ? 0
                                            : (readout.pressed
                                               ? MotionTokens.press
                                               : MotionTokens.hover)
                easing.type: MotionTokens.easeOut
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: 7
            color: root.colors.accent
            opacity: readout.interactive && (readout.hovered || readout.activeFocus)
                     ? (readout.pressed ? 0.13 : 0.08) : 0
            scale: readout.hovered || readout.activeFocus ? 1 : 0.92

            Behavior on opacity {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.hover
                    easing.type: MotionTokens.easeOut
                }
            }
            Behavior on scale {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.hover
                    easing.type: MotionTokens.easeOut
                }
            }
        }

        Keys.onPressed: function(event) {
            if (interactive
                    && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                        || event.key === Qt.Key_Space)) {
                readout.activated()
                event.accepted = true
            }
        }

        MouseArea {
            id: pointer
            anchors.fill: parent
            enabled: readout.interactive
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            preventStealing: true
            propagateComposedEvents: false
            cursorShape: Qt.PointingHandCursor
            onClicked: readout.activated()
        }

        Item {
            id: iconHost
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14

            MorphingIcon {
                anchors.centerIn: parent
                width: 14
                height: 14
                iconWidth: 14
                iconHeight: 14
                source: readout.iconSource
                reducedMotion: root.reducedMotion
            }

            SequentialAnimation {
                id: chargingPulse
                running: readout.charging && !root.reducedMotion
                loops: Animation.Infinite
                onRunningChanged: {
                    if (!running) {
                        iconHost.scale = 1
                        iconHost.opacity = 1
                    }
                }
                ParallelAnimation {
                    NumberAnimation {
                        target: iconHost
                        property: "scale"
                        from: 1
                        to: 1.09
                        duration: 620
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation {
                        target: iconHost
                        property: "opacity"
                        from: 0.78
                        to: 1
                        duration: 620
                        easing.type: Easing.InOutSine
                    }
                }
                ParallelAnimation {
                    NumberAnimation {
                        target: iconHost
                        property: "scale"
                        from: 1.09
                        to: 1
                        duration: 620
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation {
                        target: iconHost
                        property: "opacity"
                        from: 1
                        to: 0.78
                        duration: 620
                        easing.type: Easing.InOutSine
                    }
                }
            }
        }

        Item {
            id: valueHost
            x: iconHost.width + 4
            anchors.verticalCenter: parent.verticalCenter
            width: digits.measuredWidth(readout.value)
                   + (suffixText.visible ? 1.5 + suffixText.implicitWidth : 0)
            height: parent.height

            Behavior on width {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.state
                    easing.type: MotionTokens.easeOut
                }
            }

            RollingDigits {
                id: digits
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: readout.value
                color: readout.interactive && (readout.hovered || readout.activeFocus)
                       ? root.colors.accent : root.colors.text
                fontFamily: root.uiFont
                fontPixelSize: 12
                fontWeight: Font.DemiBold
                letterSpacing: -0.3
                staggerMs: 12
                reducedMotion: root.reducedMotion
                slotReference: "100"
                horizontalAlignment: Text.AlignLeft
            }

            Text {
                id: suffixText
                x: digits.measuredWidth(readout.value) + 1.5
                anchors.verticalCenter: parent.verticalCenter
                visible: text.length > 0
                text: readout.suffix
                color: readout.interactive && (readout.hovered || readout.activeFocus)
                       ? root.colors.accent : root.colors.text
                font.family: root.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.letterSpacing: -0.1

                Behavior on color {
                    ColorAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.hover
                    }
                }
                Behavior on x {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.state
                        easing.type: MotionTokens.easeOut
                    }
                }
            }
        }
    }

    implicitWidth: content.implicitWidth
    implicitHeight: content.implicitHeight

    Row {
        id: content
        anchors.centerIn: parent
        width: implicitWidth
        spacing: 10

        Behavior on width {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.state
                easing.type: MotionTokens.easeOut
            }
        }

        RollingDigits {
            anchors.verticalCenter: parent.verticalCenter
            text: root.controller.timeText
            color: root.colors.text
            fontFamily: root.uiFont
            fontPixelSize: 13
            fontWeight: Font.DemiBold
            letterSpacing: -0.35
            reducedMotion: root.reducedMotion
        }

        Divider { anchors.verticalCenter: parent.verticalCenter }

        Readout {
            anchors.verticalCenter: parent.verticalCenter
            accessibleName: "GPU usage"
            iconSource: root.usageIcon("gpu", root.controller.gpuUsage)
            value: root.controller.gpuUsage < 0 ? "--" : String(root.controller.gpuUsage)
            suffix: root.controller.gpuUsage < 0 ? "" : "%"
        }

        Divider { anchors.verticalCenter: parent.verticalCenter }

        Readout {
            id: cpuReadout
            anchors.verticalCenter: parent.verticalCenter
            accessibleName: "CPU usage"
            iconSource: root.usageIcon("cpu", root.controller.cpuUsage)
            value: root.controller.cpuUsage < 0 ? "--" : String(root.controller.cpuUsage)
            suffix: root.controller.cpuUsage < 0 ? "" : "%"
            interactive: true
            onActivated: root.cpuDetailsRequested()
        }

        Divider { anchors.verticalCenter: parent.verticalCenter }

        Readout {
            anchors.verticalCenter: parent.verticalCenter
            accessibleName: "Battery level"
            iconSource: root.batteryIcon()
            value: root.controller.batteryAvailable
                   ? String(root.controller.batteryPercent) : "--"
            suffix: root.controller.batteryAvailable ? "%" : ""
            charging: root.controller.batteryCharging
        }
    }
}
