pragma ComponentBehavior: Bound

import QtQuick 6.5

Item {
    id: root

    required property var controller
    required property var colors
    property string uiFont: "Inter"
    property bool reducedMotion: false

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

    component Readout: Row {
        id: readout

        required property string accessibleName
        required property url iconSource
        required property string value
        property string suffix: ""
        property bool charging: false

        spacing: 4
        height: 18
        Accessible.name: accessibleName + " " + value + suffix
        Accessible.role: Accessible.StaticText

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

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1.5

            RollingDigits {
                anchors.verticalCenter: parent.verticalCenter
                text: readout.value
                color: root.colors.text
                fontFamily: root.uiFont
                fontPixelSize: 12
                fontWeight: Font.DemiBold
                letterSpacing: -0.3
                staggerMs: 12
                reducedMotion: root.reducedMotion
                rollDirection: 1
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: text.length > 0
                text: readout.suffix
                color: root.colors.text
                font.family: root.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.letterSpacing: -0.1
            }
        }
    }

    implicitWidth: content.implicitWidth
    implicitHeight: content.implicitHeight

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 10

        RollingDigits {
            anchors.verticalCenter: parent.verticalCenter
            text: root.controller.timeText
            color: root.colors.text
            fontFamily: root.uiFont
            fontPixelSize: 13
            fontWeight: Font.DemiBold
            letterSpacing: -0.35
            reducedMotion: root.reducedMotion
            rollDirection: 1
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
            anchors.verticalCenter: parent.verticalCenter
            accessibleName: "CPU usage"
            iconSource: root.usageIcon("cpu", root.controller.cpuUsage)
            value: root.controller.cpuUsage < 0 ? "--" : String(root.controller.cpuUsage)
            suffix: root.controller.cpuUsage < 0 ? "" : "%"
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
