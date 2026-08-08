import QtQuick 6.5
import QtQuick.Controls 6.5

Rectangle {
    id: root

    property bool open: false
    signal closeRequested()

    color: "#0e0e11"
    border.width: 1
    border.color: "#25252a"

    Column {
        anchors.fill: parent

        Item {
            width: parent.width
            height: 52

            Text {
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                text: "Changes"
                color: "#dedee3"
                font.family: uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }

            ChatIconButton {
                anchors.right: parent.right
                anchors.rightMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                symbol: "\uE711"
                accessibleName: "Close changes"
                onClicked: root.closeRequested()
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: "#25252a"
        }

        ScrollView {
            width: parent.width
            height: parent.height - 107
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AsNeeded
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            TextArea {
                id: diffArea
                readOnly: true
                selectByMouse: true
                text: chatController.diffText.length > 0
                      ? chatController.diffText : "No file changes yet."
                color: chatController.diffText.length > 0 ? "#c9c9cf" : "#66666e"
                selectionColor: "#355b65"
                selectedTextColor: "#ffffff"
                font.family: monoFont
                font.pixelSize: 11
                wrapMode: TextEdit.NoWrap
                leftPadding: 16
                rightPadding: 16
                topPadding: 15
                bottomPadding: 15
                background: null
                Accessible.name: "Current project diff"
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: "#25252a"
        }

        Row {
            x: 10
            width: parent.width
            height: 53
            spacing: 5

            Button {
                id: commitButton
                anchors.verticalCenter: parent.verticalCenter
                text: "Commit"
                enabled: chatController.diffText.length > 0 && !chatController.git.busy
                onClicked: commitPopup.open()
                contentItem: Text {
                    text: commitButton.text
                    color: commitButton.enabled ? "#d7d7dc" : "#5c5c63"
                    font.family: uiFont
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: 68
                    implicitHeight: 32
                    radius: 9
                    color: commitButton.hovered ? "#28282d" : "#1b1b1f"
                    border.width: 1
                    border.color: "#303036"
                }
            }

            Button {
                id: pushButton
                anchors.verticalCenter: parent.verticalCenter
                text: "Push"
                enabled: !chatController.git.busy
                onClicked: chatController.git.push(chatController.projectPath)
                contentItem: Text {
                    text: pushButton.text
                    color: pushButton.enabled ? "#a9a9b0" : "#5c5c63"
                    font.family: uiFont
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: 58
                    implicitHeight: 32
                    radius: 9
                    color: pushButton.hovered ? "#222226" : "transparent"
                }
            }

            Button {
                id: prButton
                anchors.verticalCenter: parent.verticalCenter
                text: "Draft PR"
                enabled: !chatController.git.busy
                onClicked: chatController.git.createPullRequest(chatController.projectPath, true)
                contentItem: Text {
                    text: prButton.text
                    color: prButton.enabled ? "#a9a9b0" : "#5c5c63"
                    font.family: uiFont
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: 70
                    implicitHeight: 32
                    radius: 9
                    color: prButton.hovered ? "#222226" : "transparent"
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - 224)
                text: chatController.git.busy ? chatController.git.statusText
                     : (chatController.git.errorMessage.length > 0
                        ? chatController.git.errorMessage : "")
                color: chatController.git.errorMessage.length > 0 ? "#e9948d" : "#777780"
                elide: Text.ElideRight
                font.family: uiFont
                font.pixelSize: 9
            }
        }
    }

    Popup {
        id: commitPopup
        x: 12
        y: parent.height - height - 57
        width: parent.width - 24
        height: 104
        modal: false
        focus: true
        padding: 10
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: Column {
            spacing: 8
            TextField {
                id: commitMessage
                width: parent.width
                height: 38
                placeholderText: "Commit message"
                color: "#e0e0e5"
                placeholderTextColor: "#686870"
                font.family: uiFont
                font.pixelSize: 11
                background: Rectangle {
                    radius: 9
                    color: "#111114"
                    border.width: 1
                    border.color: commitMessage.activeFocus ? "#456d72" : "#303036"
                }
                Keys.onReturnPressed: {
                    if (text.trim().length > 0) {
                        chatController.git.commitAll(chatController.projectPath, text)
                        commitPopup.close()
                    }
                }
            }
            ChatTextButton {
                anchors.right: parent.right
                text: "Commit changes"
                baseColor: "#242429"
                enabled: commitMessage.text.trim().length > 0
                onClicked: {
                    chatController.git.commitAll(chatController.projectPath, commitMessage.text)
                    commitPopup.close()
                }
            }
        }

        background: Rectangle {
            radius: 12
            color: "#18181c"
            border.width: 1
            border.color: "#34343a"
        }
        onOpened: commitMessage.forceActiveFocus()
    }
}
