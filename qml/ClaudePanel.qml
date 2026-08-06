pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Controls 6.5

// The island's Claude Code surface: what Claude is asking for, and the answer.
Item {
    id: root

    property var colors
    property string uiFont: "Inter"
    property string iconFont: "Segoe Fluent Icons"
    property bool reducedMotion: false

    readonly property string kind: claude.requestKind
    readonly property bool permissionRequest: kind === "permission"
    readonly property bool questionRequest: kind === "question"
    readonly property bool doneRequest: kind === "done"
    readonly property color claudeAccent: "#d97757"
    readonly property color allowGlow: "#63e6a5"
    readonly property color denyGlow: "#ff453a"

    function headline() {
        if (permissionRequest)
            return "Claude wants to run " + claude.requestTitle
        if (questionRequest)
            return claude.requestTitle.length > 0 ? claude.requestTitle : "Claude has a question"
        if (doneRequest)
            return "Claude finished"
        return claude.requestTitle
    }

    onKindChanged: {
        if (!reducedMotion && kind.length > 0)
            cardArrival.restart()
    }

    Item {
        id: card
        anchors.fill: parent
        transform: Translate { id: cardShift }

        SequentialAnimation {
            id: cardArrival
            ParallelAnimation {
                NumberAnimation {
                    target: card
                    property: "scale"
                    from: 0.955
                    to: 1
                    duration: MotionTokens.content
                    easing.type: MotionTokens.easeOut
                }
                NumberAnimation {
                    target: cardShift
                    property: "y"
                    from: -6
                    to: 0
                    duration: MotionTokens.content
                    easing.type: MotionTokens.easeOut
                }
            }
        }

        // Header
        Row {
            id: header
            x: 20
            y: 13
            spacing: 7

            Rectangle {
                id: statusDot
                anchors.verticalCenter: parent.verticalCenter
                width: 7
                height: 7
                radius: 3.5
                color: root.doneRequest ? root.colors.green : root.claudeAccent

                SequentialAnimation on opacity {
                    running: !root.doneRequest && !root.reducedMotion
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 620; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1; duration: 620; easing.type: Easing.InOutSine }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "CLAUDE CODE"
                color: root.colors.secondary
                font.family: root.uiFont
                font.pixelSize: 8
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: claude.requestProject.length > 0
                width: projectLabel.implicitWidth + 12
                height: 15
                radius: 7.5
                color: root.colors.raised

                Text {
                    id: projectLabel
                    anchors.centerIn: parent
                    text: claude.requestProject
                    color: root.colors.tertiary
                    font.family: root.uiFont
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 18
            y: 10
            spacing: 6

            Rectangle {
                id: approvalChip
                anchors.verticalCenter: parent.verticalCenter
                visible: root.permissionRequest
                width: approvalLabel.implicitWidth + 14
                height: 15
                radius: 7.5
                color: approvalHover.hovered ? "#252527" : root.colors.raised
                Accessible.name: "Stop asking for permission in Ava"
                Accessible.role: Accessible.Button

                HoverHandler { id: approvalHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        claude.setApprovalEnabled(false)
                        claude.deferToTerminal()
                    }
                }

                Text {
                    id: approvalLabel
                    anchors.centerIn: parent
                    text: "ASK IN TERMINAL"
                    color: root.colors.tertiary
                    font.family: root.uiFont
                    font.pixelSize: 7
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.6
                }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: claude.queuedCount > 1
                width: queueLabel.implicitWidth + 12
                height: 15
                radius: 7.5
                color: root.colors.raised

                Text {
                    id: queueLabel
                    anchors.centerIn: parent
                    text: "+" + (claude.queuedCount - 1)
                    color: root.colors.secondary
                    font.family: root.uiFont
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                    font.features: { "tnum": 1 }
                }
            }

            IslandButton {
                width: 22
                height: 22
                iconOnly: true
                bare: true
                iconSource: Qt.resolvedUrl("../assets/icons/dismiss-light.svg")
                iconSize: 11
                accessibleName: "Dismiss and answer in the terminal"
                onClicked: claude.dismiss()
            }
        }

        // Body
        Text {
            id: headlineText
            x: 20
            y: 33
            width: parent.width - 40
            text: root.headline()
            color: root.colors.text
            elide: Text.ElideRight
            maximumLineCount: 1
            font.family: root.uiFont
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }

        Text {
            id: detailText
            x: 20
            y: 53
            width: parent.width - 40
            visible: !replyRow.visible && text.length > 0
            text: claude.requestDetail
            color: root.colors.secondary
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 2
            lineHeight: 1.15
            font.family: root.uiFont
            font.pixelSize: root.permissionRequest ? 10 : 10
        }

        // Reply composer, only reachable once Ava has taken keyboard focus.
        Item {
            id: replyRow
            x: 20
            y: 51
            width: parent.width - 40
            height: 30
            visible: claude.replyMode

            Rectangle {
                anchors.fill: parent
                radius: 9
                color: root.colors.raised
                border.width: 1
                border.color: replyField.activeFocus ? root.claudeAccent : "#22ffffff"
                Behavior on border.color { ColorAnimation { duration: MotionTokens.hover } }
            }

            TextField {
                id: replyField
                anchors.fill: parent
                anchors.leftMargin: 11
                anchors.rightMargin: 11
                background: null
                color: root.colors.text
                placeholderText: "Tell Claude what to do next"
                placeholderTextColor: root.colors.tertiary
                font.family: root.uiFont
                font.pixelSize: 11
                selectByMouse: true
                Accessible.name: "Reply to Claude"
                onAccepted: claude.sendReply(text)
                Keys.onEscapePressed: claude.cancelReply()
            }

            onVisibleChanged: {
                if (visible) {
                    replyField.text = ""
                    replyField.forceActiveFocus()
                }
            }
        }

        // Actions
        Row {
            id: actionRow
            x: 20
            y: 90
            spacing: 7
            visible: !claude.replyMode

            IslandButton {
                visible: root.permissionRequest
                text: "Allow"
                fixedWidth: 78
                accented: true
                glowColor: root.allowGlow
                accessibleName: "Allow " + claude.requestTitle
                onClicked: claude.approve()
            }
            IslandButton {
                visible: root.permissionRequest
                text: "Deny"
                fixedWidth: 74
                glowColor: root.denyGlow
                onClicked: claude.deny()
            }

            Repeater {
                model: root.questionRequest ? claude.requestOptions : []

                IslandButton {
                    required property int index
                    required property string modelData
                    text: modelData
                    fixedWidth: Math.min(150, Math.max(72, modelData.length * 7 + 22))
                    accented: index === 0
                    onClicked: claude.chooseOption(index)
                }
            }

            IslandButton {
                visible: root.doneRequest
                text: "Keep going"
                fixedWidth: 96
                accented: true
                glowColor: root.allowGlow
                onClicked: claude.sendReply("Keep going with the task.")
            }
            IslandButton {
                visible: root.doneRequest
                text: "Reply"
                fixedWidth: 72
                onClicked: claude.beginReply()
            }
            IslandButton {
                visible: root.doneRequest || root.kind === "notice"
                text: "Dismiss"
                fixedWidth: 80
                quiet: true
                onClicked: claude.dismiss()
            }
        }

        Row {
            x: 20
            y: 90
            spacing: 7
            visible: claude.replyMode

            IslandButton {
                text: "Send"
                fixedWidth: 78
                accented: true
                glowColor: root.allowGlow
                onClicked: claude.sendReply(replyField.text)
            }
            IslandButton {
                text: "Cancel"
                fixedWidth: 80
                quiet: true
                onClicked: claude.cancelReply()
            }
        }

        // Every card hands the answer back to Claude Code's own prompt if it is
        // left alone, so the terminal stays a usable second answer surface.
        Item {
            id: handback
            anchors.right: parent.right
            anchors.rightMargin: 20
            y: 88
            width: 108
            height: 26
            visible: claude.requestLifetimeMs > 0 && !claude.replyMode

            Text {
                id: handbackLabel
                anchors.right: parent.right
                anchors.top: parent.top
                text: root.doneRequest || root.kind === "notice"
                      ? "closing" : "over to the terminal"
                color: root.colors.tertiary
                font.family: root.uiFont
                font.pixelSize: 8
            }

            Rectangle {
                anchors.right: parent.right
                anchors.top: handbackLabel.bottom
                anchors.topMargin: 5
                width: parent.width
                height: 3
                radius: 1.5
                color: root.colors.raised

                Rectangle {
                    id: graceFill
                    anchors.right: parent.right
                    width: parent.width
                    height: parent.height
                    radius: parent.radius
                    color: root.colors.tertiary
                }

                NumberAnimation {
                    target: graceFill
                    property: "width"
                    running: handback.visible && !root.reducedMotion
                    from: 108
                    to: 0
                    duration: Math.max(1, claude.requestLifetimeMs)
                    easing.type: Easing.Linear
                }
            }
        }
    }
}
