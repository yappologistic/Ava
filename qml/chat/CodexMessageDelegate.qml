import QtQuick 6.5
import QtQuick.Controls 6.5
import Ava.Chat.Native 1.0

Item {
    id: root

    required property int index
    required property string itemId
    required property string kind
    required property string phase
    required property string title
    required property string body
    required property string detail
    required property string itemStatus
    required property string cwd
    required property bool running
    required property bool isError
    required property var fileChanges
    required property int additions
    required property int deletions
    required property var activities
    required property string elapsed

    signal openDiffRequested(string path)
    signal openUrlRequested(string url)

    property bool expanded: kind === "file" && fileChanges.length <= 4
    readonly property bool conversational: kind === "user" || kind === "agent"
    readonly property bool thinking: kind === "reasoning" && running
    readonly property bool toolActivity: kind === "tool" || kind === "search"
                                         || kind === "command" || kind === "image"
    readonly property bool quietActivity: kind === "reasoning" || kind === "plan"
                                         || kind === "activity"
    readonly property int laneWidth: Math.min(width - 48, 920)
    readonly property int laneX: Math.max(24, (width - laneWidth) / 2)
    readonly property int contentWidth: kind === "user"
                                        ? Math.min(laneWidth, 650) : laneWidth

    function fileTint(extension) {
        const ext = extension.toLowerCase()
        if (ext === "qml") return "#6a4fc4"
        if (ext === "cpp" || ext === "cc" || ext === "c") return "#3d76b7"
        if (ext === "h" || ext === "hpp") return "#7562b4"
        if (ext === "ts" || ext === "tsx") return "#3178c6"
        if (ext === "js" || ext === "jsx") return "#8c7b28"
        if (ext === "py") return "#3d718e"
        if (ext === "md") return "#4f7659"
        if (ext === "json") return "#81662f"
        return "#45454d"
    }

    width: ListView.view ? ListView.view.width : 760
    implicitHeight: contentColumn.implicitHeight + 18
    Accessible.role: Accessible.StaticText
    Accessible.name: title + (body.length > 0 ? ": " + body : "")

    Column {
        id: contentColumn
        width: root.contentWidth
        x: root.kind === "user" ? root.laneX + root.laneWidth - width : root.laneX
        spacing: 7

        Row {
            id: thinkingRow
            width: parent.width
            height: 38
            spacing: 9
            visible: root.thinking

            ThinkingOrb {
                width: 34
                height: 34
                anchors.verticalCenter: parent.verticalCenter
                running: root.thinking
                reducedMotion: reducedMotion
                tint: "#e2e2e5"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Thinking…"
                color: "#a8a8af"
                font.family: uiFont
                font.pixelSize: 12
            }
        }

        Item {
            id: activityHeader
            width: parent.width
            height: Math.max(activityTitle.implicitHeight, activityState.implicitHeight)
            visible: root.kind !== "user" && root.kind !== "agent"
                     && root.kind !== "file"
                     && root.kind !== "work"
                     && root.kind !== "reasoning"
                     && !root.thinking

            Rectangle {
                id: activityMarker
                visible: root.toolActivity
                width: root.running ? 6 : 5
                height: width
                radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: root.isError ? "#ef6f74"
                                    : (root.running ? "#79d8ce" : "#676770")

                SequentialAnimation on opacity {
                    running: root.running && !reducedMotion
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.38; duration: 720; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 720; easing.type: Easing.InOutSine }
                }
            }

            Text {
                id: activityTitle
                x: activityMarker.visible ? 14 : 0
                text: root.kind === "reasoning" ? "" : root.title
                visible: text.length > 0
                color: root.isError ? "#ff9a92" : "#9b9ba3"
                font.family: uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                width: implicitWidth
            }

            Text {
                id: activityState
                visible: root.running
                text: "working"
                x: activityTitle.visible ? activityTitle.x + activityTitle.width + 12 : 0
                color: "#62626a"
                font.family: uiFont
                font.pixelSize: 10
            }
        }

        Rectangle {
            id: surface
            width: root.kind === "user"
                   ? Math.min(parent.width, Math.max(58, userMetrics.advanceWidth + 30))
                   : parent.width
            x: root.kind === "user" ? parent.width - width : 0
            implicitHeight: (fileCard.visible ? fileCard.implicitHeight
                            : (workDisclosure.visible ? workDisclosure.implicitHeight
                            : (planText.visible ? planText.implicitHeight
                                                : messageText.implicitHeight)
                              + (root.kind === "user" ? 24 : 10)))
                            + (detailArea.visible ? detailArea.implicitHeight + 10 : 0)
            visible: !root.thinking
            radius: root.kind === "user" ? 15 : 12
            color: root.kind === "user"
                   ? (userHover.hovered ? "#19191c" : "#151517")
                   : "transparent"
            border.width: 0

            Behavior on color {
                ColorAnimation { duration: reducedMotion ? 0 : 120 }
            }

            TextMetrics {
                id: userMetrics
                text: root.body
                font.family: uiFont
                font.pixelSize: 13
            }

            TextEdit {
                id: messageText
                x: root.kind === "user" ? 15 : (root.toolActivity ? 17 : 0)
                y: root.kind === "user" ? 11 : 3
                width: parent.width - (root.kind === "user" ? 30
                                                             : (root.toolActivity ? 17 : 0))
                visible: root.kind !== "plan" && root.kind !== "file"
                         && root.kind !== "work"
                readOnly: true
                selectByMouse: true
                selectionColor: "#355b65"
                selectedTextColor: "#ffffff"
                text: root.body.length > 0
                      ? (root.kind === "agent" && !root.running
                         ? chatTextStyler.renderMarkdown(root.body) : root.body)
                      : (root.running ? "Working…" : "")
                textFormat: root.kind === "agent" && !root.running
                            ? TextEdit.RichText : TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                horizontalAlignment: TextEdit.AlignLeft
                color: root.kind === "reasoning" ? "#a1a1aa" : "#dedee3"
                font.family: root.kind === "command" ? monoFont : uiFont
                font.pixelSize: root.kind === "command" ? 12 : 13
                Accessible.name: root.body
                onLinkActivated: function(link) { root.openUrlRequested(link) }
            }

            CodexWorkDisclosure {
                id: workDisclosure
                width: parent.width
                visible: root.kind === "work"
                activities: root.activities
                running: root.running
                elapsed: root.elapsed
                onOpenUrlRequested: function(url) { root.openUrlRequested(url) }
            }

            Rectangle {
                visible: root.toolActivity && root.body.length > 0
                x: 4
                y: 5
                width: 1
                height: Math.max(12, messageText.implicitHeight - 2)
                color: "#2b2b30"
            }

            Text {
                id: planText
                x: 0
                y: 2
                width: parent.width
                visible: root.kind === "plan"
                text: root.body
                color: "#dedee3"
                wrapMode: Text.Wrap
                lineHeight: 1.42
                lineHeightMode: Text.ProportionalHeight
                font.family: uiFont
                font.pixelSize: 13
            }

            Rectangle {
                id: fileCard
                width: parent.width
                implicitHeight: 42 + (root.expanded ? fileRows.implicitHeight + 8 : 0)
                visible: root.kind === "file"
                radius: 14
                color: "#111114"
                clip: true

                Behavior on implicitHeight {
                    NumberAnimation {
                        duration: reducedMotion ? 0 : 175
                        easing.type: Easing.OutCubic
                    }
                }

                Item {
                    id: fileHeader
                    x: 8
                    y: 4
                    width: parent.width - 16
                    height: 34

                    Text {
                        id: fileChevron
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "\uE76C"
                        rotation: root.expanded ? 90 : 0
                        color: "#777780"
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 11
                        Behavior on rotation {
                            NumberAnimation {
                                duration: reducedMotion ? 0 : 150
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    Text {
                        id: fileSummary
                        anchors.left: fileChevron.right
                        anchors.leftMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.fileChanges.length + " changed file"
                              + (root.fileChanges.length === 1 ? "" : "s")
                        color: "#d0d0d5"
                        font.family: uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }

                    Row {
                        anchors.left: fileSummary.right
                        anchors.leftMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7

                        Text {
                            text: "+" + root.additions
                            visible: root.additions > 0
                            color: "#48cda6"
                            font.family: monoFont
                            font.pixelSize: 10
                        }

                        Text {
                            text: "−" + root.deletions
                            visible: root.deletions > 0
                            color: "#ef6f74"
                            font.family: monoFont
                            font.pixelSize: 10
                        }
                    }

                    ChatTextButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: 28
                        text: "Open diff"
                        foregroundColor: "#b9b9c0"
                        hoverColor: "#25252a"
                        onClicked: root.openDiffRequested("")
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: root.expanded = !root.expanded
                    }
                }

                Column {
                    id: fileRows
                    x: 8
                    y: 40
                    width: parent.width - 16
                    spacing: 1
                    visible: root.expanded
                    opacity: root.expanded ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: reducedMotion ? 0 : 120 }
                    }

                    Repeater {
                        model: root.fileChanges

                        delegate: Item {
                            id: fileRow
                            required property var modelData
                            width: fileRows.width
                            height: 31

                            Rectangle {
                                anchors.fill: parent
                                radius: 8
                                color: fileHover.hovered ? "#1c1c20" : "transparent"
                            }

                            Rectangle {
                                x: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: 23
                                height: 17
                                radius: 4
                                color: root.fileTint(fileRow.modelData.extension)
                                Text {
                                    anchors.centerIn: parent
                                    text: fileRow.modelData.extension.length > 0
                                          ? fileRow.modelData.extension : "FILE"
                                    color: "#f0f0f3"
                                    font.family: monoFont
                                    font.pixelSize: 7
                                    font.weight: Font.DemiBold
                                }
                            }

                            Text {
                                x: 39
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 132
                                text: (fileRow.modelData.directory.length > 0
                                      ? fileRow.modelData.directory + "/" : "")
                                      + fileRow.modelData.name
                                color: "#a8a8af"
                                elide: Text.ElideMiddle
                                font.family: monoFont
                                font.pixelSize: 10
                            }

                            Text {
                                anchors.right: deletionCount.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: "+" + fileRow.modelData.additions
                                visible: fileRow.modelData.additions > 0
                                color: "#48cda6"
                                font.family: monoFont
                                font.pixelSize: 9
                            }
                            Text {
                                id: deletionCount
                                anchors.right: parent.right
                                anchors.rightMargin: 9
                                anchors.verticalCenter: parent.verticalCenter
                                text: "−" + fileRow.modelData.deletions
                                visible: fileRow.modelData.deletions > 0
                                color: "#ef6f74"
                                font.family: monoFont
                                font.pixelSize: 9
                            }

                            HoverHandler { id: fileHover }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: root.openDiffRequested(fileRow.modelData.path)
                            }
                        }
                    }
                }
            }

            Column {
                id: detailArea
                x: root.kind === "user" ? 13 : 0
                width: parent.width - (root.kind === "user" ? 26 : 0)
                y: (fileCard.visible ? fileCard.implicitHeight
                    : (workDisclosure.visible ? workDisclosure.implicitHeight
                    : (planText.visible ? planText.y + planText.implicitHeight
                                        : messageText.y + messageText.implicitHeight))) + 8
                visible: root.detail.length > 0 && (root.expanded || root.isError)
                spacing: 6

                Rectangle {
                    width: parent.width
                    height: 1
                    color: "#29292e"
                }

                TextEdit {
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    text: root.detail
                    color: "#aeb0b7"
                    font.family: monoFont
                    font.pixelSize: 11
                    wrapMode: TextEdit.WrapAnywhere
                }
            }

            TapHandler {
                enabled: root.detail.length > 0
                acceptedButtons: Qt.LeftButton
                onTapped: root.expanded = !root.expanded
            }

            HoverHandler { id: hover }
            HoverHandler { id: userHover; enabled: root.kind === "user" }

            ChatIconButton {
                visible: root.detail.length > 0 && hover.hovered
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 7
                width: 28
                height: 28
                symbol: root.expanded ? "\uE70D" : "\uE70E"
                accessibleName: root.expanded ? "Collapse details" : "Expand details"
                onClicked: root.expanded = !root.expanded
            }
        }
    }
}
