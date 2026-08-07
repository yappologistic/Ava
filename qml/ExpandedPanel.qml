pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property var colors
    property string uiFont: "Inter"
    property string monoFont: "Geist Mono"
    property string iconFont: "Segoe Fluent Icons"
    property bool expanded: false
    property bool dragActive: false
    property bool reducedMotion: false
    property real morphProgress: 1
    property real shellCornerRadius: 28
    property bool tilingFeedbackActive: false
    property bool codexOpen: false
    property bool mediaMotionReady: false
    property string displayedMediaTitle: ""
    property string displayedMediaArtist: ""
    property string displayedMediaSource: ""
    property url displayedMediaAppIcon: ""
    property url displayedArtwork: ""
    property string pendingMediaTitle: ""
    property string pendingMediaArtist: ""
    property string pendingMediaSource: ""
    property url pendingMediaAppIcon: ""
    property url pendingArtwork: ""
    property string displayedArtworkAccent: ""
    property string pendingArtworkAccent: ""
    property int mediaTransitionDirection: 0
    property real mediaDetailsProgress: 1
    property bool mediaScrubbing: false
    property real mediaScrubProgress: 0
    property date displayedCalendarDate: new Date()
    property string displayedCalendarToken: ""
    property int observedLayoutRevision: 0
    property int observedShortcutRevision: 0
    property bool observedTilingConstraint: false
    property int displayedFileCount: 0
    property bool fileShelfRetained: false
    property real tilingCommitProgress: 0
    property real calendarAccentPulseProgress: 0
    property string observedArtworkAccent: ""
    property real utilityRevealProgress: qaUtilityMenu
                                         ? 1 : (actionHover.hovered ? 1 : 0)
    property int utilityMenuIndex: 2
    readonly property date currentDate: displayedCalendarDate
    readonly property int currentDayIndex: 3
    readonly property int calendarCellWidth: 31
    readonly property int calendarCellGap: 2
    readonly property int calendarSelectionWidth: 29
    readonly property int calendarSelectionHeight: 25
    readonly property bool calendarUsesMediaAccent: controller.mediaPlaying
                                                        && displayedArtworkAccent.length > 0
    readonly property color calendarActiveAccent: calendarUsesMediaAccent
                                                    ? displayedArtworkAccent
                                                    : colors.calendarAccent
    readonly property color calendarActiveSelection: calendarUsesMediaAccent
                                                       ? Qt.rgba(calendarActiveAccent.r,
                                                                 calendarActiveAccent.g,
                                                                 calendarActiveAccent.b, 0.24)
                                                       : colors.calendarSelection
    readonly property real revealProgress: reducedMotion
                                               ? (expanded ? 1 : 0)
                                               : Math.max(0, Math.min(1,
                                                   (morphProgress - 0.04) / 0.82))

    Behavior on utilityRevealProgress {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : MotionTokens.state
            easing.type: MotionTokens.easeOut
        }
    }

    function calendarDateAt(index) {
        return new Date(currentDate.getFullYear(), currentDate.getMonth(),
                        currentDate.getDate() + index - currentDayIndex)
    }

    function calendarLabelAlpha(index) {
        const distance = Math.abs(index - currentDayIndex)
        if (distance === 1)
            return 0.58
        if (distance === 2)
            return 0.34
        return 0.16
    }

    function calendarDateAlpha(index) {
        const distance = Math.abs(index - currentDayIndex)
        if (distance === 1)
            return 0.82
        if (distance === 2)
            return 0.56
        return 0.28
    }

    function calendarLabelSize(index) {
        return 11 - Math.abs(index - currentDayIndex)
    }

    function calendarDateSize(index) {
        return 15 - Math.abs(index - currentDayIndex)
    }

    function utilityIconOpacity(distance, hovered, selected, enabled) {
        if (!enabled)
            return 0.22
        if (hovered || selected || distance === 0)
            return 1
        return distance === 1 ? 0.64 : 0.34
    }

    function utilityIconScale(distance) {
        if (distance === 0)
            return 1
        return distance === 1 ? 0.92 : 0.84
    }

    function utilityMenuName(index) {
        const names = ["Codex", "Timer", "Dwindle", "Sound", "Pin", "Wallpaper"]
        return names[Math.max(0, Math.min(names.length - 1, index))]
    }

    function utilityMenuIsActive(index) {
        if (index === 1)
            return controller.timerPanelOpen
        if (index === 2)
            return tilingManager.enabled
        if (index === 3)
            return controller.muted
        if (index === 4)
            return controller.pinned
        if (index === 5)
            return controller.wallpaperPanelOpen
        return false
    }

    function activateUtility(index) {
        if (index === 0)
            codexBridge.setPanelOpen(true)
        else if (index === 1)
            controller.openTimer()
        else if (index === 2)
            tilingManager.toggleEnabled()
        else if (index === 3)
            controller.toggleMute()
        else if (index === 4)
            controller.togglePinned()
        else if (index === 5)
            controller.openWallpaperPanel()
    }

    function stepUtility(direction) {
        utilityMenuIndex = Math.max(0, Math.min(utilityMenu.count - 1,
                                                utilityMenuIndex + direction))
        utilityList.positionViewAtIndex(utilityMenuIndex, ListView.Contain)
    }

    function formatMediaTime(progress) {
        const totalSeconds = Math.max(0, Math.round(
            controller.mediaDurationMilliseconds * progress / 1000))
        const hours = Math.floor(totalSeconds / 3600)
        const minutes = Math.floor((totalSeconds % 3600) / 60)
        const seconds = totalSeconds % 60
        if (hours > 0)
            return hours + ":" + String(minutes).padStart(2, "0")
                + ":" + String(seconds).padStart(2, "0")
        return minutes + ":" + String(seconds).padStart(2, "0")
    }

    function syncMediaImmediately() {
        displayedMediaTitle = controller.mediaTitle
        displayedMediaArtist = controller.mediaArtist
        displayedMediaSource = controller.mediaSource
        displayedMediaAppIcon = controller.mediaAppIconUrl
        displayedArtwork = controller.mediaArtworkUrl
        displayedArtworkAccent = controller.mediaArtworkAccent
    }

    Component.onCompleted: {
        syncMediaImmediately()
        displayedCalendarToken = controller.dateText
        displayedCalendarDate = new Date()
        observedLayoutRevision = tilingManager.layoutRevision
        observedShortcutRevision = tilingManager.shortcutRevision
        observedTilingConstraint = tilingManager.interactionConstrained
        displayedFileCount = controller.droppedFileCount
        observedArtworkAccent = controller.mediaArtworkAccent
        displayedArtworkAccent = controller.mediaArtworkAccent
        mediaMotionReady = true
    }

    Connections {
        target: controller
        function onMediaChanged() {
            const accentChanged = root.observedArtworkAccent !== controller.mediaArtworkAccent
            const artworkChanged = controller.mediaArtworkUrl !== root.displayedArtwork.toString()
            root.observedArtworkAccent = controller.mediaArtworkAccent
            if (accentChanged) {
                root.pendingArtworkAccent = controller.mediaArtworkAccent
                if (root.reducedMotion) {
                    root.displayedArtworkAccent = root.pendingArtworkAccent
                } else {
                    accentHandoffDelay.restart()
                }
            }
            if (!root.mediaMotionReady || root.reducedMotion) {
                root.syncMediaImmediately()
                return
            }
            if (controller.mediaTitle !== root.displayedMediaTitle
                    || controller.mediaArtist !== root.displayedMediaArtist) {
                root.pendingMediaTitle = controller.mediaTitle
                root.pendingMediaArtist = controller.mediaArtist
                if (artworkChanged)
                    metadataHandoffDelay.restart()
                else
                    metadataTransition.restart()
            }
            if (controller.mediaSource !== root.displayedMediaSource
                    || controller.mediaAppIconUrl !== root.displayedMediaAppIcon.toString()) {
                root.pendingMediaSource = controller.mediaSource
                root.pendingMediaAppIcon = controller.mediaAppIconUrl
                if (artworkChanged)
                    sourceHandoffDelay.restart()
                else
                    sourceTransition.restart()
            }
            if (artworkChanged) {
                root.pendingArtwork = controller.mediaArtworkUrl
                artworkTransition.restart()
                mediaDetailsTrail.restart()
            }
        }
        function onClockChanged() {
            if (root.displayedCalendarToken.length === 0) {
                root.displayedCalendarToken = controller.dateText
                root.displayedCalendarDate = new Date()
            } else if (root.displayedCalendarToken !== controller.dateText) {
                if (root.reducedMotion) {
                    root.displayedCalendarToken = controller.dateText
                    root.displayedCalendarDate = new Date()
                } else {
                    calendarDateTransition.restart()
                }
            }
        }
        function onDroppedFilesChanged() {
            if (controller.droppedFileCount === 0 && root.displayedFileCount > 0) {
                root.fileShelfRetained = true
                if (root.reducedMotion) {
                    root.displayedFileCount = 0
                    root.fileShelfRetained = false
                } else {
                    fileShelfRemoval.restart()
                }
            } else {
                root.displayedFileCount = controller.droppedFileCount
                root.fileShelfRetained = false
                if (!root.reducedMotion && controller.droppedFileCount > 0)
                    fileShelfLanding.restart()
            }
        }
        function onMediaSeekFinished(accepted, requestedProgress) {
            if (root.reducedMotion)
                return
            if (accepted)
                seekConfirmation.restart()
            else
                seekRejection.restart()
        }
        function onMediaCommandRejected(command) {
            if (command === "previous")
                previousButton.reject()
            else if (command === "next")
                nextButton.reject()
            else if (command === "playback")
                playbackButton.reject()
        }
    }

    Timer {
        id: metadataHandoffDelay
        interval: root.reducedMotion ? 0 : 38
        repeat: false
        onTriggered: metadataTransition.restart()
    }

    Timer {
        id: sourceHandoffDelay
        interval: root.reducedMotion ? 0 : 62
        repeat: false
        onTriggered: sourceTransition.restart()
    }

    Timer {
        id: accentHandoffDelay
        interval: root.reducedMotion ? 0 : 96
        repeat: false
        onTriggered: {
            root.displayedArtworkAccent = root.pendingArtworkAccent
            if (controller.mediaPlaying)
                calendarAccentPulse.restart()
        }
    }

    SequentialAnimation {
        id: mediaDetailsTrail
        NumberAnimation {
            target: root
            property: "mediaDetailsProgress"
            from: 1
            to: 0.72
            duration: root.reducedMotion ? 0 : MotionTokens.press
            easing.type: Easing.InCubic
        }
        PauseAnimation { duration: root.reducedMotion ? 0 : 34 }
        NumberAnimation {
            target: root
            property: "mediaDetailsProgress"
            to: 1
            duration: root.reducedMotion ? 0 : MotionTokens.content
            easing.type: MotionTokens.easeOut
        }
    }

    Connections {
        target: tilingManager
        function onStateChanged() {
            if (!root.reducedMotion && tilingStatusView.opacity > 0)
                tilingStatusPulse.restart()
        }
        function onInteractionChanged() {
            const layoutChanged = root.observedLayoutRevision !== tilingManager.layoutRevision
            const constraintEntered = !root.observedTilingConstraint
                    && tilingManager.interactionConstrained
            const meaningfulChange = layoutChanged
                    || root.observedShortcutRevision !== tilingManager.shortcutRevision
            root.observedLayoutRevision = tilingManager.layoutRevision
            root.observedShortcutRevision = tilingManager.shortcutRevision
            root.observedTilingConstraint = tilingManager.interactionConstrained
            if (meaningfulChange && !root.reducedMotion && tilingStatusView.opacity > 0)
                tilingStatusPulse.restart()
            if (layoutChanged && !root.reducedMotion)
                tilingCommit.restart()
            if (constraintEntered && !root.reducedMotion)
                constraintPulse.restart()
        }
    }

    enabled: expanded && !dragActive && !codexOpen
    opacity: enabled ? 1 : 0
    scale: enabled ? 1 : 0.975
    transformOrigin: Item.Top

    Behavior on opacity {
        SequentialAnimation {
            PauseAnimation { duration: root.enabled && !root.reducedMotion ? 45 : 0 }
            NumberAnimation { duration: root.reducedMotion ? 0 : 135; easing.type: Easing.OutCubic }
        }
    }

    Behavior on scale {
        NumberAnimation { duration: root.reducedMotion ? 0 : 180; easing.type: Easing.OutCubic }
    }

    // The reference is deliberately sparse: artwork and transport controls occupy
    // the left third, while the middle stays visually quiet.
    Item {
        id: mediaPane
        enabled: !controller.timerPanelOpen && !controller.wallpaperPanelOpen
        visible: opacity > 0.001
        opacity: enabled ? 1 : 0
        scale: enabled ? 1 : 0.94
        x: 18
        y: 22
        width: 292
        height: 110
        transform: Translate {
            y: mediaPane.enabled ? 0 : -5
            Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }
        }
        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }

        Item {
            anchors.fill: parent
            visible: controller.mediaAvailable

            Rectangle {
                id: artworkFrame
                width: 88
                height: 88
                radius: Math.min(width / 2, root.shellCornerRadius)
                color: root.colors.raised
                clip: true
                opacity: 0.56 + root.revealProgress * 0.44
                scale: 0.31 + root.revealProgress * 0.69
                transformOrigin: Item.Center
                transform: [
                    Translate { id: artworkShift },
                    Translate {
                        x: -4 * (1 - root.revealProgress)
                        y: -5 * (1 - root.revealProgress)
                    }
                ]

                Image {
                    id: artworkImage
                    anchors.fill: parent
                    source: root.displayedArtwork
                    visible: root.displayedArtwork.toString().length > 0
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    smooth: true
                    mipmap: true
                }

                Text {
                    anchors.centerIn: parent
                    visible: root.displayedArtwork.toString().length === 0
                    text: "\uE8D6"
                    color: root.colors.secondary
                    font.family: root.iconFont
                    font.pixelSize: 28
                }

                Rectangle {
                    anchors.fill: parent
                    color: "black"
                    opacity: root.mediaScrubbing ? 0.18 : 0
                    Behavior on opacity {
                        NumberAnimation {
                            duration: root.reducedMotion ? 0 : MotionTokens.state
                            easing.type: MotionTokens.easeOut
                        }
                    }
                }
            }

            Column {
                id: metadataColumn
                x: 104
                y: 4
                width: 174
                spacing: 2
                scale: 0.94 + root.revealProgress * 0.06
                transformOrigin: Item.Left
                transform: [
                    Translate { id: metadataShift },
                    Translate { x: 13 * (1 - root.revealProgress) }
                ]

                Text {
                    width: parent.width
                    text: root.displayedMediaTitle
                    color: root.colors.text
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: root.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: root.displayedMediaArtist.length > 0
                          ? root.displayedMediaArtist : root.displayedMediaSource
                    color: root.colors.secondary
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: root.uiFont
                    font.pixelSize: 8
                }
                Item {
                    id: mediaSourceRow
                    width: parent.width
                    height: 11
                    visible: root.displayedMediaArtist.length > 0
                             && root.displayedMediaSource.length > 0
                    transform: Translate { id: mediaSourceShift }

                    Image {
                        id: mediaAppIcon
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 9
                        height: 9
                        visible: root.displayedMediaAppIcon.toString().length > 0
                        source: root.displayedMediaAppIcon
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true
                        smooth: true
                        mipmap: true
                    }

                    Text {
                        anchors.left: mediaAppIcon.visible ? mediaAppIcon.right : parent.left
                        anchors.leftMargin: mediaAppIcon.visible ? 4 : 0
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.displayedMediaSource
                        color: root.colors.tertiary
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        font.family: root.uiFont
                        font.pixelSize: 8
                    }
                }
            }

            SequentialAnimation {
                id: artworkTransition
                ParallelAnimation {
                    NumberAnimation { target: artworkImage; property: "opacity"; to: 0.18; duration: MotionTokens.press }
                    NumberAnimation { target: artworkImage; property: "scale"; to: 0.955; duration: MotionTokens.press; easing.type: Easing.InCubic }
                    NumberAnimation {
                        target: artworkShift
                        property: "x"
                        to: -root.mediaTransitionDirection * 10
                        duration: MotionTokens.press
                        easing.type: Easing.InCubic
                    }
                }
                ScriptAction {
                    script: {
                        root.displayedArtwork = root.pendingArtwork
                        artworkShift.x = root.mediaTransitionDirection * 10
                    }
                }
                ParallelAnimation {
                    NumberAnimation { target: artworkImage; property: "opacity"; to: 1; duration: MotionTokens.content }
                    NumberAnimation { target: artworkImage; property: "scale"; to: 1; duration: MotionTokens.content; easing.type: MotionTokens.easeOut }
                    NumberAnimation { target: artworkShift; property: "x"; to: 0; duration: MotionTokens.content; easing.type: MotionTokens.easeOut }
                }
            }

            SequentialAnimation {
                id: metadataTransition
                ParallelAnimation {
                    NumberAnimation { target: metadataColumn; property: "opacity"; to: 0; duration: MotionTokens.press }
                    NumberAnimation {
                        target: metadataShift
                        property: "x"
                        to: root.mediaTransitionDirection === 0 ? -4 : -root.mediaTransitionDirection * 9
                        duration: MotionTokens.press
                        easing.type: Easing.InCubic
                    }
                }
                ScriptAction {
                    script: {
                        root.displayedMediaTitle = root.pendingMediaTitle
                        root.displayedMediaArtist = root.pendingMediaArtist
                        metadataShift.x = root.mediaTransitionDirection === 0
                            ? 5 : root.mediaTransitionDirection * 9
                    }
                }
                ParallelAnimation {
                    NumberAnimation { target: metadataColumn; property: "opacity"; to: 1; duration: MotionTokens.state }
                    NumberAnimation { target: metadataShift; property: "x"; to: 0; duration: MotionTokens.state; easing.type: MotionTokens.easeOut }
                }
                ScriptAction { script: root.mediaTransitionDirection = 0 }
            }

            SequentialAnimation {
                id: sourceTransition
                ParallelAnimation {
                    NumberAnimation { target: mediaSourceRow; property: "opacity"; to: 0; duration: MotionTokens.press }
                    NumberAnimation {
                        target: mediaSourceShift
                        property: "x"
                        to: root.mediaTransitionDirection === 0 ? -5 : -root.mediaTransitionDirection * 7
                        duration: MotionTokens.press
                        easing.type: Easing.InCubic
                    }
                }
                ScriptAction {
                    script: {
                        root.displayedMediaSource = root.pendingMediaSource
                        root.displayedMediaAppIcon = root.pendingMediaAppIcon
                        mediaSourceShift.x = root.mediaTransitionDirection === 0
                            ? 6 : root.mediaTransitionDirection * 7
                    }
                }
                ParallelAnimation {
                    NumberAnimation { target: mediaSourceRow; property: "opacity"; to: 1; duration: MotionTokens.state }
                    NumberAnimation { target: mediaSourceShift; property: "x"; to: 0; duration: MotionTokens.state; easing.type: MotionTokens.easeOut }
                }
            }

            Row {
                id: transportControls
                x: 99
                y: 56
                spacing: 8
                opacity: Math.max(0, Math.min(1, (root.revealProgress - 0.20) / 0.80))
                         * root.mediaDetailsProgress
                scale: (0.88 + root.revealProgress * 0.12)
                       * (0.96 + root.mediaDetailsProgress * 0.04)
                transformOrigin: Item.Left

                IslandButton {
                    id: previousButton
                    width: 24
                    height: 24
                    iconOnly: true
                    bare: true
                    transportKind: "previous"
                    iconSize: 14
                    pressShiftX: -2
                    enabled: controller.mediaCanPrevious
                    accessibleName: "Previous track"
                    onClicked: {
                        root.mediaTransitionDirection = -1
                        controller.previousTrack()
                    }
                }
                IslandButton {
                    id: playbackButton
                    width: 24
                    height: 24
                    iconOnly: true
                    bare: true
                    transportKind: "playback"
                    playbackPlaying: controller.mediaPlaying
                    iconSize: 14
                    accessibleName: controller.mediaPlaying ? "Pause" : "Play"
                    onClicked: controller.togglePlayback()
                }
                IslandButton {
                    id: nextButton
                    width: 24
                    height: 24
                    iconOnly: true
                    bare: true
                    transportKind: "next"
                    iconSize: 14
                    pressShiftX: 2
                    enabled: controller.mediaCanNext
                    accessibleName: "Next track"
                    onClicked: {
                        root.mediaTransitionDirection = 1
                        controller.nextTrack()
                    }
                }
            }

            Item {
                id: seekSurface
                x: 104
                y: 80
                width: 174
                height: 17
                enabled: controller.mediaSeekable
                opacity: Math.max(0, Math.min(1, (root.revealProgress - 0.28) / 0.72))
                         * root.mediaDetailsProgress
                scale: (0.90 + root.revealProgress * 0.10)
                       * (0.96 + root.mediaDetailsProgress * 0.04)
                transformOrigin: Item.Left
                transform: Translate { id: seekFeedbackShift }

                Rectangle {
                    id: seekTrack
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: seekArea.containsMouse || seekArea.pressed ? 4 : 2
                    radius: height / 2
                    color: root.colors.raised

                    Rectangle {
                        width: parent.width * (root.mediaScrubbing
                                               ? root.mediaScrubProgress
                                               : controller.mediaProgress)
                        height: parent.height
                        radius: height / 2
                        color: root.displayedArtworkAccent.length > 0
                               ? root.displayedArtworkAccent : root.colors.text
                        Behavior on width {
                            enabled: !seekArea.pressed
                            NumberAnimation { duration: root.reducedMotion ? 0 : 130; easing.type: Easing.OutCubic }
                        }
                        Behavior on color {
                            ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.content }
                        }
                    }
                }

                Rectangle {
                    id: seekThumb
                    anchors.verticalCenter: parent.verticalCenter
                    x: Math.max(0, Math.min(parent.width - width,
                        (root.mediaScrubbing ? root.mediaScrubProgress
                                             : controller.mediaProgress) * parent.width - width / 2))
                    width: seekArea.containsMouse || seekArea.pressed ? 8 : 0
                    height: width
                    radius: width / 2
                    color: root.colors.text
                    scale: 1
                    Behavior on width { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover } }
                }

                SequentialAnimation {
                    id: seekConfirmation
                    NumberAnimation { target: seekThumb; property: "scale"; from: 1; to: 0.72; duration: MotionTokens.press; easing.type: Easing.InCubic }
                    NumberAnimation { target: seekThumb; property: "scale"; to: 1; duration: MotionTokens.directSettle; easing.type: MotionTokens.settle }
                }

                SequentialAnimation {
                    id: seekRejection
                    NumberAnimation { target: seekFeedbackShift; property: "x"; from: 0; to: -2; duration: 45 }
                    NumberAnimation { target: seekFeedbackShift; property: "x"; to: 2; duration: 65 }
                    NumberAnimation { target: seekFeedbackShift; property: "x"; to: 0; duration: 70; easing.type: Easing.OutCubic }
                }

                Rectangle {
                    id: seekPreview
                    visible: opacity > 0.001
                    opacity: seekArea.containsMouse || seekArea.pressed ? 1 : 0
                    x: Math.max(0, Math.min(parent.width - width, seekArea.mouseX - width / 2))
                    y: seekArea.containsMouse || seekArea.pressed ? -18 : -14
                    width: seekPreviewText.implicitWidth + 10
                    height: 15
                    radius: 7.5
                    color: "#252527"
                    scale: seekArea.containsMouse || seekArea.pressed ? 1 : 0.92
                    Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover } }
                    Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover; easing.type: MotionTokens.easeOut } }
                    Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover; easing.type: MotionTokens.settle } }
                    Text {
                        id: seekPreviewText
                        anchors.centerIn: parent
                        text: root.formatMediaTime(root.mediaScrubbing
                                                   ? root.mediaScrubProgress
                                                   : Math.max(0, Math.min(1, seekArea.mouseX / seekSurface.width)))
                        color: root.colors.text
                        font.family: root.uiFont
                        font.pixelSize: 8
                        font.features: { "tnum": 1 }
                    }
                }

                MouseArea {
                    id: seekArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: controller.mediaSeekable
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    function updateProgress(pointerX) {
                        root.mediaScrubProgress = Math.max(0, Math.min(1, pointerX / width))
                    }
                    onPressed: function(mouse) {
                        root.mediaScrubbing = true
                        updateProgress(mouse.x)
                    }
                    onPositionChanged: function(mouse) {
                        if (pressed)
                            updateProgress(mouse.x)
                    }
                    onReleased: function(mouse) {
                        updateProgress(mouse.x)
                        controller.seekMedia(root.mediaScrubProgress)
                        root.mediaScrubbing = false
                    }
                    onCanceled: root.mediaScrubbing = false
                }
            }
        }

        Item {
            anchors.fill: parent
            visible: !controller.mediaAvailable

            Rectangle {
                width: 88
                height: 88
                radius: Math.min(width / 2, root.shellCornerRadius)
                color: root.colors.raised

                Text {
                    anchors.centerIn: parent
                    text: "\uE8D6"
                    color: root.colors.secondary
                    font.family: root.iconFont
                    font.pixelSize: 28
                }
            }
            Text {
                x: 104
                y: 20
                text: "Nothing playing"
                color: root.colors.text
                font.family: root.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Text {
                x: 104
                y: 42
                width: 172
                wrapMode: Text.WordWrap
                text: "Start media in a Windows app"
                color: root.colors.tertiary
                font.family: root.uiFont
                font.pixelSize: 9
            }
        }
    }

    // Clock and a real current-week calendar reproduce the small right cluster in
    // the source. Hovering this cluster crossfades to utility actions.
    Item {
        id: clockPane
        enabled: !controller.timerPanelOpen && !controller.wallpaperPanelOpen
        visible: opacity > 0.001
        opacity: enabled ? 1 : 0
        scale: enabled ? 1 : 0.94
        x: 360
        y: 17
        width: 175
        height: 106
        transform: Translate {
            y: clockPane.enabled ? 0 : -5
            Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }
        }
        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }
        Accessible.name: controller.timeText
                         + ". " + (controller.networkName.length > 0
                                   ? controller.networkName + ". " : "")
                         + controller.networkStatus
                         + (controller.batteryAvailable ? ". " + controller.powerText : "")
                         + (tilingManager.enabled ? ". " + tilingManager.statusText : "")

        RollingDigits {
            id: expandedClockDigits
            anchors.horizontalCenter: parent.horizontalCenter
            text: controller.timeText
            color: root.colors.text
            fontFamily: root.uiFont
            fontPixelSize: 23
            fontWeight: Font.Medium
            letterSpacing: -0.5
            reducedMotion: root.reducedMotion
            rollDirection: 1
            scale: 0.72 + root.revealProgress * 0.28
            transform: Translate { y: -7 * (1 - root.revealProgress) }
        }

        Item {
            id: calendarView
            y: 48
            width: parent.width
            height: 62
            opacity: tilingManager.adjusting || root.tilingFeedbackActive
                     ? 0 : 1 - root.utilityRevealProgress
            scale: 1 - root.utilityRevealProgress * 0.09
            transform: Translate {
                id: calendarShift
                y: root.utilityRevealProgress * 3
            }

            Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 110 } }
            Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }

            Row {
                id: calendarDays
                y: 3
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: root.calendarCellGap

                Repeater {
                    model: 7

                    Item {
                        id: dayCell
                        required property int index
                        readonly property date calendarDate: root.calendarDateAt(index)
                        width: root.calendarCellWidth
                        height: 44
                        opacity: calendarView.opacity > 0.5 ? 1 : 0
                        scale: calendarView.opacity > 0.5 ? 1 : 0.94

                        Behavior on opacity {
                            SequentialAnimation {
                                PauseAnimation {
                                    duration: root.reducedMotion ? 0 : dayCell.index * 14
                                }
                                NumberAnimation {
                                    duration: root.reducedMotion ? 0 : MotionTokens.state
                                    easing.type: MotionTokens.easeOut
                                }
                            }
                        }
                        Behavior on scale {
                            NumberAnimation {
                                duration: root.reducedMotion ? 0 : MotionTokens.state
                                easing.type: MotionTokens.easeOut
                            }
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: dayCell.index === root.currentDayIndex
                                  ? Qt.formatDate(dayCell.calendarDate, "ddd").toUpperCase()
                                  : Qt.formatDate(dayCell.calendarDate, "ddd").charAt(0).toUpperCase()
                            color: dayCell.index === root.currentDayIndex
                                   ? Qt.rgba(root.colors.text.r,
                                             root.colors.text.g,
                                             root.colors.text.b, 0.78)
                                   : Qt.rgba(root.colors.tertiary.r,
                                             root.colors.tertiary.g,
                                             root.colors.tertiary.b,
                                             root.calendarLabelAlpha(dayCell.index))
                            font.family: root.uiFont
                            font.pixelSize: root.calendarLabelSize(dayCell.index)
                            font.weight: Font.DemiBold
                            font.letterSpacing: dayCell.index === root.currentDayIndex ? 0.4 : 0
                        }
                        Rectangle {
                            id: selectedDatePlate
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 13
                            width: root.calendarSelectionWidth
                            height: root.calendarSelectionHeight
                            radius: 6
                            color: dayCell.index === root.currentDayIndex
                                   ? root.calendarActiveSelection
                                   : "transparent"
                            scale: dayCell.index === root.currentDayIndex
                                   ? 1 + root.calendarAccentPulseProgress * 0.035 : 1

                            Behavior on color {
                                ColorAnimation {
                                    duration: root.reducedMotion ? 0 : MotionTokens.content
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                text: dayCell.calendarDate.getDate()
                                color: dayCell.index === root.currentDayIndex
                                       ? root.calendarActiveAccent
                                       : Qt.rgba(root.colors.tertiary.r,
                                                 root.colors.tertiary.g,
                                                 root.colors.tertiary.b,
                                                 root.calendarDateAlpha(dayCell.index))
                                font.family: root.uiFont
                                font.pixelSize: root.calendarDateSize(dayCell.index)
                                font.weight: Font.Medium
                                font.features: { "tnum": 1 }

                                Behavior on color {
                                    ColorAnimation {
                                        duration: root.reducedMotion ? 0 : MotionTokens.content
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        SequentialAnimation {
            id: calendarDateTransition
            ParallelAnimation {
                NumberAnimation { target: calendarView; property: "opacity"; to: 0; duration: MotionTokens.press }
                NumberAnimation { target: calendarShift; property: "x"; to: -8; duration: MotionTokens.press; easing.type: Easing.InCubic }
            }
            ScriptAction {
                script: {
                    root.displayedCalendarToken = controller.dateText
                    root.displayedCalendarDate = new Date()
                    calendarShift.x = 8
                }
            }
            ParallelAnimation {
                NumberAnimation { target: calendarView; property: "opacity"; to: 1; duration: MotionTokens.state }
                NumberAnimation { target: calendarShift; property: "x"; to: 0; duration: MotionTokens.state; easing.type: MotionTokens.easeOut }
            }
        }

        SequentialAnimation {
            id: calendarAccentPulse
            NumberAnimation {
                target: root
                property: "calendarAccentPulseProgress"
                from: 0
                to: 1
                duration: MotionTokens.press
                easing.type: MotionTokens.easeOut
            }
            NumberAnimation {
                target: root
                property: "calendarAccentPulseProgress"
                to: 0
                duration: MotionTokens.directSettle
                easing.type: MotionTokens.settle
            }
        }

        Item {
            id: tilingStatusView
            y: 48
            width: parent.width
            height: 62
            opacity: (tilingManager.adjusting || root.tilingFeedbackActive)
                     && !actionHover.hovered ? 1 : 0
            scale: (tilingManager.adjusting || root.tilingFeedbackActive)
                   && !actionHover.hovered ? 1 : 0.96

            Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 110 } }
            Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }

            Row {
                id: tilingStatusContent
                anchors.centerIn: parent
                spacing: 10

                Item {
                    width: 38
                    height: 28
                    scale: 1 + root.tilingCommitProgress * 0.045

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: "transparent"
                        border.width: 1
                        border.color: root.tilingCommitProgress > 0
                                      || tilingManager.interactionConstrained
                                      ? root.colors.accent
                                      : (tilingManager.previewSlot >= 0
                                         ? root.colors.text : root.colors.divider)
                        Behavior on border.color { ColorAnimation { duration: MotionTokens.hover } }
                    }
                    Rectangle {
                        id: firstTilePreview
                        x: 3
                        y: 3
                        width: 14
                        height: 22
                        radius: 3
                        color: tilingManager.previewSlot === 0
                               ? root.colors.accent
                               : (tilingManager.layoutRevision % 2 === 0
                                  ? root.colors.text : root.colors.secondary)
                        opacity: tilingManager.previewSlot < 0
                                 || tilingManager.previewSlot === 0 ? 1 : 0.28
                        scale: tilingManager.previewSlot === 0
                               ? 1.06 : (tilingManager.previewSlot >= 0 ? 0.94 : 1)
                        transform: Translate {
                            x: root.tilingCommitProgress * 4
                            y: -root.tilingCommitProgress * 2
                        }
                        Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
                        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover } }
                        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover; easing.type: MotionTokens.settle } }
                    }
                    Rectangle {
                        id: secondTilePreview
                        x: 20
                        y: 3
                        width: 15
                        height: 10
                        radius: 3
                        color: tilingManager.previewSlot === 1
                               ? root.colors.accent
                               : (tilingManager.layoutRevision % 2 === 0
                                  ? root.colors.secondary : root.colors.text)
                        opacity: tilingManager.previewSlot < 0
                                 || tilingManager.previewSlot === 1 ? 1 : 0.28
                        scale: tilingManager.previewSlot === 1
                               ? 1.06 : (tilingManager.previewSlot >= 0 ? 0.94 : 1)
                        transform: Translate {
                            x: -root.tilingCommitProgress * 4
                            y: root.tilingCommitProgress * 2
                        }
                        Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
                        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover } }
                        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover; easing.type: MotionTokens.settle } }
                    }
                    Rectangle {
                        id: thirdTilePreview
                        x: 20
                        y: 16
                        width: 15
                        height: 9
                        radius: 3
                        color: tilingManager.previewSlot === 2
                               ? root.colors.accent : root.colors.tertiary
                        opacity: tilingManager.previewSlot < 0
                                 || tilingManager.previewSlot === 2 ? 1 : 0.28
                        scale: tilingManager.previewSlot === 2
                               ? 1.06 : (tilingManager.previewSlot >= 0 ? 0.94 : 1)
                        transform: Translate { y: -root.tilingCommitProgress * 2 }
                        Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
                        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover } }
                        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover; easing.type: MotionTokens.settle } }
                    }
                    Rectangle {
                        id: resizeDivider
                        visible: tilingManager.adjusting
                                 && tilingManager.interactionKind === "RESIZING"
                        x: Math.max(2, Math.min(parent.width - 3,
                            tilingManager.interactionProgress * parent.width))
                        y: 2
                        width: tilingManager.interactionConstrained ? 3 : 2
                        height: parent.height - 4
                        radius: 1
                        color: root.colors.accent
                        transformOrigin: Item.Center
                        Behavior on x {
                            enabled: !root.reducedMotion
                            NumberAnimation { duration: 45; easing.type: Easing.OutCubic }
                        }
                        Behavior on width { NumberAnimation { duration: MotionTokens.hover } }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: tilingManager.interactionConstrained ? -1 : 0
                            width: tilingManager.interactionConstrained ? 7 : 4
                            height: 2
                            radius: 1
                            color: parent.color
                            Behavior on width { NumberAnimation { duration: MotionTokens.hover } }
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: tilingManager.interactionConstrained ? -1 : 0
                            width: tilingManager.interactionConstrained ? 7 : 4
                            height: 2
                            radius: 1
                            color: parent.color
                            Behavior on width { NumberAnimation { duration: MotionTokens.hover } }
                        }
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        text: "DWINDLE"
                        color: root.colors.text
                        font.family: root.uiFont
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Text {
                        text: tilingManager.adjusting
                              ? (tilingManager.interactionConstrained
                                 ? "MIN SIZE" : tilingManager.interactionKind)
                              : (!tilingManager.enabled
                              ? "OFF"
                              : (tilingManager.tiledWindowCount === 0
                              ? "READY"
                              : (tilingManager.tiledWindowCount === 1
                                 ? "1 WINDOW"
                                 : tilingManager.tiledWindowCount + " WINDOWS")))
                        color: tilingManager.interactionConstrained
                               ? root.colors.accent : root.colors.tertiary
                        Behavior on color { ColorAnimation { duration: MotionTokens.hover } }
                        font.family: root.uiFont
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                        font.features: { "tnum": 1 }
                    }
                }
            }

            SequentialAnimation {
                id: tilingStatusPulse
                NumberAnimation {
                    target: tilingStatusContent
                    property: "scale"
                    from: 0.94
                    to: 1.025
                    duration: MotionTokens.press
                    easing.type: MotionTokens.easeOut
                }
                NumberAnimation {
                    target: tilingStatusContent
                    property: "scale"
                    to: 1
                    duration: MotionTokens.hover
                    easing.type: MotionTokens.easeOut
                }
            }

            SequentialAnimation {
                id: tilingCommit
                NumberAnimation {
                    target: root
                    property: "tilingCommitProgress"
                    from: 0
                    to: 1
                    duration: MotionTokens.press
                    easing.type: MotionTokens.easeOut
                }
                NumberAnimation {
                    target: root
                    property: "tilingCommitProgress"
                    to: 0
                    duration: MotionTokens.directSettle
                    easing.type: MotionTokens.easeOut
                }
            }

            SequentialAnimation {
                id: constraintPulse
                NumberAnimation {
                    target: resizeDivider
                    property: "scale"
                    from: 1
                    to: 1.35
                    duration: MotionTokens.press
                    easing.type: MotionTokens.easeOut
                }
                NumberAnimation {
                    target: resizeDivider
                    property: "scale"
                    to: 0.94
                    duration: 70
                    easing.type: Easing.InOutCubic
                }
                NumberAnimation {
                    target: resizeDivider
                    property: "scale"
                    to: 1
                    duration: MotionTokens.hover
                    easing.type: MotionTokens.settle
                }
            }
        }

        Item {
            id: actionReveal
            x: 2
            y: 49
            width: parent.width - 4
            height: 59

            HoverHandler { id: actionHover }

            ListModel {
                id: utilityMenu
                ListElement { utilityIndex: 0; iconPath: "../assets/icons/codex-terminal-light.svg"; invertedPath: ""; usesDwindle: false }
                ListElement { utilityIndex: 1; iconPath: "../assets/icons/timer-light.svg"; invertedPath: "../assets/icons/timer-dark.svg"; usesDwindle: false }
                ListElement { utilityIndex: 2; iconPath: ""; invertedPath: ""; usesDwindle: true }
                ListElement { utilityIndex: 3; iconPath: "../assets/icons/speaker-light.svg"; invertedPath: "../assets/icons/speaker-muted-dark.svg"; usesDwindle: false }
                ListElement { utilityIndex: 4; iconPath: "../assets/icons/pin-light.svg"; invertedPath: "../assets/icons/pin-off-dark.svg"; usesDwindle: false }
                ListElement { utilityIndex: 5; iconPath: "../assets/icons/wallpaper-light.svg"; invertedPath: "../assets/icons/wallpaper-dark.svg"; usesDwindle: false }
            }

            Item {
                id: utilityCarousel
                anchors.fill: parent
                opacity: root.utilityRevealProgress
                scale: 0.9 + root.utilityRevealProgress * 0.1
                enabled: opacity > 0.5
                transform: Translate { y: 4 * (1 - root.utilityRevealProgress) }

                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 110 } }
                Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 150; easing.type: MotionTokens.settle } }

                IslandButton {
                    id: utilityPrevious
                    x: 0
                    y: 1
                    width: 17
                    height: 27
                    iconOnly: true
                    bare: true
                    glyph: "\uE76B"
                    enabled: root.utilityMenuIndex > 0
                    opacity: enabled ? 0.54 : 0.15
                    accessibleName: "Previous menu item"
                    onClicked: root.stepUtility(-1)
                }

                ListView {
                    id: utilityList
                    x: 17
                    y: 1
                    width: 137
                    height: 28
                    orientation: ListView.Horizontal
                    spacing: 3
                    clip: true
                    interactive: false
                    boundsBehavior: Flickable.StopAtBounds
                    model: utilityMenu
                    currentIndex: root.utilityMenuIndex

                    delegate: IslandButton {
                        id: utilityDelegate
                        required property int index
                        required property int utilityIndex
                        required property string iconPath
                        required property string invertedPath
                        required property bool usesDwindle
                        readonly property int menuDistance: Math.abs(index - root.utilityMenuIndex)
                        width: 25
                        height: 25
                        iconOnly: true
                        quiet: true
                        dwindleMorph: usesDwindle
                        iconSource: iconPath.length > 0 ? Qt.resolvedUrl(iconPath) : ""
                        invertedIconSource: invertedPath.length > 0 ? Qt.resolvedUrl(invertedPath) : ""
                        iconSize: usesDwindle ? 15 : 14
                        selected: root.utilityMenuIsActive(utilityIndex)
                        baseScale: root.utilityIconScale(menuDistance)
                                   * (0.90 + root.utilityRevealProgress * 0.10)
                        opacity: root.utilityIconOpacity(menuDistance, hovered,
                                                         selected || index === root.utilityMenuIndex,
                                                         enabled)
                        rotatesOnSelection: utilityIndex === 4
                        accessibleName: root.utilityMenuName(utilityIndex)
                        onClicked: {
                            root.utilityMenuIndex = utilityIndex
                            utilityList.positionViewAtIndex(utilityIndex, ListView.Contain)
                            root.activateUtility(utilityIndex)
                        }
                    }
                }

                IslandButton {
                    id: utilityNext
                    x: 154
                    y: 1
                    width: 17
                    height: 27
                    iconOnly: true
                    bare: true
                    glyph: "\uE76C"
                    enabled: root.utilityMenuIndex < utilityMenu.count - 1
                    opacity: enabled ? 0.54 : 0.15
                    accessibleName: "Next menu item"
                    onClicked: root.stepUtility(1)
                }

                MorphingLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 34
                    width: parent.width - 34
                    text: root.utilityMenuName(root.utilityMenuIndex)
                    color: root.colors.tertiary
                    fontFamily: root.uiFont
                    fontPixelSize: 8
                    fontWeight: Font.DemiBold
                    letterSpacing: 0.45
                    horizontalAlignment: Text.AlignHCenter
                    reducedMotion: root.reducedMotion
                }

                WheelHandler {
                    onWheel: function(event) {
                        if (Math.abs(event.angleDelta.y) < 20)
                            return
                        root.stepUtility(event.angleDelta.y < 0 ? 1 : -1)
                        event.accepted = true
                    }
                }
            }
        }
    }

    Rectangle {
        id: fileShelfChip
        visible: !controller.timerPanelOpen && !controller.wallpaperPanelOpen
                 && (controller.droppedFileCount > 0 || root.fileShelfRetained)
        x: 318
        y: 75
        width: 62
        height: 28
        radius: 14
        color: fileShelfHover.hovered ? "#252527" : root.colors.raised
        scale: fileShelfTap.pressed ? 0.97 : (fileShelfHover.hovered ? 1.018 : 1)
        Accessible.name: root.displayedFileCount + " files in shelf. Show latest file"
        Accessible.role: Accessible.Button

        HoverHandler { id: fileShelfHover }
        TapHandler {
            id: fileShelfTap
            onTapped: {
                if (!root.reducedMotion)
                    fileShelfReveal.restart()
                controller.revealLastDroppedFile()
            }
        }

        Row {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: fileShelfHover.hovered ? -5 : 0
            spacing: 5
            Behavior on anchors.horizontalCenterOffset {
                NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover; easing.type: MotionTokens.easeOut }
            }
            Text {
                text: "\uE8B7"
                color: root.colors.accent
                font.family: root.iconFont
                font.pixelSize: 12
            }
            RollingDigits {
                anchors.verticalCenter: parent.children[0].verticalCenter
                text: String(root.displayedFileCount)
                color: root.colors.text
                fontFamily: root.uiFont
                fontPixelSize: 9
                fontWeight: Font.DemiBold
                reducedMotion: root.reducedMotion
                rollDirection: controller.droppedFileCount < root.displayedFileCount ? -1 : 1
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            text: "\u00d7"
            color: root.colors.secondary
            opacity: fileShelfHover.hovered ? 1 : 0
            scale: fileShelfHover.hovered ? 1 : 0.7
            font.family: root.uiFont
            font.pixelSize: 13
            font.weight: Font.Medium
            Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover } }
            Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover; easing.type: MotionTokens.settle } }

            MouseArea {
                anchors.centerIn: parent
                width: 22
                height: 24
                enabled: fileShelfHover.hovered
                cursorShape: Qt.PointingHandCursor
                onClicked: fileShelfClearCommit.restart()
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * fileRevealProgress.value
            height: parent.height
            radius: parent.radius
            color: root.colors.accent
            opacity: 0.13
            clip: true
        }

        QtObject { id: fileRevealProgress; property real value: 0 }

        Behavior on color { ColorAnimation { duration: MotionTokens.hover } }
        Behavior on scale { NumberAnimation { duration: MotionTokens.hover; easing.type: MotionTokens.easeOut } }

        SequentialAnimation {
            id: fileShelfLanding
            NumberAnimation { target: fileShelfChip; property: "scale"; from: 0.88; to: 1.06; duration: MotionTokens.press; easing.type: MotionTokens.easeOut }
            NumberAnimation { target: fileShelfChip; property: "scale"; to: 1; duration: MotionTokens.directSettle; easing.type: MotionTokens.settle }
        }

        SequentialAnimation {
            id: fileShelfReveal
            NumberAnimation { target: fileRevealProgress; property: "value"; from: 0; to: 1; duration: MotionTokens.content; easing.type: MotionTokens.easeOut }
            NumberAnimation { target: fileRevealProgress; property: "value"; to: 0; duration: MotionTokens.state; easing.type: Easing.InCubic }
        }

        SequentialAnimation {
            id: fileShelfClearCommit
            NumberAnimation { target: fileShelfChip; property: "scale"; to: 0.94; duration: root.reducedMotion ? 0 : MotionTokens.press; easing.type: Easing.InCubic }
            ScriptAction { script: controller.clearDroppedFiles() }
        }

        SequentialAnimation {
            id: fileShelfRemoval
            ParallelAnimation {
                NumberAnimation { target: fileShelfChip; property: "opacity"; to: 0; duration: MotionTokens.state; easing.type: Easing.InCubic }
                NumberAnimation { target: fileShelfChip; property: "scale"; to: 0.72; duration: MotionTokens.state; easing.type: Easing.InCubic }
            }
            ScriptAction {
                script: {
                    root.displayedFileCount = 0
                    root.fileShelfRetained = false
                    fileShelfChip.opacity = 1
                    fileShelfChip.scale = 1
                }
            }
        }
    }

    TimerPanel {
        id: timerPanelHost
        z: 20
        anchors.fill: parent
        enabled: controller.timerPanelOpen
        visible: opacity > 0.001
        opacity: enabled ? 1 : 0
        scale: enabled ? 1 : 0.94
        colors: root.colors
        uiFont: root.uiFont
        iconFont: root.iconFont
        reducedMotion: root.reducedMotion
        transformOrigin: Item.Top
        Behavior on opacity {
            SequentialAnimation {
                PauseAnimation { duration: timerPanelHost.enabled && !root.reducedMotion ? 55 : 0 }
                NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state; easing.type: MotionTokens.easeOut }
            }
        }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }
    }

    WallpaperPanel {
        id: wallpaperPanelHost
        z: 21
        anchors.fill: parent
        open: controller.wallpaperPanelOpen
        enabled: open
        visible: opacity > 0.001
        opacity: enabled ? 1 : 0
        scale: enabled ? 1 : 0.955
        colors: root.colors
        uiFont: root.uiFont
        iconFont: root.iconFont
        reducedMotion: root.reducedMotion
        shellCornerRadius: root.shellCornerRadius
        transformOrigin: Item.Top
        Behavior on opacity {
            SequentialAnimation {
                PauseAnimation { duration: wallpaperPanelHost.enabled && !root.reducedMotion ? 72 : 0 }
                NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut }
            }
        }
        Behavior on scale {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.reveal
                easing.type: MotionTokens.settle
            }
        }
    }
}
