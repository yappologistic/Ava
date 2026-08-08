import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property string code: ""
    property string highlightedHtml: ""
    property string language: "Code"

    implicitHeight: 46 + codeViewport.height + 14

    Rectangle {
        anchors.fill: parent
        radius: 12
        color: "#111114"
        border.width: 0
    }

    Item {
        id: header
        x: 16
        y: 6
        width: parent.width - 26
        height: 34

        Text {
            id: codeMark
            anchors.verticalCenter: parent.verticalCenter
            text: "</>"
            color: "#68aafc"
            font.family: monoFont
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }

        Text {
            anchors.left: codeMark.right
            anchors.leftMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            text: root.language
            color: "#777780"
            font.family: uiFont
            font.pixelSize: 9
            font.weight: Font.DemiBold
        }

        Text {
            id: copiedLabel
            anchors.right: copyButton.left
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            text: "Copied"
            color: "#7dafa9"
            opacity: 0
            font.family: uiFont
            font.pixelSize: 9

            Behavior on opacity {
                NumberAnimation { duration: reducedMotion ? 0 : 110 }
            }
        }

        ChatIconButton {
            id: copyButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 30
            height: 30
            radius: 8
            symbol: "\uE8C8"
            accessibleName: "Copy code"
            baseColor: "transparent"
            hoverColor: "#222226"
            pressedColor: "#2b2b30"
            foregroundColor: "#96969e"
            onClicked: {
                chatTextStyler.copyText(root.code)
                copiedLabel.opacity = 1
                copiedTimer.restart()
            }
        }

        Timer {
            id: copiedTimer
            interval: 1300
            onTriggered: copiedLabel.opacity = 0
        }
    }

    Flickable {
        id: codeViewport
        x: 16
        y: 42
        width: parent.width - 32
        height: Math.min(320, Math.max(58, codeText.contentHeight + 6))
        contentWidth: Math.max(width, codeText.contentWidth)
        contentHeight: codeText.contentHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 3400

        TextEdit {
            id: codeText
            width: Math.max(codeViewport.width, contentWidth)
            height: contentHeight
            text: root.highlightedHtml
            textFormat: TextEdit.RichText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.NoWrap
            selectionColor: "#355b65"
            selectedTextColor: "#ffffff"
            color: "#d7d7dc"
            font.family: monoFont
            font.pixelSize: 12
            Accessible.name: root.code
        }

        ScrollBar.horizontal: ScrollBar {
            policy: codeViewport.contentWidth > codeViewport.width
                    ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }
    }
}
