pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property var colors
    property string uiFont: "Inter"
    property string monoFont: "Geist Mono"
    property string iconFont: "Segoe Fluent Icons"
    property bool reducedMotion: false
    property bool open: false
    property string promptText: promptInput.text
    readonly property bool composerReady: open && codexBridge.connected
                                                   && !codexBridge.active
                                                   && !codexBridge.awaitingApproval
                                                   && codexBridge.phase !== "completed"
                                                   && codexBridge.phase !== "error"
    readonly property int visualStateRank: composerReady ? 0
                                           : (codexBridge.active && !codexBridge.awaitingApproval ? 1
                                              : (codexBridge.awaitingApproval ? 2
                                                 : (codexBridge.phase === "completed" ? 3 : 4)))
    readonly property Item currentContentView: composerReady ? readyView
                                                : (codexBridge.active && !codexBridge.awaitingApproval
                                                   ? activityView
                                                   : (codexBridge.awaitingApproval
                                                      ? approvalView
                                                      : (codexBridge.phase === "completed"
                                                         ? completedView : errorView)))
    readonly property real currentContentCenterY: currentContentView.y
                                                   + (currentContentView.contentTop
                                                      + currentContentView.contentBottom) / 2

    function restingOffset(rank) {
        return visualStateRank > rank ? -6 : 6
    }

    function submit() {
        const value = promptInput.text.trim()
        if (value.length === 0 || !root.composerReady)
            return
        codexBridge.submitTask(value)
    }

    enabled: open
    opacity: enabled ? 1 : 0
    scale: enabled ? 1 : 0.975
    transformOrigin: Item.Top

    Behavior on opacity {
        SequentialAnimation {
            PauseAnimation { duration: root.enabled && !root.reducedMotion ? 58 : 0 }
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.state
                easing.type: MotionTokens.easeOut
            }
        }
    }
    Behavior on scale {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : MotionTokens.content
            easing.type: MotionTokens.easeOut
        }
    }

    Connections {
        target: codexBridge
        function onTaskAccepted() {
            promptInput.text = ""
            promptInput.focus = false
        }
        function onPanelOpenChanged() {
            if (!codexBridge.panelOpen)
                promptInput.focus = false
        }
    }

    Item {
        id: brandMark
        x: 30
        y: root.currentContentCenterY - height / 2
        width: 34
        height: 34
        scale: attentionPulse.value

        Behavior on y {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.content
                easing.type: MotionTokens.easeOut
            }
        }

        MorphingIcon {
            anchors.centerIn: parent
            width: 20
            height: 20
            iconWidth: 20
            iconHeight: 20
            reducedMotion: root.reducedMotion
            opacity: codexBridge.active && !codexBridge.awaitingApproval ? 0 : 1
            scale: codexBridge.active && !codexBridge.awaitingApproval ? 0.82 : 1
            source: codexBridge.awaitingApproval
                    ? Qt.resolvedUrl("../assets/icons/codex-approval-amber.svg")
                    : (codexBridge.phase === "completed"
                       ? Qt.resolvedUrl("../assets/icons/codex-complete-green.svg")
                       : Qt.resolvedUrl("../assets/icons/codex-terminal-blue.svg"))

            Behavior on opacity {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.state
                    easing.type: MotionTokens.easeOut
                }
            }
            Behavior on scale {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.content
                    easing.type: MotionTokens.settle
                }
            }
        }

        OpenTuiSpinner {
            anchors.centerIn: parent
            color: "#a8c7fa"
            fontFamily: root.monoFont
            running: codexBridge.active && !codexBridge.awaitingApproval
            reducedMotion: root.reducedMotion
            fontPixelSize: 17
            opacity: running ? 1 : 0
            scale: running ? 1 : 0.82

            Behavior on opacity {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.state
                    easing.type: MotionTokens.easeOut
                }
            }
            Behavior on scale {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.content
                    easing.type: MotionTokens.settle
                }
            }
        }
    }

    QtObject { id: attentionPulse; property real value: 1 }

    SequentialAnimation {
        id: attentionAnimation
        running: codexBridge.awaitingApproval && !root.reducedMotion
        loops: Animation.Infinite
        NumberAnimation { target: attentionPulse; property: "value"; to: 1.065; duration: 560; easing.type: Easing.InOutSine }
        NumberAnimation { target: attentionPulse; property: "value"; to: 1; duration: 700; easing.type: Easing.InOutSine }
    }

    Item {
        id: readyView
        property real contentTop: titleLabel.y
        property real contentBottom: policyLabel.y + policyLabel.implicitHeight
        x: 68
        y: 17
        width: parent.width - 92
        height: 100
        enabled: root.composerReady
        visible: opacity > 0.001
        opacity: enabled ? 1 : 0
        transform: Translate {
            y: readyView.enabled ? 0 : root.restingOffset(0)
            Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut } }
        }

        Behavior on opacity {
            SequentialAnimation {
                PauseAnimation { duration: readyView.enabled && !root.reducedMotion ? 34 : 0 }
                NumberAnimation { duration: root.reducedMotion ? 0 : (readyView.enabled ? MotionTokens.state : MotionTokens.press); easing.type: MotionTokens.easeOut }
            }
        }

        Text {
            id: titleLabel
            x: 0
            y: 1
            text: "Ask Codex"
            color: root.colors.text
            font.family: root.uiFont
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 30
            y: 3
            width: 170
            text: codexBridge.workspaceName
            color: root.colors.tertiary
            elide: Text.ElideLeft
            horizontalAlignment: Text.AlignRight
            font.family: root.monoFont
            font.pixelSize: 9
        }

        Rectangle {
            x: 0
            y: 34
            width: parent.width - 76
            height: 39
            radius: 13
            color: inputHover.hovered || promptInput.activeFocus ? "#1d1d20" : "#151517"
            border.width: promptInput.activeFocus ? 1 : 0
            border.color: "#34465b"

            HoverHandler { id: inputHover }

            TextInput {
                id: promptInput
                anchors.fill: parent
                anchors.leftMargin: 13
                anchors.rightMargin: 13
                verticalAlignment: TextInput.AlignVCenter
                color: root.colors.text
                selectionColor: "#365b84"
                selectedTextColor: root.colors.text
                clip: true
                font.family: root.uiFont
                font.pixelSize: 11
                maximumLength: 1200
                Accessible.name: "Task for Codex"
                Keys.onReturnPressed: root.submit()
                Keys.onEnterPressed: root.submit()
                Keys.onEscapePressed: {
                    focus = false
                    codexBridge.setPanelOpen(false)
                }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 13
                anchors.verticalCenter: parent.verticalCenter
                visible: promptInput.text.length === 0 && !promptInput.activeFocus
                text: "Describe a task…"
                color: root.colors.tertiary
                font.family: root.uiFont
                font.pixelSize: 11
            }

            Behavior on color { ColorAnimation { duration: MotionTokens.hover } }
            Behavior on border.width { NumberAnimation { duration: MotionTokens.press } }
        }

        IslandButton {
            x: parent.width - 68
            y: 34
            width: 68
            height: 39
            fixedWidth: 68
            text: "Run"
            enabled: promptInput.text.trim().length > 0
            onClicked: root.submit()
        }

        Text {
            id: policyLabel
            x: 1
            y: 81
            text: "workspace-write  ·  approvals on request"
            color: root.colors.tertiary
            font.family: root.monoFont
            font.pixelSize: 8
        }
    }

    Item {
        id: activityView
        property real contentTop: activityTitleRow.y
        property real contentBottom: activityMetaRow.y + activityMetaRow.implicitHeight
        x: 68
        y: 19
        width: parent.width - 92
        height: 94
        enabled: codexBridge.active && !codexBridge.awaitingApproval
        visible: opacity > 0.001
        opacity: enabled ? 1 : 0
        scale: enabled ? 1 : 0.975
        transform: Translate {
            y: activityView.enabled ? 0 : root.restingOffset(1)
            Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut } }
        }

        Behavior on opacity {
            SequentialAnimation {
                PauseAnimation { duration: activityView.enabled && !root.reducedMotion ? 34 : 0 }
                NumberAnimation { duration: root.reducedMotion ? 0 : (activityView.enabled ? MotionTokens.state : MotionTokens.press); easing.type: MotionTokens.easeOut }
            }
        }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut } }

        Row {
            id: activityTitleRow
            spacing: 7
            MorphingLabel {
                text: codexBridge.statusText
                color: root.colors.text
                fontFamily: root.uiFont
                fontPixelSize: 13
                fontWeight: Font.DemiBold
                reducedMotion: root.reducedMotion
            }
        }

        MorphingLabel {
            x: 0
            y: 30
            width: parent.width - 132
            text: codexBridge.activityText
            color: root.colors.secondary
            elide: Text.ElideRight
            fontFamily: root.monoFont
            fontPixelSize: 10
            fontWeight: Font.Normal
            reducedMotion: root.reducedMotion
        }

        Row {
            id: activityMetaRow
            x: 0
            y: 50
            spacing: 9
            Text {
                text: codexBridge.elapsedText
                color: root.colors.tertiary
                font.family: root.monoFont
                font.pixelSize: 9
                font.features: { "tnum": 1 }
            }
            Text {
                visible: codexBridge.changedFileCount > 0
                text: codexBridge.changedFileCount + (codexBridge.changedFileCount === 1 ? " file" : " files")
                color: root.colors.tertiary
                font.family: root.monoFont
                font.pixelSize: 9
            }
            Text {
                text: codexBridge.workspaceName
                color: root.colors.tertiary
                font.family: root.monoFont
                font.pixelSize: 9
            }
        }

        IslandButton {
            x: parent.width - 132
            y: 31
            text: "Stop"
            fixedWidth: 58
            quiet: true
            onClicked: codexBridge.interrupt()
        }
        IslandButton {
            x: parent.width - 68
            y: 31
            text: "Open"
            fixedWidth: 68
            onClicked: codexBridge.openCodexApp()
        }
    }

    Item {
        id: approvalView
        property real contentTop: approvalTitleLabel.y
        property real contentBottom: approvalGuidanceLabel.y + approvalGuidanceLabel.implicitHeight
        x: 68
        y: 18
        width: parent.width - 92
        height: 96
        enabled: codexBridge.awaitingApproval
        visible: opacity > 0.001
        opacity: enabled ? 1 : 0
        scale: enabled ? 1 : 0.96
        transform: Translate {
            y: approvalView.enabled ? 0 : root.restingOffset(2)
            Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut } }
        }

        Behavior on opacity {
            SequentialAnimation {
                PauseAnimation { duration: approvalView.enabled && !root.reducedMotion ? 34 : 0 }
                NumberAnimation { duration: root.reducedMotion ? 0 : (approvalView.enabled ? MotionTokens.state : MotionTokens.press); easing.type: MotionTokens.easeOut }
            }
        }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut } }

        Text {
            id: approvalTitleLabel
            text: codexBridge.approvalTitle
            color: root.colors.text
            font.family: root.uiFont
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Text {
            y: 28
            width: parent.width - 158
            text: codexBridge.approvalDetail
            color: "#d1b37b"
            elide: Text.ElideMiddle
            maximumLineCount: 1
            font.family: root.monoFont
            font.pixelSize: 9
        }
        Text {
            id: approvalGuidanceLabel
            y: 50
            text: "Review the action before allowing it"
            color: root.colors.tertiary
            font.family: root.uiFont
            font.pixelSize: 9
        }
        IslandButton {
            x: parent.width - 146
            y: 29
            text: "Deny"
            fixedWidth: 66
            quiet: true
            onClicked: codexBridge.denyPending()
        }
        IslandButton {
            x: parent.width - 74
            y: 29
            text: "Allow"
            fixedWidth: 74
            onClicked: codexBridge.approvePending()
        }
    }

    Item {
        id: completedView
        property real contentTop: completedTitleLabel.y
        property real contentBottom: completedMetaLabel.y + completedMetaLabel.implicitHeight
        x: 68
        y: 20
        width: parent.width - 92
        height: 92
        enabled: codexBridge.phase === "completed"
        visible: opacity > 0.001
        opacity: enabled ? 1 : 0
        scale: enabled ? 1 : 0.97
        transform: Translate {
            y: completedView.enabled ? 0 : root.restingOffset(3)
            Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut } }
        }
        Behavior on opacity {
            SequentialAnimation {
                PauseAnimation { duration: completedView.enabled && !root.reducedMotion ? 34 : 0 }
                NumberAnimation { duration: root.reducedMotion ? 0 : (completedView.enabled ? MotionTokens.state : MotionTokens.press); easing.type: MotionTokens.easeOut }
            }
        }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut } }

        Text {
            id: completedTitleLabel
            text: "Done"
            color: root.colors.text
            font.family: root.uiFont
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Text {
            y: 29
            width: parent.width - 10
            text: codexBridge.finalText
            color: root.colors.secondary
            elide: Text.ElideRight
            maximumLineCount: 1
            font.family: root.monoFont
            font.pixelSize: 10
        }
        Text {
            id: completedMetaLabel
            y: 52
            text: codexBridge.elapsedText + (codexBridge.changedFileCount > 0
                  ? "  ·  " + codexBridge.changedFileCount + " files changed" : "")
            color: root.colors.tertiary
            font.family: root.monoFont
            font.pixelSize: 9
        }
        IslandButton {
            x: parent.width - 146
            y: 31
            text: "New task"
            fixedWidth: 82
            quiet: true
            onClicked: codexBridge.dismissCompactActivity()
        }
        IslandButton {
            x: parent.width - 58
            y: 31
            text: "Open"
            fixedWidth: 58
            onClicked: codexBridge.openCodexApp()
        }
    }

    Item {
        id: errorView
        property real contentTop: errorTitleLabel.y
        property real contentBottom: errorDetailLabel.y + errorDetailLabel.implicitHeight
        x: 68
        y: 41
        width: parent.width - 92
        height: 92
        enabled: codexBridge.phase === "error"
        visible: opacity > 0.001
        opacity: enabled ? 1 : 0
        transform: Translate {
            y: errorView.enabled ? 0 : root.restingOffset(4)
            Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut } }
        }
        Behavior on opacity {
            SequentialAnimation {
                PauseAnimation { duration: errorView.enabled && !root.reducedMotion ? 34 : 0 }
                NumberAnimation { duration: root.reducedMotion ? 0 : (errorView.enabled ? MotionTokens.state : MotionTokens.press); easing.type: MotionTokens.easeOut }
            }
        }

        Text {
            id: errorTitleLabel
            text: "Codex unavailable"
            color: root.colors.text
            font.family: root.uiFont
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Text {
            id: errorDetailLabel
            y: 25
            width: parent.width - 174
            text: codexBridge.errorText
            color: root.colors.secondary
            elide: Text.ElideRight
            font.family: root.monoFont
            font.pixelSize: 10
        }
        IslandButton {
            x: parent.width - 148
            y: 7
            text: "Retry"
            fixedWidth: 68
            quiet: true
            onClicked: codexBridge.retryConnection()
        }
        IslandButton {
            x: parent.width - 74
            y: 7
            text: "Open"
            fixedWidth: 74
            onClicked: codexBridge.openCodexApp()
        }
    }

    IslandButton {
        z: 100
        x: parent.width - 43
        y: 8
        width: 28
        height: 28
        iconOnly: true
        bare: true
        iconSource: Qt.resolvedUrl("../assets/icons/dismiss-light.svg")
        iconSize: 15
        accessibleName: "Close Codex"
        onClicked: codexBridge.setPanelOpen(false)
    }
}
