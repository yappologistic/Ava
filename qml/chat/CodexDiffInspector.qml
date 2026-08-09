import QtQuick 6.5
import QtQuick.Controls 6.5

Rectangle {
    id: root

    property bool open: false
    readonly property var selectedChange: {
        const entries = chatController.git.changes
        for (let index = 0; index < entries.length; ++index) {
            if (entries[index].path === chatController.git.selectedPath)
                return entries[index]
        }
        return ({})
    }
    property string pendingDiscardPath: ""

    signal closeRequested()

    function refresh() {
        if (chatController.projectPath.length > 0)
            chatController.git.refreshChanges(chatController.projectPath)
    }

    function showSelectedFile() {
        if (!root.open || chatController.git.busy
                || chatController.git.selectedPath.length === 0)
            return
        chatController.git.selectFile(chatController.projectPath,
                                      chatController.git.selectedPath,
                                      chatController.git.selectedStaged)
    }

    function htmlEscape(value) {
        return value.replace(/&/g, "&amp;")
                    .replace(/</g, "&lt;")
                    .replace(/>/g, "&gt;")
    }

    function diffHtml(value) {
        if (value.length === 0)
            return "<pre><font color='#66666e'>No textual diff is available.</font></pre>"
        const lines = value.split("\n")
        let result = "<pre style='margin:0; white-space:pre'>"
        for (let index = 0; index < lines.length; ++index) {
            const source = lines[index]
            const escaped = htmlEscape(source)
            let color = "#b9b9c0"
            let background = "transparent"
            if (source.indexOf("@@") === 0) {
                color = "#8fcac4"
                background = "#132020"
            } else if (source.indexOf("+++") === 0
                       || source.indexOf("---") === 0
                       || source.indexOf("diff --git") === 0
                       || source.indexOf("index ") === 0) {
                color = "#777780"
            } else if (source.indexOf("+") === 0) {
                color = "#a8d5b7"
                background = "#122019"
            } else if (source.indexOf("-") === 0) {
                color = "#d7a19b"
                background = "#211515"
            }
            result += "<span style='color:" + color + ";background-color:"
                    + background + "'>" + (escaped.length > 0 ? escaped : " ")
                    + "</span>\n"
        }
        return result + "</pre>"
    }

    onOpenChanged: {
        if (open) {
            refresh()
            Qt.callLater(showSelectedFile)
        }
    }
    Component.onCompleted: refresh()

    color: "#0e0e11"
    border.width: 1
    border.color: "#25252a"

    Connections {
        target: chatController
        function onProjectChanged() { root.refresh() }
    }

    Connections {
        target: chatController.git
        function onChangesChanged() { Qt.callLater(root.showSelectedFile) }
    }

    Item {
        id: header
        x: 1
        y: 1
        width: parent.width - 2
        height: 50

        Text {
            x: 15
            anchors.verticalCenter: parent.verticalCenter
            text: chatController.git.hasChanges
                  ? "Changes · " + chatController.git.changes.length : "Changes"
            color: "#dedee3"
            font.family: uiFont
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }

        Text {
            anchors.right: refreshButton.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - 150)
            text: chatController.git.busy ? chatController.git.statusText
                  : (chatController.git.diffTruncated ? "Preview truncated"
                     : chatController.git.errorMessage)
            color: chatController.git.errorMessage.length > 0 ? "#d5948e" : "#6d6d75"
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            font.family: uiFont
            font.pixelSize: 9
        }

        ChatIconButton {
            id: refreshButton
            anchors.right: closeButton.left
            anchors.rightMargin: 1
            anchors.verticalCenter: parent.verticalCenter
            symbol: "\uE72C"
            accessibleName: "Refresh Git changes"
            enabled: !chatController.git.busy
            onClicked: root.refresh()
        }

        ChatIconButton {
            id: closeButton
            anchors.right: parent.right
            anchors.rightMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            symbol: "\uE711"
            accessibleName: "Close changes"
            onClicked: root.closeRequested()
        }
    }

    Rectangle {
        x: 1
        y: header.height
        width: parent.width - 2
        height: 1
        color: "#25252a"
    }

    ListView {
        id: fileList
        x: 1
        y: header.height + 1
        width: parent.width - 2
        height: Math.min(176, Math.max(54, contentHeight))
        clip: true
        model: chatController.git.changes
        currentIndex: {
            for (let index = 0; index < count; ++index) {
                if (chatController.git.changes[index].path
                        === chatController.git.selectedPath)
                    return index
            }
            return count > 0 ? 0 : -1
        }
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        Accessible.name: "Changed files"

        delegate: Button {
            id: fileButton
            required property var modelData
            required property int index

            width: fileList.width
            height: 44
            hoverEnabled: true
            Accessible.name: modelData.path + ", " + modelData.status + ", "
                             + modelData.additions + " additions, "
                             + modelData.deletions + " deletions"
            onClicked: {
                const stagedView = !modelData.unstaged && modelData.staged
                chatController.git.selectFile(chatController.projectPath,
                                              modelData.path, stagedView)
            }

            contentItem: Item {
                Text {
                    id: fileName
                    x: 14
                    y: 7
                    width: Math.max(40, parent.width - 126)
                    text: fileButton.modelData.path.split("/").pop()
                    color: fileButton.modelData.path === chatController.git.selectedPath
                           ? "#d8eae8" : "#c5c5cb"
                    elide: Text.ElideRight
                    font.family: uiFont
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
                Text {
                    x: 14
                    y: 24
                    width: Math.max(40, parent.width - 126)
                    text: fileButton.modelData.status + " · " + fileButton.modelData.path
                    color: "#67676f"
                    elide: Text.ElideMiddle
                    font.family: uiFont
                    font.pixelSize: 8
                }
                Text {
                    anchors.right: deletionCount.left
                    anchors.rightMargin: 9
                    anchors.verticalCenter: parent.verticalCenter
                    text: fileButton.modelData.untracked
                          ? "+new" : "+" + fileButton.modelData.additions
                    color: "#8fc09f"
                    font.family: monoFont
                    font.pixelSize: 9
                }
                Text {
                    id: deletionCount
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: "−" + fileButton.modelData.deletions
                    visible: !fileButton.modelData.untracked
                    color: "#c9908a"
                    font.family: monoFont
                    font.pixelSize: 9
                }
            }

            background: Rectangle {
                color: fileButton.modelData.path === chatController.git.selectedPath
                       ? "#17201f" : (fileButton.hovered ? "#17171a" : "transparent")
                Rectangle {
                    width: 2
                    height: parent.height - 12
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#79bcb5"
                    visible: fileButton.modelData.path === chatController.git.selectedPath
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: fileList.count === 0 && !chatController.git.busy
            text: chatController.git.repositoryPath.length > 0
                  ? "Working tree is clean" : "No Git repository loaded"
            color: "#66666e"
            font.family: uiFont
            font.pixelSize: 10
        }
    }

    Rectangle {
        id: selectionToolbar
        x: 1
        y: fileList.y + fileList.height
        width: parent.width - 2
        height: chatController.git.hasChanges ? 60 : 0
        color: "#101013"
        visible: height > 0

        Text {
            x: 14
            y: 8
            width: parent.width - 116
            text: chatController.git.selectedPath
            color: "#aaaab1"
            elide: Text.ElideMiddle
            font.family: monoFont
            font.pixelSize: 9
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 13
            y: 6
            spacing: 8
            Text {
                text: root.selectedChange.untracked === true
                      ? "+new" : "+" + chatController.git.selectedAdditions
                color: "#8fc09f"
                font.family: monoFont
                font.pixelSize: 9
            }
            Text {
                text: "−" + chatController.git.selectedDeletions
                visible: root.selectedChange.untracked !== true
                color: "#c9908a"
                font.family: monoFont
                font.pixelSize: 9
            }
        }

        Row {
            x: 8
            y: 28
            height: 28
            spacing: 3

            ChatTextButton {
                height: 27
                text: root.selectedChange.untracked ? "Untracked" : "Working tree"
                enabled: root.selectedChange.unstaged === true
                         && !chatController.git.busy
                baseColor: !chatController.git.selectedStaged ? "#24302f" : "transparent"
                foregroundColor: !chatController.git.selectedStaged ? "#c2e6e2" : "#85858d"
                onClicked: chatController.git.selectFile(chatController.projectPath,
                                                         chatController.git.selectedPath,
                                                         false)
            }
            ChatTextButton {
                height: 27
                text: "Staged"
                visible: root.selectedChange.staged === true
                enabled: !chatController.git.busy
                baseColor: chatController.git.selectedStaged ? "#24302f" : "transparent"
                foregroundColor: chatController.git.selectedStaged ? "#c2e6e2" : "#85858d"
                onClicked: chatController.git.selectFile(chatController.projectPath,
                                                         chatController.git.selectedPath,
                                                         true)
            }
        }
    }

    Rectangle {
        id: diffDivider
        x: 1
        y: selectionToolbar.y + selectionToolbar.height
        width: parent.width - 2
        height: 1
        color: "#25252a"
    }

    ScrollView {
        id: diffView
        x: 1
        y: diffDivider.y + 1
        width: parent.width - 2
        height: Math.max(80, footer.y - y)
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AsNeeded
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        TextArea {
            id: diffArea
            readOnly: true
            selectByMouse: true
            textFormat: TextEdit.RichText
            text: chatController.git.diffLoading
                  ? "<pre><font color='#66666e'>Loading diff…</font></pre>"
                  : root.diffHtml(chatController.git.selectedDiff)
            color: "#b9b9c0"
            selectionColor: "#355b65"
            selectedTextColor: "#ffffff"
            font.family: monoFont
            font.pixelSize: 11
            wrapMode: TextEdit.NoWrap
            leftPadding: 14
            rightPadding: 14
            topPadding: 12
            bottomPadding: 12
            background: null
            Accessible.name: chatController.git.selectedPath.length > 0
                             ? "Diff for " + chatController.git.selectedPath
                             : "Current project diff"
        }
    }

    Rectangle {
        id: footer
        x: 1
        y: parent.height - height - 1
        width: parent.width - 2
        height: 83
        color: "#0f0f12"

        Rectangle {
            width: parent.width
            height: 1
            color: "#25252a"
        }

        Row {
            x: 8
            y: 5
            height: 34
            spacing: 3

            ChatTextButton {
                height: 31
                text: "Stage"
                visible: !chatController.git.selectedStaged
                         && root.selectedChange.unstaged === true
                enabled: !chatController.git.busy
                         && root.selectedChange.conflict !== true
                onClicked: chatController.git.stageFile(chatController.projectPath,
                                                        chatController.git.selectedPath)
            }
            ChatTextButton {
                height: 31
                text: "Unstage"
                visible: chatController.git.selectedStaged
                         && root.selectedChange.staged === true
                enabled: !chatController.git.busy
                         && root.selectedChange.conflict !== true
                onClicked: chatController.git.unstageFile(chatController.projectPath,
                                                          chatController.git.selectedPath)
            }
            ChatTextButton {
                height: 31
                text: root.selectedChange.untracked === true ? "Delete file" : "Discard"
                foregroundColor: "#c99691"
                visible: !chatController.git.selectedStaged
                         && root.selectedChange.unstaged === true
                enabled: !chatController.git.busy
                         && root.selectedChange.conflict !== true
                onClicked: {
                    root.pendingDiscardPath = chatController.git.selectedPath
                    discardPopup.open()
                }
            }

        }

        Row {
            x: 8
            y: 43
            height: 34
            spacing: 3

            ChatTextButton {
                text: "Commit"
                height: 31
                baseColor: "#20282a"
                enabled: chatController.git.hasChanges && !chatController.git.busy
                onClicked: commitPopup.open()
            }
            ChatTextButton {
                text: "Push"
                height: 31
                enabled: chatController.git.repositoryPath.length > 0
                         && !chatController.git.busy
                onClicked: chatController.git.push(chatController.projectPath)
            }
            ChatTextButton {
                text: "Draft PR"
                height: 31
                enabled: chatController.git.repositoryPath.length > 0
                         && !chatController.git.busy
                onClicked: chatController.git.createPullRequest(chatController.projectPath, true)
            }
        }
    }

    Popup {
        id: commitPopup
        x: 12
        y: Math.max(12, parent.height - height - footer.height - 8)
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
                Accessible.name: "Commit message"
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
                    chatController.git.commitAll(chatController.projectPath,
                                                 commitMessage.text)
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
        onClosed: commitMessage.text = ""
    }

    Popup {
        id: discardPopup
        x: 12
        y: Math.max(12, (parent.height - height) / 2)
        width: parent.width - 24
        height: 154
        modal: true
        focus: true
        padding: 14
        closePolicy: Popup.CloseOnEscape

        contentItem: Column {
            spacing: 8
            Text {
                width: parent.width
                text: root.selectedChange.untracked === true
                      ? "Delete this untracked file?" : "Discard working changes?"
                color: "#dedee3"
                font.family: uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: root.pendingDiscardPath
                color: "#9a9aa2"
                elide: Text.ElideMiddle
                font.family: monoFont
                font.pixelSize: 9
            }
            Text {
                width: parent.width
                text: "This affects one file and cannot be undone by Ava."
                color: "#777780"
                wrapMode: Text.Wrap
                font.family: uiFont
                font.pixelSize: 9
            }
            Row {
                anchors.right: parent.right
                spacing: 4
                ChatTextButton {
                    text: "Cancel"
                    onClicked: discardPopup.close()
                }
                ChatTextButton {
                    text: root.selectedChange.untracked === true ? "Delete" : "Discard"
                    baseColor: "#39201f"
                    hoverColor: "#4a2725"
                    foregroundColor: "#efbbb5"
                    onClicked: {
                        const path = root.pendingDiscardPath
                        discardPopup.close()
                        chatController.git.discardFile(chatController.projectPath,
                                                       path, true)
                    }
                }
            }
        }

        background: Rectangle {
            radius: 12
            color: "#18181c"
            border.width: 1
            border.color: "#49302e"
        }
        onClosed: root.pendingDiscardPath = ""
    }
}
