import QtQuick 6.5
import QtQuick.Controls 6.5

ComboBox {
    id: root

    property color accentColor: "#9ad9cc"
    property string uiFont: "Inter"

    implicitWidth: 190
    implicitHeight: 34
    leftPadding: 12
    rightPadding: 32
    activeFocusOnTab: true

    contentItem: Text {
        leftPadding: 0
        rightPadding: 0
        text: root.displayText
        color: "#e7e7e9"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font.family: root.uiFont
        font.pixelSize: 12
    }

    indicator: Text {
        x: root.width - width - 10
        anchors.verticalCenter: parent.verticalCenter
        text: "\uE70D"
        color: "#8c8c94"
        font.family: "Segoe Fluent Icons"
        font.pixelSize: 11
    }

    background: Rectangle {
        radius: 8
        color: root.down ? "#1d1d20" : "#151517"
        border.width: root.activeFocus ? 1 : 0
        border.color: root.accentColor
    }

    delegate: ItemDelegate {
        id: optionDelegate
        required property var modelData
        required property int index
        width: root.width
        height: 34
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            text: optionDelegate.modelData
            color: optionDelegate.highlighted ? "#f4f4f5" : "#b6b6bd"
            verticalAlignment: Text.AlignVCenter
            font.family: root.uiFont
            font.pixelSize: 12
        }
        background: Rectangle {
            color: optionDelegate.highlighted
                   ? Qt.rgba(root.accentColor.r,
                             root.accentColor.g,
                             root.accentColor.b, 0.12)
                   : "transparent"
        }
    }

    popup: Popup {
        y: root.height + 5
        width: root.width
        implicitHeight: contentItem.implicitHeight + 10
        padding: 5

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }
        background: Rectangle {
            radius: 9
            color: "#121214"
            border.width: 1
            border.color: "#29292d"
        }
    }
}
