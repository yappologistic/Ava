import QtQuick 6.5
import QtQuick.Controls 6.5

Button {
    id: control

    property color baseColor: "transparent"
    property color hoverColor: "#202024"
    property color pressedColor: "#2a2a2f"
    property color foregroundColor: "#c7c7cd"
    property color disabledForegroundColor: "#57575f"
    property int cornerRadius: 8

    implicitWidth: Math.max(58, label.implicitWidth + leftPadding + rightPadding)
    implicitHeight: 32
    leftPadding: 11
    rightPadding: 11
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    Accessible.role: Accessible.Button

    contentItem: Text {
        id: label
        text: control.text
        color: control.enabled ? control.foregroundColor
                               : control.disabledForegroundColor
        font.family: uiFont
        font.pixelSize: 10
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight

        Behavior on color {
            ColorAnimation { duration: reducedMotion ? 0 : 100 }
        }
    }

    background: Rectangle {
        radius: control.cornerRadius
        color: !control.enabled ? "transparent"
              : control.down ? control.pressedColor
              : control.hovered ? control.hoverColor : control.baseColor

        Behavior on color {
            ColorAnimation { duration: reducedMotion ? 0 : 100 }
        }
    }

    scale: down ? 0.975 : 1
    Behavior on scale {
        NumberAnimation {
            duration: reducedMotion ? 0 : 85
            easing.type: Easing.OutCubic
        }
    }
}
