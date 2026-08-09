import QtQuick 6.5
import QtQuick.Controls 6.5
import Ava.Chat.Native 1.0

Item {
    id: root

    required property var activities
    required property bool running
    required property string elapsed

    signal openUrlRequested(string url)

    property bool expanded: running
    readonly property bool compacting: {
        for (let index = 0; index < activities.length; ++index) {
            if (activities[index].kind === "compaction")
                return true
        }
        return false
    }
    readonly property bool compactingOnly: running && compacting
                                                   && activities.length === 1

    implicitHeight: header.height
                    + (expanded && !compactingOnly
                       ? activityColumn.implicitHeight + 8 : 0)
    clip: true

    onRunningChanged: {
        if (!running)
            expanded = false
        else
            expanded = true
    }

    Behavior on implicitHeight {
        NumberAnimation {
            duration: reducedMotion || root.running ? 0 : 180
            easing.type: Easing.OutCubic
        }
    }

    Item {
        id: header
        width: parent.width
        height: 34

        ThinkingOrb {
            id: orb
            visible: root.running
            width: 26
            height: 26
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            running: root.running
            reducedMotion: reducedMotion
            tint: "#dddddf"
        }

        Text {
            id: headerText
            anchors.left: root.running ? orb.right : parent.left
            anchors.leftMargin: root.running ? 8 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.running ? (root.compacting ? "Compacting…" : "Thinking…")
                               : (root.compacting ? "Context compacted"
                               : (root.elapsed.length > 0
                                  ? "Worked for " + root.elapsed : "Worked")
                               )
            color: headerHover.hovered ? "#aaaab1" : "#777780"
            font.family: uiFont
            font.pixelSize: 11

            Behavior on color {
                ColorAnimation { duration: reducedMotion ? 0 : 110 }
            }
        }

        Text {
            anchors.left: headerText.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: "\uE76C"
            rotation: root.expanded ? 90 : 0
            color: "#66666e"
            font.family: "Segoe Fluent Icons"
            font.pixelSize: 9
            visible: !root.compactingOnly

            Behavior on rotation {
                NumberAnimation {
                    duration: reducedMotion ? 0 : 150
                    easing.type: Easing.OutCubic
                }
            }
        }

        HoverHandler { id: headerHover }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            enabled: !root.compactingOnly
            onTapped: root.expanded = !root.expanded
        }
    }

    Column {
        id: activityColumn
        x: 0
        y: header.height
        width: parent.width
        spacing: 12
        visible: root.expanded && !root.compactingOnly
        opacity: visible ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: reducedMotion ? 0 : 130 }
        }

        Repeater {
            model: root.activities

            delegate: Column {
                id: activity
                required property var modelData
                width: activityColumn.width
                spacing: 6

                Row {
                    id: activityTitleRow
                    spacing: 8
                    visible: activity.modelData.kind !== "reasoning"

                    Rectangle {
                        width: activity.modelData.running ? 6 : 5
                        height: width
                        radius: width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        color: activity.modelData.error ? "#ef6f74"
                             : (activity.modelData.running ? "#79d8ce" : "#64646d")

                        SequentialAnimation on opacity {
                            running: activity.modelData.running && !reducedMotion
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.35; duration: 700; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutSine }
                        }
                    }

                    Text {
                        text: activity.modelData.title
                        color: activity.modelData.error ? "#ed9b94" : "#a1a1a9"
                        font.family: uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }

                Text {
                    width: parent.width - (activity.modelData.kind === "reasoning" ? 0 : 17)
                    x: activity.modelData.kind === "reasoning" ? 0 : 17
                    visible: activity.modelData.body.length > 0
                    text: activity.modelData.body
                    color: activity.modelData.kind === "reasoning" ? "#9b9ba3" : "#b8b8bf"
                    wrapMode: Text.Wrap
                    lineHeight: 1.34
                    lineHeightMode: Text.ProportionalHeight
                    font.family: activity.modelData.kind === "command" ? monoFont : uiFont
                    font.pixelSize: activity.modelData.kind === "command" ? 11 : 12
                }

                Text {
                    width: parent.width - 17
                    x: 17
                    visible: activity.modelData.detail.length > 0
                    text: activity.modelData.detail.length > 420
                          ? activity.modelData.detail.slice(0, 419) + "…"
                          : activity.modelData.detail
                    color: "#73737b"
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 5
                    elide: Text.ElideRight
                    font.family: monoFont
                    font.pixelSize: 10
                }

                Column {
                    x: 17
                    width: parent.width - 17
                    spacing: 2
                    visible: activity.modelData.sources.length > 0

                    Repeater {
                        model: activity.modelData.sources

                        delegate: Item {
                            id: sourceRow
                            required property var modelData
                            width: parent.width
                            height: 28

                            Rectangle {
                                anchors.fill: parent
                                radius: 7
                                color: sourceHover.hovered ? "#17171a" : "transparent"

                                Behavior on color {
                                    ColorAnimation { duration: reducedMotion ? 0 : 100 }
                                }
                            }

                            Image {
                                id: favicon
                                x: 7
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14
                                height: 14
                                source: sourceRow.modelData.favicon
                                sourceSize: Qt.size(28, 28)
                                asynchronous: true
                                cache: true
                                smooth: true
                            }

                            Rectangle {
                                x: 7
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14
                                height: 14
                                radius: 7
                                visible: favicon.status !== Image.Ready
                                         || sourceRow.modelData.favicon.length === 0
                                color: "#28282d"

                                Text {
                                    anchors.centerIn: parent
                                    text: sourceRow.modelData.host.length > 0
                                          ? sourceRow.modelData.host.charAt(0).toUpperCase() : "·"
                                    color: "#a6a6ad"
                                    font.family: uiFont
                                    font.pixelSize: 8
                                    font.weight: Font.DemiBold
                                }
                            }

                            Text {
                                id: sourceTitle
                                x: 29
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, parent.width * 0.55)
                                text: sourceRow.modelData.title
                                color: sourceHover.hovered ? "#a8dcd7" : "#9ccac6"
                                elide: Text.ElideRight
                                font.family: uiFont
                                font.pixelSize: 10
                            }

                            Text {
                                x: sourceTitle.x + sourceTitle.width + 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - x - 8
                                text: sourceRow.modelData.host
                                color: "#5f5f67"
                                elide: Text.ElideMiddle
                                font.family: uiFont
                                font.pixelSize: 9
                            }

                            HoverHandler {
                                id: sourceHover
                                cursorShape: Qt.PointingHandCursor
                            }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: root.openUrlRequested(sourceRow.modelData.url)
                            }
                        }
                    }
                }
            }
        }
    }
}
