pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property var colors
    property string uiFont: "Inter"
    property string iconFont: "Segoe Fluent Icons"
    property bool reducedMotion: false
    property int selectedMinutes: 15
    property real visualTimerProgress: controller.timerProgress
    property real cancelProgressOverride: -1
    property real timerLaunchProgress: 0
    readonly property bool timerHandoffActive: startCommitAnimation.running
    property int lastObservedRemaining: controller.timerRemainingSeconds
    readonly property real effectiveTimerProgress: cancelProgressOverride >= 0
                                                   ? cancelProgressOverride
                                                   : visualTimerProgress
    readonly property color timerOrange: "#ff9f0a"
    readonly property color timerOrangePressed: "#e98700"
    readonly property int tickSpacing: 13
    readonly property int maximumMinutes: 60
    readonly property bool setupActive: !controller.timerActive && !controller.timerRinging
    readonly property bool runningActive: controller.timerActive
    readonly property bool ringingActive: controller.timerRinging
    readonly property real handoffProgress: Math.max(timerLaunchProgress,
                                                       runningActive ? 1 : 0)

    Behavior on visualTimerProgress {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : 110
            easing.type: Easing.OutCubic
        }
    }

    function snapRuler() {
        const minute = Math.max(1, Math.min(maximumMinutes,
                                            Math.round(ruler.contentX / tickSpacing)))
        selectedMinutes = minute
        snapAnimation.stop()
        snapAnimation.from = ruler.contentX
        snapAnimation.to = minute * tickSpacing
        snapAnimation.start()
    }

    Connections {
        target: controller
        function onTimerChanged() {
            const remaining = controller.timerRemainingSeconds
            if (controller.timerActive && remaining <= 10 && remaining > 0
                    && remaining !== root.lastObservedRemaining
                    && !root.reducedMotion)
                finalSecondSettle.restart()
            root.lastObservedRemaining = remaining
        }
    }

    Item {
        id: setupView
        anchors.fill: parent
        enabled: root.setupActive
        visible: opacity > 0.001
        opacity: root.setupActive ? 1
                                  : (root.timerHandoffActive
                                     ? 1 - root.timerLaunchProgress : 0)
        scale: root.setupActive ? 1 : 0.97

        Behavior on opacity {
            enabled: !root.timerHandoffActive
            NumberAnimation { duration: root.reducedMotion ? 0 : 150; easing.type: Easing.OutCubic }
        }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 210; easing.type: Easing.OutCubic } }

        Flickable {
            id: ruler
            x: 20
            y: 4
            width: parent.width - 40
            height: 63
            clip: true
            contentWidth: width + root.maximumMinutes * root.tickSpacing
            contentHeight: height
            contentX: root.selectedMinutes * root.tickSpacing
            boundsBehavior: Flickable.StopAtBounds
            flickDeceleration: 5200
            maximumFlickVelocity: 1700
            interactive: true
            opacity: 1 - root.timerLaunchProgress
            scale: 1 - root.timerLaunchProgress * 0.12
            transformOrigin: Item.Bottom
            transform: Translate { y: root.timerLaunchProgress * 7 }

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
                        x: ruler.width / 2 + index * root.tickSpacing - width / 2
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
                            height: minuteMark.distance < 0.6
                                    ? 38 : (minuteMark.index % 5 === 0 ? 34 : 29)
                            radius: width / 2
                            color: root.timerOrange
                            opacity: Math.max(0.10, 1 - minuteMark.distance / 20)
                            scale: 1 + Math.max(0, 1 - minuteMark.distance) * 0.08

                            Behavior on width { NumberAnimation { duration: root.reducedMotion ? 0 : 90 } }
                            Behavior on height {
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
                    }
                }
            }
        }

        Item {
            id: selectionIndicator
            anchors.horizontalCenter: ruler.horizontalCenter
            y: (ruler.moving || snapAnimation.running ? 65 : 64)
               + root.timerLaunchProgress * 8
            width: 14
            height: ruler.moving || snapAnimation.running ? 12 : 10
            opacity: 1 - root.handoffProgress
            scale: (ruler.moving || snapAnimation.running ? 1.10 : 1)
                   * (1 - root.timerLaunchProgress * 0.12)
            transform: Translate {
                x: -44 * root.timerLaunchProgress
                y: -21 * root.timerLaunchProgress
            }

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
            Behavior on height {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.hover
                    easing.type: MotionTokens.easeOut
                }
            }

            Canvas {
                id: selectionCanvas
                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
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
            width: 128
            height: 39
            radius: height / 2
            color: startTap.pressed ? "#492b0b" : (startHover.hovered ? "#3d250c" : "#311e0a")
            opacity: 1 - root.timerLaunchProgress
            scale: (startTap.pressed ? 0.965 : (startHover.hovered ? 1.018 : 1))
                   * (1 - root.timerLaunchProgress * 0.04)
            Accessible.name: "Start " + root.selectedMinutes + " minute timer"
            Accessible.role: Accessible.Button

            Behavior on color { ColorAnimation { duration: 110 } }
            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

            HoverHandler { id: startHover }
            TapHandler {
                id: startTap
                enabled: !startCommitAnimation.running
                onTapped: startCommitAnimation.restart()
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

        RollingDigits {
            id: selectedTimeText
            anchors.right: parent.right
            anchors.rightMargin: 28
            y: 67
            text: root.selectedMinutes + ":00"
            color: root.timerOrange
            fontFamily: root.uiFont
            fontPixelSize: 37
            fontWeight: Font.Light
            letterSpacing: -1.5
            reducedMotion: root.reducedMotion
            rollDirection: 1
            opacity: 1 - Math.max(0, root.timerLaunchProgress - 0.58) / 0.42
            scale: 1 - root.timerLaunchProgress * 0.38
            transformOrigin: Item.Center
            transform: Translate {
                id: selectedTimeShift
                x: ((root.width / 2 + 40)
                    - (root.width - 28 - selectedTimeText.width / 2))
                   * root.timerLaunchProgress
                y: -41 * root.timerLaunchProgress
            }
        }

        SequentialAnimation {
            id: startCommitAnimation
            NumberAnimation {
                target: root
                property: "timerLaunchProgress"
                from: 0
                to: 0.48
                duration: root.reducedMotion ? 0 : 105
                easing.type: MotionTokens.easeInOut
            }
            ScriptAction {
                script: {
                    controller.startTimer(root.selectedMinutes * 60)
                    collapseDelay.restart()
                    launchReset.restart()
                }
            }
            NumberAnimation {
                target: root
                property: "timerLaunchProgress"
                to: 1
                duration: root.reducedMotion ? 0 : MotionTokens.directSettle
                easing.type: MotionTokens.easeOut
            }
        }

        Timer {
            id: launchReset
            interval: root.reducedMotion ? 0 : 420
            repeat: false
            onTriggered: root.timerLaunchProgress = 0
        }
    }

    Item {
        id: runningView
        anchors.fill: parent
        enabled: root.runningActive
        visible: opacity > 0.001
        opacity: root.runningActive
                 ? (root.timerHandoffActive
                    ? Math.max(0, Math.min(1,
                        (root.timerLaunchProgress - 0.42) / 0.58)) : 1)
                 : 0
        scale: root.runningActive
               ? (root.timerHandoffActive
                  ? 0.96 + 0.04 * Math.max(0, Math.min(1,
                      (root.timerLaunchProgress - 0.42) / 0.58)) : 1)
               : 0.97

        Behavior on opacity {
            enabled: !root.timerHandoffActive
            NumberAnimation { duration: root.reducedMotion ? 0 : 170; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            enabled: !root.timerHandoffActive
            NumberAnimation { duration: root.reducedMotion ? 0 : 220; easing.type: Easing.OutCubic }
        }

        Row {
            id: runningHeader
            anchors.horizontalCenter: parent.horizontalCenter
            y: 14
            spacing: 17
            opacity: runningView.opacity
            scale: 0.94 + runningView.opacity * 0.06
            transform: Translate { y: 4 * (1 - runningView.opacity) }

            Item {
                width: 58
                height: 60

                Item {
                    y: 2
                    width: 58
                    height: 58

                    Canvas {
                        id: progressCanvas
                        anchors.fill: parent

                        Connections {
                            target: root
                            function onVisualTimerProgressChanged() { progressCanvas.requestPaint() }
                            function onCancelProgressOverrideChanged() { progressCanvas.requestPaint() }
                        }

                        onPaint: {
                            const context = getContext("2d")
                            context.reset()
                            context.lineCap = "round"
                            context.lineWidth = controller.timerRemainingSeconds <= 10
                                                && controller.timerRemainingSeconds > 0 ? 5 : 4
                            context.strokeStyle = "#2a210f"
                            context.beginPath()
                            context.arc(width / 2, height / 2, 24, 0, Math.PI * 2)
                            context.stroke()
                            context.strokeStyle = controller.timerRemainingSeconds <= 10
                                                  && controller.timerRemainingSeconds > 0
                                                  ? "#ffb43b" : root.timerOrange
                            context.beginPath()
                            context.arc(width / 2, height / 2, 24, -Math.PI / 2,
                                        -Math.PI / 2 + Math.PI * 2 * root.effectiveTimerProgress)
                            context.stroke()
                        }
                        opacity: controller.timerPaused ? 0.58 : 1
                        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
                    }

                    Item {
                        id: timerGlyphHost
                        anchors.centerIn: parent
                        width: 20
                        height: 20
                        Image {
                            anchors.fill: parent
                            source: Qt.resolvedUrl("../assets/icons/timer-orange.svg")
                            sourceSize.width: 40
                            sourceSize.height: 40
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }
                    }
                }
            }

            Item {
                width: countdownText.implicitWidth
                height: 60

                RollingDigits {
                    id: countdownText
                    y: 12
                    text: controller.timerRemainingText
                    color: "#f5f5f7"
                    fontFamily: root.uiFont
                    fontPixelSize: 35
                    fontWeight: Font.Light
                    letterSpacing: -1.5
                    reducedMotion: root.reducedMotion
                    rollDirection: -1
                }
            }
        }

        SequentialAnimation {
            id: finalSecondSettle
            NumberAnimation { target: countdownText; property: "scale"; from: 1; to: 1.025; duration: MotionTokens.press; easing.type: MotionTokens.easeOut }
            NumberAnimation { target: countdownText; property: "scale"; to: 1; duration: MotionTokens.hover; easing.type: MotionTokens.easeOut }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
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
                    enabled: !cancelTimerAnimation.running
                    onTapped: cancelTimerAnimation.restart()
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
                id: addMinuteButton
                width: 80
                height: 39
                radius: 20
                color: addTap.pressed ? "#353535" : (addHover.hovered ? "#2b2b2d" : "#222224")
                scale: addTap.pressed ? 0.96 : 1
                Accessible.name: "Add one minute"
                Accessible.role: Accessible.Button
                HoverHandler { id: addHover }
                TapHandler {
                    id: addTap
                    onTapped: {
                        controller.addTimerMinute()
                        if (!root.reducedMotion)
                            addMinuteFeedback.restart()
                    }
                }
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

                Rectangle {
                    id: addMinuteBadge
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: -20
                    width: 44
                    height: 18
                    radius: 9
                    color: "#3a260c"
                    opacity: 0
                    scale: 0.88
                    Text {
                        anchors.centerIn: parent
                        text: "+1:00"
                        color: root.timerOrange
                        font.family: root.uiFont
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        font.features: { "tnum": 1 }
                    }
                }

                SequentialAnimation {
                    id: addMinuteFeedback
                    ParallelAnimation {
                        NumberAnimation { target: addMinuteBadge; property: "opacity"; from: 0; to: 1; duration: MotionTokens.press }
                        NumberAnimation { target: addMinuteBadge; property: "scale"; from: 0.88; to: 1; duration: MotionTokens.hover; easing.type: MotionTokens.settle }
                        NumberAnimation { target: addMinuteBadge; property: "y"; from: -12; to: -22; duration: MotionTokens.content; easing.type: MotionTokens.easeOut }
                    }
                    PauseAnimation { duration: 250 }
                    ParallelAnimation {
                        NumberAnimation { target: addMinuteBadge; property: "opacity"; to: 0; duration: MotionTokens.state }
                        NumberAnimation { target: addMinuteBadge; property: "y"; to: -28; duration: MotionTokens.state; easing.type: Easing.InCubic }
                    }
                }
            }

            Rectangle {
                width: 80
                height: 39
                radius: 20
                color: pauseTap.pressed ? root.timerOrangePressed : root.timerOrange
                scale: pauseTap.pressed ? 0.94 : (pauseHover.hovered ? 1.018 : 1)
                Accessible.name: controller.timerPaused ? "Resume timer" : "Pause timer"
                Accessible.role: Accessible.Button
                HoverHandler { id: pauseHover }
                TapHandler {
                    id: pauseTap
                    onTapped: {
                        controller.toggleTimerPaused()
                        if (!root.reducedMotion)
                            timerPauseRelease.restart()
                    }
                }
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

        SequentialAnimation {
            id: timerPauseRelease
            NumberAnimation {
                target: timerGlyphHost
                property: "scale"
                from: 0.9
                to: 1.08
                duration: MotionTokens.press
                easing.type: MotionTokens.easeOut
            }
            NumberAnimation {
                target: timerGlyphHost
                property: "scale"
                to: 1
                duration: MotionTokens.directSettle
                easing.type: MotionTokens.settle
            }
        }

        SequentialAnimation {
            id: cancelTimerAnimation
            ScriptAction { script: root.cancelProgressOverride = root.visualTimerProgress }
            ParallelAnimation {
                NumberAnimation { target: root; property: "cancelProgressOverride"; to: 0; duration: root.reducedMotion ? 0 : MotionTokens.state; easing.type: Easing.InCubic }
                NumberAnimation { target: countdownText; property: "opacity"; to: 0; duration: root.reducedMotion ? 0 : MotionTokens.state; easing.type: Easing.InCubic }
                NumberAnimation { target: countdownText; property: "scale"; to: 0.88; duration: root.reducedMotion ? 0 : MotionTokens.state; easing.type: Easing.InCubic }
                NumberAnimation { target: timerGlyphHost; property: "scale"; to: 0.78; duration: root.reducedMotion ? 0 : MotionTokens.state; easing.type: Easing.InCubic }
            }
            ScriptAction {
                script: {
                    controller.cancelTimer()
                    controller.closeTimer()
                    controller.setExpanded(false)
                    root.cancelProgressOverride = -1
                    countdownText.opacity = 1
                    countdownText.scale = 1
                    timerGlyphHost.scale = 1
                }
            }
        }
    }

    Item {
        id: ringingView
        anchors.fill: parent
        enabled: root.ringingActive
        visible: opacity > 0.001
        opacity: root.ringingActive ? 1 : 0
        scale: root.ringingActive ? 1 : 0.94

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
                loops: 2
                NumberAnimation { to: 1.07; duration: 520; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: 520; easing.type: Easing.InOutSine }
            }

            Image {
                anchors.centerIn: parent
                width: 22
                height: 22
                source: Qt.resolvedUrl("../assets/icons/timer-dark.svg")
                sourceSize.width: 44
                sourceSize.height: 44
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
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
            scale: stopTap.pressed ? 0.94 : (stopHover.hovered ? 1.018 : 1)
            Accessible.name: "Stop timer alert"
            Accessible.role: Accessible.Button
            HoverHandler { id: stopHover }
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
