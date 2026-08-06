pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property var colors
    property string uiFont: "Segoe UI Variable"
    property string iconFont: "Segoe Fluent Icons"
    property bool reducedMotion: false
    property int selectedMinutes: 15
    readonly property color timerOrange: "#ff9f0a"
    readonly property color timerOrangePressed: "#e98700"
    readonly property int tickSpacing: 17
    readonly property int maximumMinutes: 60

    function snapRuler() {
        const minute = Math.max(1, Math.min(maximumMinutes,
                                            Math.round(ruler.contentX / tickSpacing)))
        selectedMinutes = minute
        snapAnimation.stop()
        snapAnimation.from = ruler.contentX
        snapAnimation.to = minute * tickSpacing
        snapAnimation.start()
    }

    Item {
        id: setupView
        anchors.fill: parent
        visible: !controller.timerActive && !controller.timerRinging
        opacity: visible ? 1 : 0
        scale: visible ? 1 : 0.97

        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 150; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 210; easing.type: Easing.OutCubic } }

        Flickable {
            id: ruler
            x: 20
            y: 4
            // Leave a quiet lane for the close control without shortening the
            // visible 0–30 minute span around the default 15-minute selection.
            width: parent.width - 68
            height: 63
            clip: true
            contentWidth: width + root.maximumMinutes * root.tickSpacing
            contentHeight: height
            contentX: root.selectedMinutes * root.tickSpacing
            boundsBehavior: Flickable.StopAtBounds
            flickDeceleration: 5200
            maximumFlickVelocity: 1700
            interactive: true

            onContentXChanged: {
                if (!snapAnimation.running) {
                    root.selectedMinutes = Math.max(1, Math.min(root.maximumMinutes,
                        Math.round(contentX / root.tickSpacing)))
                }
            }
            onMovementEnded: root.snapRuler()
            Component.onCompleted: contentX = root.selectedMinutes * root.tickSpacing

            NumberAnimation {
                id: snapAnimation
                target: ruler
                property: "contentX"
                duration: root.reducedMotion ? 0 : 180
                easing.type: Easing.OutCubic
            }

            Item {
                width: ruler.contentWidth
                height: ruler.height

                Repeater {
                    model: root.maximumMinutes + 1

                    Item {
                        id: minuteMark
                        required property int index
                        readonly property real distance: Math.abs(index - ruler.contentX / root.tickSpacing)
                        x: ruler.width / 2 + index * root.tickSpacing - 8
                        width: 16
                        height: ruler.height

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 0
                            visible: minuteMark.index % 5 === 0
                            text: minuteMark.index
                            color: root.timerOrange
                            opacity: Math.max(0.12, 1 - minuteMark.distance / 17)
                            font.family: root.uiFont
                            font.pixelSize: 10
                            font.weight: minuteMark.distance < 0.55 ? Font.DemiBold : Font.Medium
                            font.features: { "tnum": 1 }
                        }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 22
                            width: minuteMark.distance < 0.6 ? 3 : 2
                            height: minuteMark.index % 5 === 0 ? 34 : 29
                            radius: width / 2
                            color: root.timerOrange
                            opacity: Math.max(0.10, 1 - minuteMark.distance / 20)

                            Behavior on width { NumberAnimation { duration: root.reducedMotion ? 0 : 90 } }
                        }
                    }
                }
            }
        }

        Item {
            anchors.horizontalCenter: ruler.horizontalCenter
            y: 60
            width: 14
            height: 10

            Canvas {
                anchors.fill: parent
                onPaint: {
                    const context = getContext("2d")
                    context.reset()
                    context.fillStyle = root.timerOrange
                    context.beginPath()
                    context.moveTo(width / 2, 0)
                    context.lineTo(width, height)
                    context.lineTo(0, height)
                    context.closePath()
                    context.fill()
                }
            }
        }

        Rectangle {
            id: startButton
            x: 24
            y: 77
            width: 136
            height: 42
            radius: 21
            color: startTap.pressed ? "#492b0b" : (startHover.hovered ? "#3d250c" : "#311e0a")
            scale: startTap.pressed ? 0.965 : (startHover.hovered ? 1.018 : 1)
            Accessible.name: "Start " + root.selectedMinutes + " minute timer"
            Accessible.role: Accessible.Button

            Behavior on color { ColorAnimation { duration: 110 } }
            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

            HoverHandler { id: startHover }
            TapHandler {
                id: startTap
                onTapped: {
                    controller.startTimer(root.selectedMinutes * 60)
                    collapseDelay.restart()
                }
            }

            Text {
                anchors.centerIn: parent
                text: "Start Timer"
                color: root.timerOrange
                font.family: root.uiFont
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 28
            y: 67
            text: root.selectedMinutes + ":00"
            color: root.timerOrange
            font.family: root.uiFont
            font.pixelSize: 39
            font.weight: Font.Light
            font.letterSpacing: -1.7
            font.features: { "tnum": 1 }
        }
    }

    Item {
        id: runningView
        anchors.fill: parent
        visible: controller.timerActive
        opacity: visible ? 1 : 0
        scale: visible ? 1 : 0.97

        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 170; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 220; easing.type: Easing.OutCubic } }

        Item {
            x: 24
            y: 16
            width: 58
            height: 58

            Canvas {
                id: progressCanvas
                anchors.fill: parent

                Connections {
                    target: controller
                    function onTimerChanged() { progressCanvas.requestPaint() }
                }

                onPaint: {
                    const context = getContext("2d")
                    context.reset()
                    context.lineCap = "round"
                    context.lineWidth = 4
                    context.strokeStyle = "#2a210f"
                    context.beginPath()
                    context.arc(width / 2, height / 2, 24, 0, Math.PI * 2)
                    context.stroke()
                    context.strokeStyle = root.timerOrange
                    context.beginPath()
                    context.arc(width / 2, height / 2, 24, -Math.PI / 2,
                                -Math.PI / 2 + Math.PI * 2 * controller.timerProgress)
                    context.stroke()
                }
            }

            Text {
                anchors.centerIn: parent
                text: "\uE121"
                color: root.timerOrange
                font.family: root.iconFont
                font.pixelSize: 19
            }
        }

        Column {
            x: 99
            y: 14
            spacing: 1

            Text {
                text: controller.timerPaused ? "TIMER PAUSED" : "TIMER"
                color: root.timerOrange
                font.family: root.uiFont
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1
            }
            Text {
                id: countdownText
                text: controller.timerRemainingText
                color: "#f5f5f7"
                font.family: root.uiFont
                font.pixelSize: 35
                font.weight: Font.Light
                font.letterSpacing: -1.5
                font.features: { "tnum": 1 }
                transformOrigin: Item.Left

                onTextChanged: countdownPulse.restart()
                SequentialAnimation {
                    id: countdownPulse
                    NumberAnimation { target: countdownText; property: "scale"; to: 0.985; duration: root.reducedMotion ? 0 : 45; easing.type: Easing.OutQuad }
                    NumberAnimation { target: countdownText; property: "scale"; to: 1; duration: root.reducedMotion ? 0 : 105; easing.type: Easing.OutCubic }
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 20
            y: 78
            spacing: 8

            Rectangle {
                width: 76
                height: 39
                radius: 20
                color: cancelTap.pressed ? "#353535" : (cancelHover.hovered ? "#2b2b2d" : "#222224")
                scale: cancelTap.pressed ? 0.96 : 1
                Accessible.name: "Cancel timer"
                Accessible.role: Accessible.Button
                HoverHandler { id: cancelHover }
                TapHandler {
                    id: cancelTap
                    onTapped: {
                        controller.cancelTimer()
                        controller.closeTimer()
                        controller.setExpanded(false)
                    }
                }
                Text {
                    anchors.centerIn: parent
                    text: "Cancel"
                    color: "#f5f5f7"
                    font.family: root.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Behavior on color { ColorAnimation { duration: 100 } }
                Behavior on scale { NumberAnimation { duration: 100 } }
            }

            Rectangle {
                width: 80
                height: 39
                radius: 20
                color: addTap.pressed ? "#353535" : (addHover.hovered ? "#2b2b2d" : "#222224")
                scale: addTap.pressed ? 0.96 : 1
                Accessible.name: "Add one minute"
                Accessible.role: Accessible.Button
                HoverHandler { id: addHover }
                TapHandler { id: addTap; onTapped: controller.addTimerMinute() }
                Text {
                    anchors.centerIn: parent
                    text: "+1 min"
                    color: "#f5f5f7"
                    font.family: root.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Behavior on color { ColorAnimation { duration: 100 } }
                Behavior on scale { NumberAnimation { duration: 100 } }
            }

            Rectangle {
                width: 80
                height: 39
                radius: 20
                color: pauseTap.pressed ? root.timerOrangePressed : root.timerOrange
                scale: pauseTap.pressed ? 0.96 : 1
                Accessible.name: controller.timerPaused ? "Resume timer" : "Pause timer"
                Accessible.role: Accessible.Button
                TapHandler { id: pauseTap; onTapped: controller.toggleTimerPaused() }
                Text {
                    anchors.centerIn: parent
                    text: controller.timerPaused ? "Resume" : "Pause"
                    color: "#000000"
                    font.family: root.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Behavior on color { ColorAnimation { duration: 100 } }
                Behavior on scale { NumberAnimation { duration: 100 } }
            }
        }
    }

    Item {
        id: ringingView
        anchors.fill: parent
        visible: controller.timerRinging
        opacity: visible ? 1 : 0
        scale: visible ? 1 : 0.94

        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 240; easing.type: Easing.OutBack } }

        Rectangle {
            id: ringingOrb
            x: 24
            anchors.verticalCenter: parent.verticalCenter
            width: 56
            height: 56
            radius: 28
            color: root.timerOrange

            SequentialAnimation on scale {
                running: ringingView.visible && !root.reducedMotion
                loops: Animation.Infinite
                NumberAnimation { to: 1.07; duration: 520; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: 520; easing.type: Easing.InOutSine }
            }

            Text {
                anchors.centerIn: parent
                text: "\uE121"
                color: "#000000"
                font.family: root.iconFont
                font.pixelSize: 21
            }
        }

        Column {
            x: 99
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Text {
                text: "TIMER"
                color: root.timerOrange
                font.family: root.uiFont
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1
            }
            Text {
                text: "Time's up"
                color: "#f5f5f7"
                font.family: root.uiFont
                font.pixelSize: 30
                font.weight: Font.DemiBold
                font.letterSpacing: -1
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            width: 104
            height: 44
            radius: 22
            color: stopTap.pressed ? root.timerOrangePressed : root.timerOrange
            scale: stopTap.pressed ? 0.96 : 1
            Accessible.name: "Stop timer alert"
            Accessible.role: Accessible.Button
            TapHandler {
                id: stopTap
                onTapped: {
                    controller.dismissTimer()
                    controller.setExpanded(false)
                }
            }
            Text {
                anchors.centerIn: parent
                text: "Stop"
                color: "#000000"
                font.family: root.uiFont
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Behavior on scale { NumberAnimation { duration: 100 } }
        }
    }

    IslandButton {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 9
        width: 24
        height: 24
        iconOnly: true
        bare: true
        glyph: "\uE711"
        accessibleName: controller.timerActive ? "Collapse timer" : "Close timer"
        visible: !controller.timerRinging
        onClicked: {
            if (controller.timerActive) {
                controller.setExpanded(false)
            } else {
                controller.closeTimer()
            }
        }
    }

    Timer {
        id: collapseDelay
        interval: root.reducedMotion ? 80 : 430
        repeat: false
        onTriggered: controller.setExpanded(false)
    }
}
