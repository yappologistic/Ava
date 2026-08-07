import QtQuick 6.5
import QtQuick.Shapes 6.5

Item {
    id: root

    property color surfaceColor: "#000000"
    property real bottomRadius: 24
    property real earWidth: 16
    property real earDepth: 20

    // Exact cubic approximation of a circular quarter arc. The previous longer
    // tangents produced a pinched, almost-square transition at the lower edges.
    readonly property real roundKappa: 0.5522847498
    readonly property real earKappa: 0.54

    Shape {
        anchors.fill: parent
        antialiasing: true

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
}
