import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    signal attachRequested()
    signal modelRequested(Item anchor)
    signal effortRequested(Item anchor)

    property alias text: composer.text
    property bool active: composer.activeFocus
    property int commandIndex: 0
    property bool commandMenuDismissed: false
    readonly property string commandPrefix: composer.text.trim()
    readonly property var commandItems: [
        { command: "/review", label: "Review changes" },
        { command: "/compact", label: "Compact conversation" }
    ]
    readonly property var filteredCommands: commandItems.filter(function(item) {
        return item.command.indexOf(commandPrefix.toLowerCase()) === 0
    })
    readonly property bool commandMenuOpen: composer.activeFocus
                                                     && !commandMenuDismissed
                                                     && commandPrefix.charAt(0) === "/"
                                                     && commandPrefix.indexOf(" ") < 0
                                                     && filteredCommands.length > 0
    readonly property int attachmentHeight: chatController.attachmentCount > 0 ? 52 : 0

    implicitHeight: 112 + attachmentHeight

    function focusComposer() {
        composer.forceActiveFocus()
    }

    function submit() {
        var value = composer.text.trim()
        if (value.length === 0 || !chatController.canSubmit)
            return
        if (value === "/review" || value.startsWith("/review ")) {
            composer.text = ""
            chatController.startReview(value.slice(7).trim())
            return
        }
        if (value === "/compact") {
            composer.text = ""
            chatController.compactThread()
            return
        }
        if (chatController.sendMessage(value))
            composer.text = ""
    }

    function runCommand(command) {
        composer.text = ""
        commandMenuDismissed = true
        if (command === "/review")
            chatController.startReview()
        else if (command === "/compact")
            chatController.compactThread()
    }

    onCommandPrefixChanged: {
        commandIndex = 0
        commandMenuDismissed = false
    }

    Rectangle {
        id: commandMenu
        z: 20
        x: 0
        y: -height - 8
        width: Math.min(root.width, 260)
        height: commandColumn.implicitHeight + 12
        radius: 12
        color: "#161619"
        visible: root.commandMenuOpen
        opacity: visible ? 1 : 0
        scale: visible ? 1 : 0.98
        transformOrigin: Item.BottomLeft

        Behavior on opacity {
            NumberAnimation { duration: reducedMotion ? 0 : 120 }
        }
        Behavior on scale {
            NumberAnimation {
                duration: reducedMotion ? 0 : 140
                easing.type: Easing.OutCubic
            }
        }

        Column {
            id: commandColumn
            x: 6
            y: 6
            width: parent.width - 12

            Repeater {
                model: root.filteredCommands

                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    width: commandColumn.width
                    height: 38
                    radius: 8
                    color: index === root.commandIndex ? "#232327" : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: reducedMotion ? 0 : 90 }
                    }

                    Text {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.command
                        color: "#d7d7dc"
                        font.family: monoFont
                        font.pixelSize: 11
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: "#74747c"
                        font.family: uiFont
                        font.pixelSize: 10
                    }

                    HoverHandler {
                        id: commandHover
                        cursorShape: Qt.PointingHandCursor
                        onHoveredChanged: {
                            if (hovered)
                                root.commandIndex = index
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: root.runCommand(modelData.command)
                    }
                }
            }
        }
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
        x: 9
        y: 24 + root.attachmentHeight
        width: parent.width - 23
        height: 39
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
            if (root.commandMenuOpen && event.key === Qt.Key_Down) {
                root.commandIndex = Math.min(root.filteredCommands.length - 1,
                                             root.commandIndex + 1)
                event.accepted = true
            } else if (root.commandMenuOpen && event.key === Qt.Key_Up) {
                root.commandIndex = Math.max(0, root.commandIndex - 1)
                event.accepted = true
            } else if (root.commandMenuOpen
                       && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                root.runCommand(root.filteredCommands[root.commandIndex].command)
                event.accepted = true
            } else if (root.commandMenuOpen && event.key === Qt.Key_Escape) {
                root.commandMenuDismissed = true
                event.accepted = true
            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
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
            implicitWidth: fastContent.implicitWidth + leftPadding + rightPadding
            leftPadding: 10
            rightPadding: 10
            text: "Fast"
            checkable: true
            checked: chatController.fastMode
            Accessible.name: "Fast mode"
            Accessible.checked: checked
            onToggled: chatController.fastMode = checked
            contentItem: Row {
                id: fastContent
                spacing: 6
                Item {
                    width: 12
                    height: 14
                    anchors.verticalCenter: parent.verticalCenter

                    Canvas {
                        id: fastGlyph
                        anchors.fill: parent
                        onPaint: {
                            var context = getContext("2d")
                            context.clearRect(0, 0, width, height)
                            context.beginPath()
                            context.moveTo(7.1, 0.8)
                            context.lineTo(1.8, 7.4)
                            context.lineTo(5.8, 7.4)
                            context.lineTo(4.5, 13.2)
                            context.lineTo(10.7, 5.6)
                            context.lineTo(6.8, 5.6)
                            context.closePath()
                            context.lineWidth = 1.15
                            context.strokeStyle = fastButton.checked ? "#8ee6dd" : "#777780"
                            context.fillStyle = "#8ee6dd"
                            if (fastButton.checked)
                                context.fill()
                            else
                                context.stroke()
                        }

                        Connections {
                            target: fastButton
                            function onCheckedChanged() { fastGlyph.requestPaint() }
                        }
                    }
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
                color: fastButton.hovered ? "#202024" : "transparent"
                border.width: 0
            }
        }

        Button {
            id: planButton
            height: 34
            implicitWidth: planContent.implicitWidth + leftPadding + rightPadding
            leftPadding: 10
            rightPadding: 10
            text: "Plan"
            checkable: true
            checked: chatController.planMode
            enabled: !chatController.busy
            Accessible.name: "Plan mode"
            Accessible.checked: checked
            onToggled: chatController.planMode = checked
            contentItem: Row {
                id: planContent
                spacing: 6
                Item {
                    width: 13
                    height: 13
                    anchors.verticalCenter: parent.verticalCenter

                    Repeater {
                        model: 3
                        Item {
                            required property int index
                            x: 0
                            y: index * 4.5 + 1
                            width: 13
                            height: 3

                            Rectangle {
                                width: 2.5
                                height: 2.5
                                radius: 1.25
                                color: planButton.checked ? "#8ee6dd" : "#777780"
                            }
                            Rectangle {
                                x: 5
                                y: 0.75
                                width: 8
                                height: 1
                                radius: 0.5
                                color: planButton.checked ? "#8ee6dd" : "#777780"
                            }
                        }
                    }
                }
                Text {
                    text: planButton.text
                    color: planButton.checked ? "#b8e8e3" : "#898991"
                    font.family: uiFont
                    font.pixelSize: 10
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            background: Rectangle {
                radius: 8
                color: planButton.hovered ? "#202024" : "transparent"
                border.width: 0
            }
        }

        Item {
            id: contextUsage
            visible: chatController.contextUsagePercent >= 0
            width: visible ? 30 : 0
            height: 34
            Accessible.role: Accessible.Indicator
            Accessible.name: chatController.contextUsagePercent + "% of context used"

            Rectangle {
                anchors.centerIn: parent
                width: 26
                height: 26
                radius: 8
                color: contextHover.hovered ? "#202024" : "transparent"

                Behavior on color {
                    ColorAnimation { duration: reducedMotion ? 0 : 90 }
                }
            }

            Canvas {
                id: contextRing
                anchors.centerIn: parent
                width: 15
                height: 15
                antialiasing: true
                onPaint: {
                    var context = getContext("2d")
                    context.reset()
                    context.lineWidth = 1.6
                    context.strokeStyle = "#303037"
                    context.beginPath()
                    context.arc(width / 2, height / 2, 5.4, 0, Math.PI * 2)
                    context.stroke()
                    if (chatController.contextUsagePercent > 0) {
                        context.strokeStyle = chatController.contextUsagePercent >= 85
                                              ? "#d98a82" : "#79d8ce"
                        context.lineCap = "round"
                        context.beginPath()
                        context.arc(width / 2, height / 2, 5.4, -Math.PI / 2,
                                    -Math.PI / 2 + Math.PI * 2
                                    * chatController.contextUsagePercent / 100)
                        context.stroke()
                    }
                }

                Connections {
                    target: chatController
                    function onUsageChanged() { contextRing.requestPaint() }
                }
            }

            HoverHandler {
                id: contextHover
                cursorShape: Qt.PointingHandCursor
            }

            ToolTip {
                id: contextHint
                visible: contextHover.hovered
                delay: 350
                timeout: -1
                x: Math.round((contextUsage.width - width) / 2)
                y: -height - 7
                leftPadding: 10
                rightPadding: 10
                topPadding: 6
                bottomPadding: 6

                contentItem: Text {
                    text: chatController.contextUsagePercent + "% context"
                    color: "#b8b8bf"
                    font.family: uiFont
                    font.pixelSize: 10
                }

                background: Rectangle {
                    radius: 8
                    color: "#1a1a1e"
                }

                enter: Transition {
                    NumberAnimation {
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: reducedMotion ? 0 : 110
                    }
                    NumberAnimation {
                        property: "scale"
                        from: 0.97
                        to: 1
                        duration: reducedMotion ? 0 : 130
                        easing.type: Easing.OutCubic
                    }
                }

                exit: Transition {
                    NumberAnimation {
                        property: "opacity"
                        from: 1
                        to: 0
                        duration: reducedMotion ? 0 : 80
                    }
                }
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
        readonly property bool stopAction: chatController.busy
                                           && chatController.currentTurnId.length > 0
                                           && composer.text.trim().length === 0
        symbol: stopAction ? "\uE769" : "\uE72A"
        accessibleName: stopAction ? "Stop Codex"
                                   : (chatController.busy
                                      ? "Send follow-up to Codex" : "Send message")
        enabled: stopAction || (composer.text.trim().length > 0
                                && chatController.canSubmit)
        baseColor: "#242428"
        hoverColor: chatController.busy ? "#352226" : "#303036"
        pressedColor: chatController.busy ? "#47282e" : "#3a3a40"
        foregroundColor: enabled ? "#d7d7dc" : "#5d5d64"
        onClicked: stopAction ? chatController.interrupt() : root.submit()
    }
}
