import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property alias from: slider.from
    property alias to: slider.to
    property alias stepSize: slider.stepSize
    property alias value: slider.value
    property string unit: ""
    property color accentColor: "#9ad9cc"
    property string uiFont: "Inter"
    property bool reducedMotion: false
    property string accessibleName: ""
    signal moved(real value)

    implicitWidth: 230
    implicitHeight: 32

    Slider {
        id: slider
        anchors.left: parent.left
        anchors.right: valueLabel.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        height: 28
        snapMode: Slider.SnapAlways
        activeFocusOnTab: true
        Accessible.name: root.accessibleName
        onMoved: root.moved(value)

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: "#343438"

            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: root.accentColor

                Behavior on color {
                    ColorAnimation { duration: root.reducedMotion ? 0 : 130 }
                }
            }
        }

        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition
               * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 16
            height: 16
            radius: 8
            color: root.accentColor

            Behavior on color {
                ColorAnimation { duration: root.reducedMotion ? 0 : 130 }
            }
        }
    }

    Text {
        id: valueLabel
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 54
        text: Math.round(slider.value) + root.unit
        color: "#b7b7bd"
        horizontalAlignment: Text.AlignRight
        font.family: root.uiFont
        font.pixelSize: 12
        font.features: { "tnum": 1 }
    }
}
