import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property string value: "notch"
    property color accentColor: "#9ad9cc"
    property string uiFont: "Inter"
    signal selected(string value)

    implicitWidth: 190
    implicitHeight: 34

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: "#151517"
    }

    Row {
        anchors.fill: parent
        spacing: 0

        Repeater {
            model: [
                { value: "notch", label: "Notch" },
                { value: "pill", label: "Pill" }
            ]

            Button {
                id: optionButton
                required property var modelData
                width: root.width / 2
                height: root.height
                flat: true
                text: modelData.label
                activeFocusOnTab: true
                Accessible.name: modelData.label + " island shape"
                onClicked: root.selected(modelData.value)

                contentItem: Text {
                    text: optionButton.text
                    color: root.value === optionButton.modelData.value
                           ? "#f4f4f5" : "#888890"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: root.uiFont
                    font.pixelSize: 11
                    font.weight: root.value === optionButton.modelData.value
                                 ? Font.DemiBold : Font.Normal
                }

                background: Rectangle {
                    anchors.fill: parent
                    anchors.margins: 3
                    radius: 6
                    color: root.value === optionButton.modelData.value
                           ? "#27272a" : "transparent"
                    border.width: root.value === optionButton.modelData.value ? 1 : 0
                    border.color: Qt.rgba(root.accentColor.r,
                                          root.accentColor.g,
                                          root.accentColor.b, 0.34)
                }
            }
        }
    }
}
