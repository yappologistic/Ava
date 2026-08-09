import QtQuick 6.5
import QtQuick.Controls 6.5

Button {
    id: control

    property string symbol: ""
    property string accessibleName: text
    property color baseColor: "transparent"
    property color hoverColor: "#202024"
    property color pressedColor: "#29292e"
    property color foregroundColor: "#b7b7bd"
    property int radius: 9
    property real iconRotation: 0

    implicitWidth: 34
    implicitHeight: 34
    padding: 0
    hoverEnabled: true
    Accessible.name: accessibleName
    Accessible.role: Accessible.Button

    contentItem: Text {
        text: control.symbol
        color: control.enabled ? control.foregroundColor : "#55555b"
        font.family: "Segoe Fluent Icons"
        font.pixelSize: 14
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        rotation: control.iconRotation
        transformOrigin: Item.Center

        Behavior on color {
            ColorAnimation { duration: reducedMotion ? 0 : 110 }
        }
    }

    background: Rectangle {
        radius: control.radius
        color: control.down ? control.pressedColor
                            : (control.hovered ? control.hoverColor : control.baseColor)
        border.width: control.activeFocus ? 1 : 0
        border.color: "#7aa7ff"

        Behavior on color {
            ColorAnimation { duration: reducedMotion ? 0 : 110 }
        }
    }

    scale: down ? 0.965 : 1
    Behavior on scale {
        NumberAnimation {
            duration: reducedMotion ? 0 : 90
            easing.type: Easing.OutCubic
        }
    }
}
