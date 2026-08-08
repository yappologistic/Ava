import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    signal attachRequested()
    signal modelRequested(Item anchor)
    signal effortRequested(Item anchor)

    property alias text: composer.text
    property bool active: composer.activeFocus
    readonly property int attachmentHeight: chatController.attachmentCount > 0 ? 52 : 0

    implicitHeight: 112 + attachmentHeight

    function focusComposer() {
        composer.forceActiveFocus()
    }

    function submit() {
        var value = composer.text.trim()
        if (value.length === 0 || chatController.busy)
            return
        composer.text = ""
        chatController.sendMessage(value)
    }

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: "#101013"
        border.width: composer.activeFocus ? 1 : 1
        border.color: composer.activeFocus ? "#47747a" : "#29292e"

        Behavior on border.color {
            ColorAnimation { duration: reducedMotion ? 0 : 140 }
        }
    }

    ListView {
        id: attachmentList
        x: 12
        y: 10
        width: parent.width - 24
        height: root.attachmentHeight > 0 ? 40 : 0
        visible: height > 0
        orientation: ListView.Horizontal
        spacing: 8
        clip: true
        model: chatController.attachments

        delegate: Rectangle {
            required property int index
            required property string attachmentName
            required property string attachmentKind
            required property string previewUrl

            height: 38
            width: Math.min(190, attachmentLabel.implicitWidth + 56)
            radius: 10
            color: "#1a1a1f"
            border.width: 1
            border.color: "#2c2c32"

            Image {
                x: 5
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28
                visible: attachmentKind === "image" && previewUrl.length > 0
                source: previewUrl
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
                mipmap: true
            }

            Text {
                id: typeIcon
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                visible: attachmentKind !== "image"
                text: attachmentKind === "audio" ? "\uE8D6" : "\uE8A5"
                color: "#93939b"
                font.family: "Segoe Fluent Icons"
                font.pixelSize: 14
            }

            Text {
                id: attachmentLabel
                x: attachmentKind === "image" ? 40 : 36
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - x - 27
                text: attachmentName
                color: "#c8c8ce"
                elide: Text.ElideMiddle
                font.family: uiFont
                font.pixelSize: 10
            }

            ChatIconButton {
                anchors.right: parent.right
                anchors.rightMargin: 3
                anchors.verticalCenter: parent.verticalCenter
                width: 25
                height: 25
                symbol: "\uE711"
                accessibleName: "Remove " + attachmentName
                onClicked: chatController.removeAttachment(index)
            }
        }

        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: reducedMotion ? 0 : 140 }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: reducedMotion ? 0 : 160; easing.type: Easing.OutCubic }
        }
    }

    TextArea {
        id: composer
        x: 14
        y: 9 + root.attachmentHeight
        width: parent.width - 28
        height: 54
        padding: 0
        placeholderText: chatController.hasProject ? "Ask Codex to work on this project"
                                                   : "Choose a project to begin"
        placeholderTextColor: "#66666e"
        color: "#e7e7eb"
        selectionColor: "#355b65"
        selectedTextColor: "#ffffff"
        font.family: uiFont
        font.pixelSize: 13
        wrapMode: TextEdit.Wrap
        background: null
        enabled: chatController.connected && chatController.authenticated
                 && !chatController.awaitingApproval
                 && !chatController.awaitingUserInput
        Accessible.name: "Message Codex"

        Keys.onPressed: function(event) {
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    && !(event.modifiers & Qt.ShiftModifier)) {
                root.submit()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && chatController.busy) {
                chatController.interrupt()
                event.accepted = true
            }
        }
    }

    Row {
        x: 10
        y: parent.height - 43
        height: 34
        spacing: 3

        ChatIconButton {
            symbol: "\uE898"
            accessibleName: "Attach files"
            onClicked: root.attachRequested()
        }

        ChatIconButton {
            symbol: "\uE8B9"
            accessibleName: "Attach clipboard image"
            onClicked: chatController.attachClipboardImage()
        }

        Button {
            id: modelButton
            height: 34
            leftPadding: 10
            rightPadding: 10
            text: chatController.selectedModelName.length > 0
                  ? chatController.selectedModelName : "Model"
            Accessible.name: "Choose model, current " + text
            onClicked: root.modelRequested(modelButton)
            contentItem: Text {
                text: modelButton.text
                color: "#a8a8af"
                font.family: uiFont
                font.pixelSize: 10
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 8
                color: modelButton.hovered ? "#202024" : "transparent"
            }
        }

        Button {
            id: effortButton
            visible: chatController.availableEfforts.length > 1
            height: 34
            leftPadding: 9
            rightPadding: 9
            text: chatController.selectedEffort.length > 0
                  ? chatController.selectedEffort.charAt(0).toUpperCase()
                    + chatController.selectedEffort.slice(1) : "Effort"
            Accessible.name: "Choose reasoning effort, current " + text
            onClicked: root.effortRequested(effortButton)
            contentItem: Text {
                text: effortButton.text
                color: "#898991"
                font.family: uiFont
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 8
                color: effortButton.hovered ? "#202024" : "transparent"
            }
        }

        Button {
            id: fastButton
            visible: chatController.supportsFast
            height: 34
            leftPadding: 9
            rightPadding: 9
            text: "Fast"
            checkable: true
            checked: chatController.fastMode
            Accessible.name: "Fast mode"
            Accessible.checked: checked
            onToggled: chatController.fastMode = checked
            contentItem: Row {
                spacing: 5
                Text {
                    text: "\uE945"
                    color: fastButton.checked ? "#8ee6dd" : "#777780"
                    font.family: "Segoe Fluent Icons"
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: fastButton.text
                    color: fastButton.checked ? "#b8f0ea" : "#898991"
                    font.family: uiFont
                    font.pixelSize: 10
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            background: Rectangle {
                radius: 8
                color: fastButton.checked ? "#17302f"
                     : (fastButton.hovered ? "#202024" : "transparent")
                border.width: fastButton.checked ? 1 : 0
                border.color: "#315653"
            }
        }
    }

    ChatIconButton {
        anchors.right: parent.right
        anchors.rightMargin: 10
        y: parent.height - 43
        width: 34
        height: 34
        radius: 17
        symbol: chatController.busy ? "\uE769" : "\uE72A"
        accessibleName: chatController.busy ? "Stop Codex" : "Send message"
        enabled: chatController.busy || (composer.text.trim().length > 0
                                         && chatController.hasProject)
        baseColor: "#242428"
        hoverColor: chatController.busy ? "#352226" : "#303036"
        pressedColor: chatController.busy ? "#47282e" : "#3a3a40"
        foregroundColor: enabled ? "#d7d7dc" : "#5d5d64"
        onClicked: chatController.busy ? chatController.interrupt() : root.submit()
    }
}
