import QtQuick 6.5

Item {
    id: root

    required property string title
    required property string description
    property string uiFont: "Inter"
    property color accentColor: "#9ad9cc"
    property bool highlighted: false
    property int controlWidth: 230
    default property alias controlData: controlHost.data

    implicitHeight: 78

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: root.highlighted
               ? Qt.rgba(root.accentColor.r,
                         root.accentColor.g,
                         root.accentColor.b, 0.10)
               : "transparent"

        Behavior on color {
            ColorAnimation { duration: 140 }
        }
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.right: controlHost.left
        anchors.rightMargin: 22
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5

        Text {
            width: parent.width
            text: root.title
            color: "#f4f4f5"
            elide: Text.ElideRight
            font.family: root.uiFont
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }

        Text {
            width: parent.width
            text: root.description
            color: "#85858d"
            elide: Text.ElideRight
            font.family: root.uiFont
            font.pixelSize: 11
        }
    }

    Item {
        id: controlHost
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        width: root.controlWidth
        height: Math.max(32, childrenRect.height)
    }
}
