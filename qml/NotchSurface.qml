import QtQuick 6.5
import QtQuick.Shapes 6.5
import Ava 1.0

Item {
    id: root

    property color surfaceColor: "#000000"
    property bool glassEnabled: false
    property var glassBackdrop: null
    property bool reducedMotion: false
    property point pointerPosition: Qt.point(width * 0.28, -height * 0.18)
    property bool pointerActive: false
    property real morphProgress: 0
    property color rimColor: "#52ffffff"
    property color highlightColor: "#28ffffff"
    property color shadowColor: "#52000000"
    property color coolEdgeColor: "#365ac8fa"
    property color warmEdgeColor: "#24ff7ac8"
    property bool pillMode: false
    property real pillRadius: height / 2
    property real bottomRadius: 24
    property real earWidth: 16
    property real earDepth: 20

    // Exact cubic approximation of a circular quarter arc. The previous longer
    // tangents produced a pinched, almost-square transition at the lower edges.
    readonly property real roundKappa: 0.5522847498
    readonly property real earKappa: 0.54
    Rectangle {
        id: pillSurface
        anchors.fill: parent
        visible: root.pillMode
        color: root.surfaceColor
        radius: Math.min(root.pillRadius, height / 2, width / 2)
        antialiasing: true

        LiquidGlassTexture {
            id: pillTextureSource
            anchors.fill: parent
            backdrop: root.glassBackdrop
            enabled: true
            opacity: 1
            visible: root.pillMode && root.glassBackdrop
                     && root.glassBackdrop.frameAvailable
        }

    }

    Shape {
        id: notchSurface
        anchors.fill: parent
        visible: !root.pillMode
        antialiasing: true
        layer.enabled: root.glassBackdrop && root.glassBackdrop.frameAvailable

        ShapePath {
            fillColor: root.surfaceColor
            strokeColor: "transparent"
            strokeWidth: 0
            startX: 0
            startY: 0

            // Top edge and right reverse-curve "ear".
            PathLine { x: root.width; y: 0 }
            PathCubic {
                x: root.width - root.earWidth
                y: root.earDepth
                control1X: root.width - root.earWidth * root.earKappa
                control1Y: 0
                control2X: root.width - root.earWidth
                control2Y: root.earDepth * (1 - root.earKappa)
            }

            // Right side into a continuous lower corner.
            PathLine {
                x: root.width - root.earWidth
                y: root.height - root.bottomRadius
            }
            PathCubic {
                x: root.width - root.earWidth - root.bottomRadius
                y: root.height
                control1X: root.width - root.earWidth
                control1Y: root.height - root.bottomRadius
                           + root.roundKappa * root.bottomRadius
                control2X: root.width - root.earWidth - root.bottomRadius
                           + root.roundKappa * root.bottomRadius
                control2Y: root.height
            }

            // Bottom edge and left continuous corner.
            PathLine {
                x: root.earWidth + root.bottomRadius
                y: root.height
            }
            PathCubic {
                x: root.earWidth
                y: root.height - root.bottomRadius
                control1X: root.earWidth + root.bottomRadius
                           - root.roundKappa * root.bottomRadius
                control1Y: root.height
                control2X: root.earWidth
                control2Y: root.height - root.bottomRadius
                           + root.roundKappa * root.bottomRadius
            }

            // Left side and matching reverse-curve ear back to the top edge.
            PathLine { x: root.earWidth; y: root.earDepth }
            PathCubic {
                x: 0
                y: 0
                control1X: root.earWidth
                control1Y: root.earDepth * (1 - root.earKappa)
                control2X: root.earWidth * root.earKappa
                control2Y: 0
            }
        }
    }

    LiquidGlassTexture {
        id: notchTextureSource
        anchors.fill: parent
        backdrop: root.glassBackdrop
        enabled: true
        opacity: 1
        visible: !root.pillMode && root.glassBackdrop
                 && root.glassBackdrop.frameAvailable
    }

}
