pragma ComponentBehavior: Bound

import QtQuick 6.5

Item {
    id: root

    required property var controller
    required property var colors
    property string uiFont: "Inter"
    property string monoFont: "Geist Mono"
    property string iconFont: "Segoe Fluent Icons"
    property bool reducedMotion: false
    property bool open: false

    function metricColor(value) {
        if (value >= 90)
            return colors.danger
        if (value >= 75)
            return colors.warning
        return colors.accent
    }

    component Metric: Item {
        id: metric

        required property string label
        required property int value
        required property string detail

        height: 76
        Accessible.name: label + " " + (value < 0 ? "unavailable" : value + " percent")
        Accessible.description: detail
        Accessible.role: Accessible.StaticText

        Text {
            text: metric.label.toUpperCase()
            color: root.colors.tertiary
            font.family: root.uiFont
            font.pixelSize: 8
            font.weight: Font.DemiBold
            font.letterSpacing: 0.85
        }

        Row {
            y: 15
            spacing: 2

            RollingDigits {
                text: metric.value < 0 ? "--" : String(metric.value)
                color: root.colors.text
                fontFamily: root.uiFont
                fontPixelSize: 24
                fontWeight: Font.DemiBold
                letterSpacing: -0.7
                staggerMs: 10
                reducedMotion: root.reducedMotion
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 4
                visible: metric.value >= 0
                text: "%"
                color: root.colors.secondary
                font.family: root.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }

        Text {
            y: 45
            width: parent.width
            text: metric.detail.length > 0 ? metric.detail : "Unavailable"
            color: root.colors.tertiary
            elide: Text.ElideRight
            font.family: root.uiFont
            font.pixelSize: 8
            font.features: { "tnum": 1 }
        }

        Rectangle {
            y: 63
            width: parent.width
            height: 3
            radius: 1.5
            color: root.colors.raised

            Rectangle {
                width: metric.value < 0 ? 0 : parent.width * metric.value / 100
                height: parent.height
                radius: parent.radius
                color: root.metricColor(metric.value)

                Behavior on width {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.content
                        easing.type: MotionTokens.easeOut
                    }
                }
                Behavior on color {
                    ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state }
                }
            }
        }
    }

    enabled: open
    visible: opacity > 0.001
    opacity: open ? 1 : 0
    scale: open ? 1 : 0.975
    transformOrigin: Item.Top

    Behavior on opacity {
        SequentialAnimation {
            PauseAnimation { duration: root.open && !root.reducedMotion ? 45 : 0 }
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.state
                easing.type: MotionTokens.easeOut
            }
        }
    }

    Behavior on scale {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : MotionTokens.content
            easing.type: MotionTokens.easeOut
        }
    }

    Row {
        x: 24
        y: 17
        spacing: 9

        Image {
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            source: Qt.resolvedUrl("../assets/icons/monitor-cpu-neutral.svg")
            fillMode: Image.PreserveAspectFit
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                text: "System Monitor"
                color: root.colors.text
                font.family: root.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                text: "Updates every second"
                color: root.colors.tertiary
                font.family: root.uiFont
                font.pixelSize: 8
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 17
        y: 13
        spacing: 5

        IslandButton {
            iconOnly: true
            glyph: root.controller.pinned ? "\uE77A" : "\uE718"
            selected: root.controller.pinned
            accessibleName: root.controller.pinned ? "Unpin system monitor"
                                                       : "Keep system monitor open"
            onClicked: root.controller.togglePinned()
        }

        IslandButton {
            iconOnly: true
            glyph: "\uE711"
            accessibleName: "Close system monitor"
            onClicked: {
                root.controller.setPinned(false)
                root.controller.setExpanded(false)
            }
        }
    }

    Row {
        id: metrics
        x: 24
        y: 61
        width: parent.width - 48
        spacing: 12

        Metric {
            width: (metrics.width - metrics.spacing * 2) / 3
            label: "CPU"
            value: root.controller.cpuUsage
            detail: "Total processor activity"
        }
        Metric {
            width: (metrics.width - metrics.spacing * 2) / 3
            label: "Memory"
            value: root.controller.memoryUsage
            detail: root.controller.memoryDetailText
        }
        Metric {
            width: (metrics.width - metrics.spacing * 2) / 3
            label: "Disk"
            value: root.controller.diskUsage
            detail: root.controller.diskDetailText
        }
    }

    Text {
        x: 24
        y: 157
        text: "TOP PROCESSES"
        color: root.colors.secondary
        font.family: root.uiFont
        font.pixelSize: 8
        font.weight: Font.DemiBold
        font.letterSpacing: 0.85
    }

    Text {
        x: parent.width - 142
        y: 157
        width: 48
        text: "CPU"
        color: root.colors.tertiary
        horizontalAlignment: Text.AlignRight
        font.family: root.uiFont
        font.pixelSize: 8
        font.weight: Font.DemiBold
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 24
        y: 157
        width: 70
        text: "MEMORY"
        color: root.colors.tertiary
        horizontalAlignment: Text.AlignRight
        font.family: root.uiFont
        font.pixelSize: 8
        font.weight: Font.DemiBold
    }

    Text {
        x: 24
        y: 196
        visible: root.controller.topProcesses.length === 0
        text: "Collecting process activity…"
        color: root.colors.tertiary
        font.family: root.uiFont
        font.pixelSize: 10
    }

    Repeater {
        model: root.controller.topProcesses

        delegate: Item {
            id: processRow

            required property var modelData
            required property int index

            x: 24
            y: 178 + index * 29
            width: root.width - 48
            height: 29
            Accessible.name: modelData.name + ", CPU " + modelData.cpu
                             + " percent, memory "
                             + modelData.memory
            Accessible.role: Accessible.StaticText

            Text {
                x: 0
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                text: String(processRow.index + 1)
                color: root.colors.tertiary
                font.family: root.monoFont
                font.pixelSize: 8
                font.features: { "tnum": 1 }
            }

            Text {
                x: 22
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 142
                text: processRow.modelData.name
                color: root.colors.text
                elide: Text.ElideRight
                font.family: root.uiFont
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            Text {
                x: parent.width - 118
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                text: processRow.modelData.cpu + "%"
                color: root.colors.secondary
                horizontalAlignment: Text.AlignRight
                font.family: root.monoFont
                font.pixelSize: 9
                font.features: { "tnum": 1 }
            }

            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 70
                text: processRow.modelData.memory
                color: root.colors.tertiary
                horizontalAlignment: Text.AlignRight
                font.family: root.monoFont
                font.pixelSize: 8
                font.features: { "tnum": 1 }
            }

        }
    }
}
