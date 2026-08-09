import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Dialogs
import QtQuick.Layouts 6.5

ApplicationWindow {
    id: window

    width: 1120
    height: 760
    minimumWidth: 820
    minimumHeight: 560
    visible: true
    color: "transparent"
    title: chatController.hasThread ? "Ava Chat · " + chatController.projectName
                                    : "Ava Chat"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint

    property bool sidebarOpen: width >= 940
    property bool inspectorOpen: false
    property bool threadSearchOpen: false
    property real revealProgress: 0
    property int sidebarWidth: sidebarOpen ? 238 : 0
    property int inspectorWidth: inspectorOpen ? Math.min(410, width * 0.37) : 0
    readonly property int headerHeight: 54
    readonly property color accent: "#79d8ce"

    function toggleMaximize() {
        if (visibility === Window.Maximized)
            showNormal()
        else
            showMaximized()
    }

    function openThreadSearch() {
        sidebarOpen = true
        threadSearchOpen = true
        Qt.callLater(function() { threadSearchField.forceActiveFocus() })
    }

    function closeThreadSearch() {
        threadSearchOpen = false
        threadSearchField.text = ""
        chatController.clearThreadSearch()
        composer.focusComposer()
    }

    function selectThreadRow(row) {
        if (row < 0)
            return
        chatController.selectThread(row)
        if (threadSearchOpen) {
            threadSearchOpen = false
            threadSearchField.text = ""
        }
        composer.focusComposer()
    }

    Component.onCompleted: {
        revealProgress = 1
        composer.focusComposer()
    }

    onClosing: function(close) {
        if (chatController.busy) {
            close.accepted = false
            hide()
        }
    }

    Behavior on sidebarWidth {
        NumberAnimation { duration: reducedMotion ? 0 : 180; easing.type: Easing.OutCubic }
    }
    Behavior on inspectorWidth {
        NumberAnimation { duration: reducedMotion ? 0 : 190; easing.type: Easing.OutCubic }
    }

    Shortcut {
        sequence: "Ctrl+N"
        onActivated: newChatPopup.open()
    }
    Shortcut {
        sequence: "Ctrl+B"
        onActivated: window.sidebarOpen = !window.sidebarOpen
    }
    Shortcut {
        sequence: "Ctrl+Shift+D"
        enabled: chatController.diffText.length > 0
        onActivated: window.inspectorOpen = !window.inspectorOpen
    }
    Shortcut {
        sequence: "Ctrl+L"
        onActivated: composer.focusComposer()
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: window.openThreadSearch()
    }

    Rectangle {
        id: shell
        anchors.fill: parent
        radius: visibility === Window.Maximized ? 0 : 16
        color: "#0a0a0c"
        border.width: visibility === Window.Maximized ? 0 : 1
        border.color: "#28282d"
        clip: true
        opacity: window.revealProgress
        scale: 0.985 + window.revealProgress * 0.015

        Behavior on radius {
            NumberAnimation { duration: reducedMotion ? 0 : 140; easing.type: Easing.OutCubic }
        }
        Behavior on opacity {
            NumberAnimation { duration: reducedMotion ? 0 : 170; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            NumberAnimation { duration: reducedMotion ? 0 : 180; easing.type: Easing.OutCubic }
        }

        Rectangle {
            id: header
            x: 1
            y: 1
            width: parent.width - 2
            height: window.headerHeight
            color: "#0e0e10"

            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 142
                acceptedButtons: Qt.LeftButton
                onPressed: window.startSystemMove()
                onDoubleClicked: window.toggleMaximize()
            }

            Row {
                id: headerLeft
                x: 10
                height: parent.height
                spacing: 4

                ChatIconButton {
                    id: sidebarToggle
                    anchors.verticalCenter: parent.verticalCenter
                    z: 2
                    symbol: "\uE700"
                    accessibleName: window.sidebarOpen ? "Hide conversations" : "Show conversations"
                    onClicked: window.sidebarOpen = !window.sidebarOpen
                }

                ChatIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: window.sidebarOpen
                    symbol: "\uE721"
                    accessibleName: "Search conversations"
                    baseColor: window.threadSearchOpen ? "#1d1d21" : "transparent"
                    foregroundColor: window.threadSearchOpen ? "#d3d3d8" : "#8b8b93"
                    onClicked: window.threadSearchOpen
                               ? window.closeThreadSearch() : window.openThreadSearch()
                }

                Button {
                    id: projectButton
                    anchors.verticalCenter: parent.verticalCenter
                    height: 36
                    leftPadding: 10
                    rightPadding: 10
                    Accessible.name: "Choose project, current " + chatController.projectName
                    onClicked: folderDialog.open()
                    contentItem: Row {
                        spacing: 7
                        Text {
                            text: chatController.projectName
                            color: "#d7d7dc"
                            font.family: uiFont
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "\uE70D"
                            color: "#6f6f77"
                            font.family: "Segoe Fluent Icons"
                            font.pixelSize: 9
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    background: Rectangle {
                        radius: 9
                        color: projectButton.hovered ? "#1d1d21" : "transparent"
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: chatController.branchName.length > 0 && window.width > 980
                    width: Math.min(190, implicitWidth)
                    text: chatController.branchName
                    color: "#66666e"
                    elide: Text.ElideMiddle
                    font.family: monoFont
                    font.pixelSize: 9
                }
            }

            Row {
                id: headerStatus
                anchors.right: windowControls.left
                anchors.rightMargin: 7
                height: parent.height
                spacing: 3

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: chatController.busy && chatController.elapsedText.length > 0
                    text: chatController.elapsedText
                    color: "#5f5f67"
                    font.family: monoFont
                    font.pixelSize: 9
                }

                ChatIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: chatController.diffText.length > 0
                    symbol: "\uE8A5"
                    accessibleName: window.inspectorOpen ? "Hide changes" : "Show changes"
                    baseColor: window.inspectorOpen ? "#1f2b2b" : "transparent"
                    foregroundColor: window.inspectorOpen ? "#a2ddd6" : "#8b8b93"
                    onClicked: window.inspectorOpen = !window.inspectorOpen
                }
            }

            Row {
                id: windowControls
                anchors.right: parent.right
                anchors.rightMargin: 7
                height: parent.height
                spacing: 1

                ChatIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "\uE921"
                    accessibleName: "Minimize Ava Chat"
                    onClicked: window.showMinimized()
                }
                ChatIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: window.visibility === Window.Maximized ? "\uE923" : "\uE922"
                    accessibleName: window.visibility === Window.Maximized
                                    ? "Restore Ava Chat" : "Maximize Ava Chat"
                    onClicked: window.toggleMaximize()
                }
                ChatIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "\uE8BB"
                    accessibleName: "Close Ava Chat"
                    hoverColor: "#c42b1c"
                    pressedColor: "#a82519"
                    foregroundColor: "#b8b8be"
                    onClicked: window.close()
                }
            }

        }

        Item {
            id: workspace
            x: 1
            y: window.headerHeight + 1
            width: shell.width - 2
            height: shell.height - window.headerHeight - 2

            Rectangle {
                id: sidebar
                x: 0
                width: window.sidebarWidth
                height: parent.height
                visible: width > 0
                color: "#0d0d0f"
                clip: true

                Rectangle {
                    id: threadSearchSurface
                    x: window.sidebarOpen ? 10 : 0
                    y: 8
                    width: 218
                    height: window.threadSearchOpen ? 36 : 0
                    radius: 9
                    color: "#17171a"
                    opacity: window.threadSearchOpen ? 1 : 0
                    visible: opacity > 0
                    clip: true

                    Text {
                        x: 11
                        anchors.verticalCenter: parent.verticalCenter
                        text: "\uE721"
                        color: "#74747c"
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 12
                    }

                    TextField {
                        id: threadSearchField
                        x: 34
                        width: parent.width - 68
                        height: parent.height
                        padding: 0
                        color: "#d6d6db"
                        placeholderText: "Search conversations"
                        placeholderTextColor: "#686870"
                        font.family: uiFont
                        font.pixelSize: 10
                        selectByMouse: true
                        background: Item {}
                        onTextChanged: {
                            chatController.setThreadSearchQuery(text)
                            threadList.currentIndex = 0
                        }
                        Keys.onUpPressed: function(event) {
                            if (threadList.count > 0)
                                threadList.currentIndex = Math.max(0, threadList.currentIndex - 1)
                            event.accepted = true
                        }
                        Keys.onDownPressed: function(event) {
                            if (threadList.count > 0)
                                threadList.currentIndex = Math.min(threadList.count - 1,
                                                                   threadList.currentIndex + 1)
                            event.accepted = true
                        }
                        Keys.onReturnPressed: function(event) {
                            if (threadList.count > 0)
                                window.selectThreadRow(Math.max(0, threadList.currentIndex))
                            event.accepted = true
                        }
                        Keys.onEscapePressed: function(event) {
                            if (text.length > 0)
                                text = ""
                            else
                                window.closeThreadSearch()
                            event.accepted = true
                        }
                    }

                    ChatIconButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 1
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: 32
                        implicitHeight: 32
                        symbol: chatController.threadSearchPending ? "\uE895" : "\uE711"
                        accessibleName: chatController.threadSearchPending
                                        ? "Searching conversations" : "Clear search"
                        enabled: !chatController.threadSearchPending
                        foregroundColor: "#777780"
                        RotationAnimation on rotation {
                            running: chatController.threadSearchPending && !reducedMotion
                            from: 0
                            to: 360
                            duration: 850
                            loops: Animation.Infinite
                        }
                        onClicked: {
                            if (threadSearchField.text.length > 0)
                                threadSearchField.text = ""
                            else
                                window.closeThreadSearch()
                        }
                    }

                    Behavior on height {
                        NumberAnimation { duration: reducedMotion ? 0 : 145; easing.type: Easing.OutCubic }
                    }
                    Behavior on opacity {
                        NumberAnimation { duration: reducedMotion ? 0 : 110 }
                    }
                }

                ListView {
                    id: threadList
                    x: window.sidebarOpen ? 8 : 0
                    y: window.threadSearchOpen ? 52 : 8
                    width: 222
                    height: sidebar.height - 64 - (window.threadSearchOpen ? 44 : 0)
                    opacity: window.sidebarOpen ? 1 : 0
                    enabled: window.sidebarOpen
                    model: chatController.threads
                    clip: true
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    Accessible.name: "Codex conversations"
                    onCountChanged: {
                        if (window.threadSearchOpen)
                            currentIndex = count > 0 ? 0 : -1
                    }

                    Behavior on y {
                        NumberAnimation { duration: reducedMotion ? 0 : 145; easing.type: Easing.OutCubic }
                    }
                    Behavior on height {
                        NumberAnimation { duration: reducedMotion ? 0 : 145; easing.type: Easing.OutCubic }
                    }

                    Behavior on x {
                        NumberAnimation {
                            duration: reducedMotion ? 0 : 175
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on opacity {
                        NumberAnimation { duration: reducedMotion ? 0 : 105 }
                    }

                    delegate: Item {
                        id: threadRow
                        required property int index
                        required property string threadId
                        required property string title
                        required property string preview
                        required property string cwd
                        required property string threadStatus
                        required property bool pinned

                        width: threadList.width
                        height: 52
                        clip: true
                        scale: threadTap.pressed ? 0.985 : 1
                        Accessible.role: Accessible.ListItem
                        Accessible.name: title

                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            color: chatController.currentThreadId === threadId
                                   ? "#1b1b20"
                                   : (window.threadSearchOpen && threadList.currentIndex === index)
                                     ? "#17171a"
                                     : (threadHover.hovered ? "#151518" : "transparent")
                            border.width: chatController.currentThreadId === threadId ? 1 : 0
                            border.color: "#2b2b31"

                            Rectangle {
                                visible: chatController.currentThreadId === threadId
                                x: 3
                                anchors.verticalCenter: parent.verticalCenter
                                width: 2
                                height: 22
                                radius: 1
                                color: window.accent
                            }

                            Text {
                                x: 12
                                y: 9
                                width: parent.width - 24
                                text: threadRow.title
                                color: chatController.currentThreadId === threadId
                                       ? "#d8d8dd" : "#aaaaaf"
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                maximumLineCount: 1
                                font.family: uiFont
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                            Text {
                                x: 12
                                y: 28
                                width: parent.width - 24
                                text: threadRow.preview
                                color: "#5f5f67"
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                maximumLineCount: 1
                                font.family: uiFont
                                font.pixelSize: 8
                            }
                        }

                        HoverHandler { id: threadHover }
                        TapHandler {
                            id: threadTap
                            acceptedButtons: Qt.LeftButton
                            onTapped: window.selectThreadRow(threadRow.index)
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: function(mouse) {
                                const point = mapToItem(Overlay.overlay, mouse.x, mouse.y)
                                threadActions.threadId = threadRow.threadId
                                threadActions.threadPinned = threadRow.pinned
                                threadActions.x = Math.min(point.x,
                                                           Overlay.overlay.width
                                                           - threadActions.width - 8)
                                threadActions.y = Math.min(point.y,
                                                           Overlay.overlay.height
                                                           - threadActions.height - 8)
                                threadActions.open()
                            }
                        }
                        Behavior on scale {
                            NumberAnimation { duration: reducedMotion ? 0 : 90; easing.type: Easing.OutCubic }
                        }
                    }
                }

                Button {
                    id: newChatButton
                    x: window.sidebarOpen ? 10 : 2
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    width: 218
                    height: 38
                    opacity: window.sidebarOpen ? 1 : 0
                    enabled: window.sidebarOpen
                    onClicked: newChatPopup.open()
                    Accessible.name: "New Codex conversation"
                    contentItem: Row {
                        x: 11
                        spacing: 9
                        Text {
                            text: "\uE710"
                            color: "#d8d8dd"
                            font.family: "Segoe Fluent Icons"
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "New chat"
                            color: "#d8d8dd"
                            font.family: uiFont
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    background: Rectangle {
                        radius: 8
                        color: newChatButton.down ? "#202024"
                               : (newChatButton.hovered ? "#17171a" : "transparent")
                    }

                    Behavior on x {
                        NumberAnimation {
                            duration: reducedMotion ? 0 : 175
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on opacity {
                        NumberAnimation { duration: reducedMotion ? 0 : 105 }
                    }
                }

            }

            Item {
                id: mainRegion
                x: window.sidebarWidth
                width: parent.width - window.sidebarWidth - window.inspectorWidth
                height: parent.height

                Rectangle {
                    anchors.fill: parent
                    color: "#0a0a0c"
                }

                Rectangle {
                    id: statusBanner
                    x: (parent.width - width) / 2
                    y: 12
                    width: Math.min(parent.width - 36, 920)
                    height: visible ? 42 : 0
                    radius: 11
                    visible: chatController.errorMessage.length > 0
                             || (chatController.connected && !chatController.authenticated)
                    color: chatController.errorMessage.length > 0 ? "#1c1314" : "#141416"
                    border.width: 1
                    border.color: chatController.errorMessage.length > 0 ? "#51282b" : "#303036"
                    opacity: visible ? 1 : 0

                    Text {
                        x: 13
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 92
                        text: chatController.errorMessage.length > 0
                              ? chatController.errorMessage : "Sign in to Codex to begin"
                        color: chatController.errorMessage.length > 0 ? "#e7a09a" : "#b7b7bd"
                        elide: Text.ElideRight
                        font.family: uiFont
                        font.pixelSize: 10
                    }
                    ChatTextButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        height: 28
                        text: chatController.connected ? "Sign in" : "Retry"
                        visible: !chatController.connected || !chatController.authenticated
                        onClicked: chatController.connected
                                   ? chatController.startLogin()
                                   : chatController.retryConnection()
                    }
                }

                ListView {
                    id: transcript
                    property string scrollMode: "follow-end"
                    property string anchorItemId: ""
                    property string navigationHighlightId: ""
                    property string viewportAnchorItemId: ""
                    property real viewportAnchorOffset: 0
                    property bool viewportRestoreQueued: false
                    property int activePromptIndex: -1

                    function nearLiveEdge() {
                        return Math.max(0, contentHeight - contentY - height) <= 40
                    }

                    function updateActivePrompt() {
                        if (count <= 0) {
                            activePromptIndex = -1
                            return
                        }
                        let row = indexAt(1, contentY + 12)
                        for (let offset = 20; row < 0 && offset <= 72; offset += 8)
                            row = indexAt(1, contentY + offset)
                        if (row >= 0)
                            activePromptIndex = chatController.promptNavigator
                                .promptIndexForSourceRow(row)
                    }

                    function liveEdgeY() {
                        return Math.max(0, contentHeight - height)
                    }

                    function scheduleLiveEdge() {
                        Qt.callLater(function() {
                            if (scrollMode === "follow-end")
                                contentY = liveEdgeY()
                        })
                    }

                    function captureViewportAnchor() {
                        if (scrollMode !== "free" || moving || count <= 0)
                            return
                        let row = indexAt(1, contentY + 8)
                        if (row < 0)
                            row = indexAt(1, contentY + 28)
                        const item = row >= 0 ? itemAtIndex(row) : null
                        if (!item)
                            return
                        viewportAnchorItemId = item.itemId
                        viewportAnchorOffset = item.y - contentY
                    }

                    function restoreViewportAnchor() {
                        viewportRestoreQueued = false
                        if (scrollMode !== "free" || viewportAnchorItemId.length === 0)
                            return
                        const row = chatController.timeline.rowForItem(viewportAnchorItemId)
                        const item = row >= 0 ? itemAtIndex(row) : null
                        if (!item)
                            return
                        contentY = Math.max(0, item.y - viewportAnchorOffset)
                        updateActivePrompt()
                    }

                    function prepareViewportMutation() {
                        if (scrollMode !== "free" || viewportRestoreQueued)
                            return
                        captureViewportAnchor()
                        if (viewportAnchorItemId.length === 0)
                            return
                        viewportRestoreQueued = true
                    }

                    function finishViewportMutation() {
                        if (!viewportRestoreQueued)
                            return
                        Qt.callLater(restoreViewportAnchor)
                    }

                    function jumpToPrompt(row, messageId) {
                        if (row < 0)
                            return
                        scrollMode = "free"
                        anchorItemId = ""
                        navigationHighlightId = messageId
                        positionViewAtIndex(row, ListView.Beginning)
                        Qt.callLater(function() {
                            contentY = Math.max(0, contentY - 18)
                            updateActivePrompt()
                            navigationHighlightTimer.restart()
                        })
                    }

                    function anchorSubmittedMessage(messageId) {
                        scrollMode = "anchor-new-turn"
                        anchorItemId = messageId
                        viewportAnchorItemId = ""
                        Qt.callLater(function() {
                            const row = chatController.timeline.rowForItem(messageId)
                            if (row >= 0 && anchorItemId === messageId)
                                positionViewAtIndex(row, ListView.Beginning)
                        })
                    }

                    x: 0
                    y: statusBanner.visible ? 62 : 8
                    width: parent.width
                    height: parent.height - y - composer.height - approvalPanel.height
                            - inputPanel.height - 28
                    model: chatController.timeline
                    clip: true
                    spacing: 10
                    boundsBehavior: Flickable.StopAtBounds
                    flickDeceleration: 3400
                    maximumFlickVelocity: 2600
                    cacheBuffer: 1400
                    reuseItems: true
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    Accessible.name: "Codex conversation"

                    Connections {
                        target: chatController.timeline
                        function onDataChanged() {
                            transcript.prepareViewportMutation()
                            transcript.finishViewportMutation()
                        }
                        function onRowsAboutToBeInserted() {
                            transcript.prepareViewportMutation()
                        }
                        function onRowsInserted() {
                            transcript.finishViewportMutation()
                        }
                        function onRowsAboutToBeRemoved() {
                            transcript.prepareViewportMutation()
                        }
                        function onRowsRemoved() {
                            transcript.finishViewportMutation()
                        }
                    }

                    delegate: CodexMessageDelegate {
                        width: transcript.width
                        navigationHighlighted: itemId === transcript.navigationHighlightId
                        onOpenDiffRequested: function(path) {
                            window.inspectorOpen = true
                        }
                        onOpenUrlRequested: function(url) {
                            Qt.openUrlExternally(url)
                        }
                        onRetryRequested: function(messageId) {
                            chatController.retryMessage(messageId)
                        }
                    }

                    onCountChanged: {
                        if (scrollMode === "follow-end" || count <= 2)
                            scheduleLiveEdge()
                        Qt.callLater(updateActivePrompt)
                    }

                    onContentYChanged: updateActivePrompt()
                    onHeightChanged: {
                        if (scrollMode === "follow-end")
                            scheduleLiveEdge()
                        Qt.callLater(updateActivePrompt)
                    }

                    onMovementStarted: {
                        scrollMode = "free"
                        anchorItemId = ""
                        viewportAnchorItemId = ""
                    }

                    onMovementEnded: {
                        scrollMode = nearLiveEdge() ? "follow-end" : "free"
                    }

                    add: Transition {
                        NumberAnimation {
                            property: "opacity"
                            from: 0
                            to: 1
                            duration: reducedMotion ? 0 : 150
                            easing.type: Easing.OutCubic
                        }
                    }

                    onContentHeightChanged: {
                        if (scrollMode === "follow-end")
                            scheduleLiveEdge()
                    }
                }

                Timer {
                    id: navigationHighlightTimer
                    interval: 900
                    onTriggered: transcript.navigationHighlightId = ""
                }

                CodexPromptNavigator {
                    id: promptNavigator
                    readonly property real laneWidth: Math.min(
                        Math.max(0, transcript.width - 48), 920)
                    readonly property real sideGutter: Math.max(
                        0, (transcript.width - laneWidth) / 2)

                    x: Math.max(4, sideGutter - 44)
                    y: transcript.y
                    height: transcript.height
                    promptModel: chatController.promptNavigator
                    activePromptIndex: transcript.activePromptIndex
                    enabledByLayout: sideGutter >= 48
                    onJumpRequested: function(sourceRow, itemId) {
                        transcript.jumpToPrompt(sourceRow, itemId)
                    }
                }

                Column {
                    anchors.centerIn: transcript
                    width: Math.min(430, transcript.width - 60)
                    spacing: 8
                    visible: transcript.count === 0

                    Text {
                        width: parent.width
                        text: chatController.busy && chatController.hasThread
                              && transcript.count === 0
                              ? "Restoring conversation…"
                              : (chatController.hasProject
                                 ? chatController.projectName : "Choose a project")
                        color: "#dcdce1"
                        horizontalAlignment: Text.AlignHCenter
                        font.family: uiFont
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: chatController.hasProject
                              ? (chatController.environmentMode === "worktree"
                                 ? "Isolated on " + chatController.branchName
                                 : chatController.projectPath)
                              : ""
                        visible: text.length > 0
                        color: "#66666e"
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        font.family: chatController.environmentMode === "worktree" ? monoFont : uiFont
                        font.pixelSize: 9
                    }

                    ChatTextButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !chatController.hasProject
                        text: "Choose folder"
                        onClicked: folderDialog.open()
                    }
                }

                Rectangle {
                    id: inputPanel
                    x: (parent.width - width) / 2
                    width: Math.min(parent.width - 36, 920)
                    height: chatController.awaitingUserInput ? 158 : 0
                    anchors.bottom: composer.top
                    anchors.bottomMargin: chatController.awaitingUserInput ? 10 : 0
                    radius: 14
                    clip: true
                    color: "#111114"
                    opacity: chatController.awaitingUserInput ? 1 : 0
                    scale: chatController.awaitingUserInput ? 1 : 0.985

                    Behavior on height {
                        NumberAnimation { duration: reducedMotion ? 0 : 170; easing.type: Easing.OutCubic }
                    }
                    Behavior on opacity {
                        NumberAnimation { duration: reducedMotion ? 0 : 130 }
                    }
                    Behavior on scale {
                        NumberAnimation { duration: reducedMotion ? 0 : 170; easing.type: Easing.OutCubic }
                    }

                    Text {
                        x: 14
                        y: 12
                        width: parent.width - 54
                        text: chatController.userInputHeader
                        color: "#d8d8dd"
                        font.family: uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        x: 14
                        y: 35
                        width: parent.width - 52
                        text: chatController.userInputQuestion
                        color: "#9b9ba3"
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.family: uiFont
                        font.pixelSize: 10
                    }
                    ChatIconButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 7
                        anchors.top: parent.top
                        anchors.topMargin: 6
                        symbol: "\uE711"
                        accessibleName: "Continue without answering"
                        onClicked: chatController.cancelUserInput()
                    }

                    Row {
                        id: inputOptions
                        x: 10
                        y: 73
                        width: parent.width - 20
                        height: 30
                        spacing: 5
                        Repeater {
                            model: chatController.userInputOptions
                            delegate: Button {
                                id: inputOptionButton
                                required property string modelData
                                height: 30
                                text: modelData
                                onClicked: chatController.answerUserInput(modelData)
                                contentItem: Text {
                                    text: inputOptionButton.text
                                    color: "#c6c6cc"
                                    font.family: uiFont
                                    font.pixelSize: 9
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 8
                                    color: inputOptionButton.down ? "#29292e"
                                           : (inputOptionButton.hovered ? "#202024" : "#18181b")
                                }
                            }
                        }
                    }

                    TextField {
                        id: inputAnswer
                        x: 12
                        y: 112
                        width: parent.width - 106
                        height: 34
                        echoMode: chatController.userInputSecret
                                  ? TextInput.Password : TextInput.Normal
                        placeholderText: chatController.userInputOptions.length > 0
                                         ? "Or type another answer" : "Type your answer"
                        color: "#dedee3"
                        placeholderTextColor: "#62626a"
                        font.family: uiFont
                        font.pixelSize: 10
                        background: Rectangle {
                            radius: 8
                            color: "#18181b"
                        }
                        onAccepted: {
                            if (text.trim().length > 0) {
                                chatController.answerUserInput(text)
                                text = ""
                            }
                        }
                    }
                    ChatTextButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        y: 112
                        width: 76
                        height: 34
                        text: "Submit"
                        baseColor: "#1b1b20"
                        hoverColor: "#29292e"
                        enabled: inputAnswer.text.trim().length > 0
                        onClicked: {
                            chatController.answerUserInput(inputAnswer.text)
                            inputAnswer.text = ""
                        }
                    }
                }

                Rectangle {
                    id: approvalPanel
                    x: (parent.width - width) / 2
                    width: Math.min(parent.width - 36, 920)
                    height: chatController.awaitingApproval ? 96 : 0
                    anchors.bottom: inputPanel.visible ? inputPanel.top : composer.top
                    anchors.bottomMargin: chatController.awaitingApproval ? 10 : 0
                    radius: 14
                    clip: true
                    color: "#171411"
                    border.width: 1
                    border.color: "#4d3b28"
                    opacity: chatController.awaitingApproval ? 1 : 0
                    scale: chatController.awaitingApproval ? 1 : 0.98

                    Behavior on height {
                        NumberAnimation { duration: reducedMotion ? 0 : 160; easing.type: Easing.OutCubic }
                    }
                    Behavior on opacity {
                        NumberAnimation { duration: reducedMotion ? 0 : 130 }
                    }
                    Behavior on scale {
                        NumberAnimation { duration: reducedMotion ? 0 : 170; easing.type: Easing.OutCubic }
                    }

                    Text {
                        x: 14
                        y: 12
                        width: parent.width - 28
                        text: chatController.approvalTitle
                        color: "#e7d8c6"
                        font.family: uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        x: 14
                        y: 34
                        width: parent.width - 242
                        height: 45
                        text: chatController.approvalDetail
                        color: "#a99987"
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                        font.family: monoFont
                        font.pixelSize: 9
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 10
                        spacing: 5
                        ChatTextButton {
                            text: "Deny"
                            onClicked: chatController.denyApproval()
                        }
                        ChatTextButton {
                            text: "Allow once"
                            baseColor: "#202024"
                            onClicked: chatController.approveOnce()
                        }
                        ChatTextButton {
                            text: "Allow session"
                            baseColor: "#202024"
                            onClicked: chatController.approveForSession()
                        }
                    }
                }

                CodexComposer {
                    id: composer
                    x: (parent.width - width) / 2
                    width: Math.min(parent.width - 36, 920)
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 16
                    onAttachRequested: fileDialog.open()
                    onModelRequested: function(anchor) {
                        modelPopup.x = Math.max(12, anchor.mapToItem(mainRegion, 0, 0).x)
                        modelPopup.y = composer.y - modelPopup.height - 8
                        modelPopup.open()
                    }
                    onEffortRequested: function(anchor) {
                        effortPopup.x = Math.max(12, anchor.mapToItem(mainRegion, 0, 0).x)
                        effortPopup.y = composer.y - effortPopup.height - 8
                        effortPopup.open()
                    }
                }

                Popup {
                    id: modelPopup
                    width: 260
                    height: Math.min(330, modelList.contentHeight + 16)
                    padding: 8
                    modal: false
                    focus: true
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                    contentItem: ListView {
                        id: modelList
                        clip: true
                        spacing: 2
                        model: chatController.models
                        currentIndex: chatController.models.rowForModel(chatController.selectedModel)

                        delegate: Button {
                            required property int index
                            required property string modelId
                            required property string displayName
                            required property string description
                            required property bool defaultModel

                            width: modelList.width
                            height: 48
                            hoverEnabled: true
                            onClicked: {
                                chatController.selectedModel = modelId
                                modelPopup.close()
                            }
                            contentItem: Column {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 20
                                spacing: 2
                                Text {
                                    width: parent.width
                                    text: displayName
                                    color: modelId === chatController.selectedModel ? "#dff5f2" : "#d0d0d5"
                                    elide: Text.ElideRight
                                    font.family: uiFont
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    width: parent.width
                                    text: description
                                    visible: text.length > 0
                                    color: "#686870"
                                    elide: Text.ElideRight
                                    font.family: uiFont
                                    font.pixelSize: 8
                                }
                            }
                            background: Rectangle {
                                radius: 9
                                color: modelId === chatController.selectedModel
                                       ? "#1d2929" : (parent.hovered ? "#222226" : "transparent")
                            }
                        }
                    }
                    background: Rectangle {
                        radius: 13
                        color: "#161619"
                        border.width: 1
                        border.color: "#34343a"
                    }
                }

                Popup {
                    id: effortPopup
                    width: 150
                    height: effortColumn.implicitHeight + 16
                    padding: 8
                    modal: false
                    focus: true
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                    contentItem: Column {
                        id: effortColumn
                        width: parent.width
                        spacing: 2
                        Repeater {
                            model: chatController.availableEfforts
                            delegate: ChatTextButton {
                                id: effortOption
                                required property string modelData
                                width: effortColumn.width
                                height: 36
                                text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                                foregroundColor: modelData === chatController.selectedEffort
                                                 ? "#bce9e4" : "#aaaab2"
                                baseColor: modelData === chatController.selectedEffort
                                           ? "#1d2929" : "transparent"
                                hoverColor: "#222226"
                                onClicked: {
                                    chatController.selectedEffort = modelData
                                    effortPopup.close()
                                }
                            }
                        }
                    }
                    background: Rectangle {
                        radius: 13
                        color: "#161619"
                        border.width: 0
                    }
                }
            }

            CodexDiffInspector {
                id: inspector
                x: parent.width - window.inspectorWidth
                width: window.inspectorWidth
                height: parent.height
                visible: width > 0
                open: window.inspectorOpen
                onCloseRequested: window.inspectorOpen = false
            }
        }
    }

    Popup {
        id: newChatPopup
        parent: Overlay.overlay
        x: window.sidebarOpen ? 12 : 56
        y: window.headerHeight + 50
        width: 230
        height: 112
        padding: 8
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: Column {
            spacing: 3
            ChatTextButton {
                width: parent.width
                height: 44
                text: "Local project"
                enabled: chatController.hasProject
                onClicked: {
                    chatController.startNewChat(false)
                    newChatPopup.close()
                    composer.focusComposer()
                }
            }
            ChatTextButton {
                width: parent.width
                height: 44
                text: "Isolated worktree"
                enabled: chatController.hasProject
                onClicked: {
                    chatController.startNewChat(true)
                    newChatPopup.close()
                    composer.focusComposer()
                }
            }
        }
        background: Rectangle {
            radius: 13
            color: "#161619"
            border.width: 1
            border.color: "#34343a"
        }
    }

    Popup {
        id: threadActions
        parent: Overlay.overlay
        property string threadId: ""
        property bool threadPinned: false
        width: 184
        height: 177
        padding: 7
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: Column {
            spacing: 2
            ChatTextButton {
                width: parent.width
                height: 39
                text: threadActions.threadPinned ? "Unpin conversation" : "Pin conversation"
                onClicked: {
                    chatController.setThreadPinned(threadActions.threadId,
                                                   !threadActions.threadPinned)
                    threadActions.close()
                }
            }
            ChatTextButton {
                width: parent.width
                height: 39
                text: "Fork conversation"
                enabled: !chatController.busy
                onClicked: {
                    chatController.forkThread(threadActions.threadId)
                    threadActions.close()
                }
            }
            ChatTextButton {
                width: parent.width
                height: 39
                text: "Review changes"
                enabled: !chatController.busy
                onClicked: {
                    chatController.reviewThread(threadActions.threadId)
                    threadActions.close()
                }
            }
            ChatTextButton {
                width: parent.width
                height: 39
                text: "Archive conversation"
                enabled: !chatController.busy
                foregroundColor: "#d49a94"
                onClicked: {
                    chatController.archiveThread(threadActions.threadId)
                    threadActions.close()
                }
            }
        }
        background: Rectangle {
            radius: 12
            color: "#161619"
            border.width: 0
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Choose a Codex project"
        onAccepted: {
            chatController.projectPath = selectedFolder.toString()
            composer.focusComposer()
        }
    }

    FileDialog {
        id: fileDialog
        title: "Attach files"
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            for (var index = 0; index < selectedFiles.length; ++index)
                chatController.addAttachment(selectedFiles[index].toString())
            composer.focusComposer()
        }
    }

    Connections {
        target: chatController
        function onRequestProjectSelection() { folderDialog.open() }
        function onMessageSubmitted(clientMessageId) {
            transcript.anchorSubmittedMessage(clientMessageId)
        }
        function onTurnCompleted() {
            if (chatController.diffText.length > 0 && window.width >= 980)
                window.inspectorOpen = true
        }
    }
}
