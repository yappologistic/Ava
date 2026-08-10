pragma ComponentBehavior: Bound

import QtQuick 6.5
import Ava 1.0

Item {
    id: root

    property bool playing: false
    property string kind: "playback"
    property color color: "#f5f5f7"
    property bool reducedMotion: false
    property real morphProgress: playing ? 1 : 0

    implicitWidth: 16
    implicitHeight: 16

    Behavior on morphProgress {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : MotionTokens.directSettle
            easing.type: MotionTokens.easeOut
        }
    }

    onMorphProgressChanged: iconCanvas.requestPaint()
    onColorChanged: iconCanvas.requestPaint()
    onKindChanged: iconCanvas.requestPaint()

    Canvas {
        id: iconCanvas
        anchors.fill: parent
        antialiasing: true

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()

        onPaint: {
            const context = getContext("2d")
            const progress = root.morphProgress
            const centerX = width / 2
            const centerY = height / 2
            context.reset()
            context.fillStyle = root.color

            if (root.kind === "previous" || root.kind === "next") {
                const forward = root.kind === "next"
                const barX = width * (forward ? 0.79 : 0.21)
                context.strokeStyle = root.color
                context.lineWidth = Math.max(1.35, width * 0.105)
                context.lineCap = "round"
                context.beginPath()
                context.moveTo(barX, height * 0.20)
                context.lineTo(barX, height * 0.80)
                context.stroke()

                context.lineJoin = "round"
                context.beginPath()
                if (forward) {
                    context.moveTo(width * 0.31, height * 0.18)
                    context.lineTo(width * 0.69, height * 0.50)
                    context.lineTo(width * 0.31, height * 0.82)
                } else {
                    context.moveTo(width * 0.69, height * 0.18)
                    context.lineTo(width * 0.31, height * 0.50)
                    context.lineTo(width * 0.69, height * 0.82)
                }
                context.closePath()
                context.fill()
                return
            }

            context.save()
            context.globalAlpha = 1 - progress
            const playScale = 1 - progress * 0.12
            context.translate(centerX, centerY)
            context.scale(playScale, playScale)
            context.translate(-centerX, -centerY)
            context.beginPath()
            context.moveTo(width * 0.28, height * 0.18)
            context.lineTo(width * 0.76, height * 0.50)
            context.lineTo(width * 0.28, height * 0.82)
            context.closePath()
            context.fill()
            context.restore()

            context.save()
            context.globalAlpha = progress
            const pauseScale = 0.86 + progress * 0.14
            context.translate(centerX, centerY)
            context.scale(pauseScale, pauseScale)
            context.translate(-centerX, -centerY)
            context.fillRect(width * 0.25, height * 0.18, width * 0.16, height * 0.64)
            context.fillRect(width * 0.59, height * 0.18, width * 0.16, height * 0.64)
            context.restore()
        }
    }
}
