import QtQuick 6.5
import QtQuick.Controls 6.5

Switch {
    id: root

    property color accentColor: "#9ad9cc"
    property bool reducedMotion: false

    implicitWidth: 46
    implicitHeight: 28
    padding: 0
    spacing: 0
    text: ""
    activeFocusOnTab: true

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: root.leftPadding
        y: (root.height - height) / 2
        radius: height / 2
        color: root.checked ? root.accentColor : "#343438"

        Behavior on color {
            ColorAnimation { duration: root.reducedMotion ? 0 : 130 }
        }

        Rectangle {
            width: 18
            height: 18
            x: root.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            radius: width / 2
            color: root.checked ? "#07110f" : "#e7e7e9"

            Behavior on x {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : 145
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    contentItem: Item {}
}
