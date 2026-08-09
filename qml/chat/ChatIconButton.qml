import QtQuick 6.5
import QtQuick.Controls 6.5

Button {
    id: control

    property url iconSource: ""
    property int iconSize: 16
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
    activeFocusOnTab: true
    Accessible.name: accessibleName
    Accessible.role: Accessible.Button
    text: ""
    display: AbstractButton.IconOnly
    icon.source: iconSource
    icon.width: iconSize
    icon.height: iconSize
    icon.color: enabled ? foregroundColor : "#55555b"
    palette.buttonText: enabled ? foregroundColor : "#55555b"
    contentItem.rotation: iconRotation
    contentItem.transformOrigin: Item.Center

    ToolTip {
        id: tooltip
        parent: control
        x: Math.round((control.width - implicitWidth) / 2)
        y: control.height + 6
        visible: control.hovered && control.accessibleName.length > 0
        text: control.accessibleName
        delay: 560
        timeout: 3500
        padding: 7
        leftPadding: 9
        rightPadding: 9
        margins: 6

        contentItem: Text {
            text: tooltip.text
            color: "#e4e4e7"
            font.family: uiFont
            font.pixelSize: 11
            renderType: Text.NativeRendering
        }

        background: Rectangle {
            radius: 6
            color: "#1a1a1e"
            border.width: 1
            border.color: "#303036"
        }
    }

    background: Rectangle {
        radius: control.radius
        color: control.down ? control.pressedColor
                            : (control.hovered ? control.hoverColor : control.baseColor)
        border.width: control.activeFocus ? 1 : 0
        border.color: "#79d8ce"

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
