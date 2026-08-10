import QtQuick 6.5
import Ava 1.0

Item {
    id: root

    property bool active: false
    property bool reducedMotion: false
    property color color: "#f5f5f7"
    property real progress: active ? 1 : 0

    Behavior on progress {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : MotionTokens.content
            easing.type: MotionTokens.easeOut
        }
    }

    Canvas {
        id: glyphCanvas
        anchors.fill: parent
        antialiasing: true

        function mix(from, to) {
            return from + (to - from) * root.progress
        }

        function roundedOutline(context, x, y, width, height, radius, alpha) {
            if (width <= 0 || height <= 0 || alpha <= 0)
                return
            const r = Math.min(radius, width / 2, height / 2)
            context.globalAlpha = alpha
            context.beginPath()
            context.moveTo(x + r, y)
            context.lineTo(x + width - r, y)
            context.quadraticCurveTo(x + width, y, x + width, y + r)
            context.lineTo(x + width, y + height - r)
            context.quadraticCurveTo(x + width, y + height,
                                     x + width - r, y + height)
            context.lineTo(x + r, y + height)
            context.quadraticCurveTo(x, y + height, x, y + height - r)
            context.lineTo(x, y + r)
            context.quadraticCurveTo(x, y, x + r, y)
            context.closePath()
            context.stroke()
        }

        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.scale(width / 16, height / 16)
            context.strokeStyle = root.color
            context.lineWidth = 1.35
            context.lineCap = "round"
            context.lineJoin = "round"

            roundedOutline(context,
                           mix(1.4, 1.25), mix(1.4, 1.6),
                           mix(5.2, 5.1), mix(5.2, 12.8), 1.55, 1)
            roundedOutline(context,
                           mix(9.4, 8.15), mix(1.4, 1.6),
                           mix(5.2, 6.6), mix(5.2, 5.4), 1.55, 1)
            roundedOutline(context,
                           mix(1.4, 8.15), mix(9.4, 9.0),
                           mix(5.2, 6.6), mix(5.2, 5.4), 1.55, 1)
            roundedOutline(context,
                           mix(9.4, 11.0), mix(9.4, 11.0),
                           mix(5.2, 1.8), mix(5.2, 1.8), 1.2,
                           1 - root.progress)
            context.globalAlpha = 1
        }

        Connections {
            target: root
            function onProgressChanged() { glyphCanvas.requestPaint() }
            function onColorChanged() { glyphCanvas.requestPaint() }
            function onWidthChanged() { glyphCanvas.requestPaint() }
            function onHeightChanged() { glyphCanvas.requestPaint() }
        }
    }
}
