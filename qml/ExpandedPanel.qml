pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property var colors
    property string uiFont: "Inter"
    property string iconFont: "Segoe Fluent Icons"
    property bool expanded: false
    property bool dragActive: false
    property bool reducedMotion: false
    property bool tilingFeedbackActive: false
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
    property int mediaTransitionDirection: 0
    property bool mediaScrubbing: false
    property real mediaScrubProgress: 0
    property date displayedCalendarDate: new Date()
    property string displayedCalendarToken: ""
    property int observedLayoutRevision: 0
    property int observedShortcutRevision: 0
    property int displayedFileCount: 0
    property bool fileShelfRetained: false
    property real tilingCommitProgress: 0
    readonly property bool claudePanelOpen: claude.requestActive
    readonly property date currentDate: displayedCalendarDate
    readonly property int currentDayIndex: currentDate.getDay()

    function weekDate(index) {
        return new Date(currentDate.getFullYear(), currentDate.getMonth(),
                        currentDate.getDate() - currentDayIndex + index)
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
    }

    Component.onCompleted: {
        syncMediaImmediately()
        displayedCalendarToken = controller.dateText
        displayedCalendarDate = new Date()
        observedLayoutRevision = tilingManager.layoutRevision
        observedShortcutRevision = tilingManager.shortcutRevision
        displayedFileCount = controller.droppedFileCount
        mediaMotionReady = true
    }

    Connections {
        target: controller
        function onMediaChanged() {
            if (!root.mediaMotionReady || root.reducedMotion) {
                root.syncMediaImmediately()
                return
            }
            if (controller.mediaTitle !== root.displayedMediaTitle
                    || controller.mediaArtist !== root.displayedMediaArtist) {
                root.pendingMediaTitle = controller.mediaTitle
                root.pendingMediaArtist = controller.mediaArtist
                metadataTransition.restart()
            }
            if (controller.mediaSource !== root.displayedMediaSource
                    || controller.mediaAppIconUrl !== root.displayedMediaAppIcon.toString()) {
                root.pendingMediaSource = controller.mediaSource
                root.pendingMediaAppIcon = controller.mediaAppIconUrl
                sourceTransition.restart()
            }
            if (controller.mediaArtworkUrl !== root.displayedArtwork.toString()) {
                root.pendingArtwork = controller.mediaArtworkUrl
                artworkTransition.restart()
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

    Connections {
        target: tilingManager
        function onStateChanged() {
            if (!root.reducedMotion && tilingStatusView.opacity > 0)
                tilingStatusPulse.restart()
        }
        function onInteractionChanged() {
            const layoutChanged = root.observedLayoutRevision !== tilingManager.layoutRevision
            const meaningfulChange = layoutChanged
                    || root.observedShortcutRevision !== tilingManager.shortcutRevision
            root.observedLayoutRevision = tilingManager.layoutRevision
            root.observedShortcutRevision = tilingManager.shortcutRevision
            if (meaningfulChange && !root.reducedMotion && tilingStatusView.opacity > 0)
                tilingStatusPulse.restart()
            if (layoutChanged && !root.reducedMotion)
                tilingCommit.restart()
        }
    }

    enabled: expanded && !dragActive
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
        enabled: !controller.timerPanelOpen && !root.claudePanelOpen
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
                radius: 14
                color: root.colors.raised
                clip: true
                transform: Translate { id: artworkShift }

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
            }

            Column {
                id: metadataColumn
                x: 104
                y: 4
                width: 174
                spacing: 2
                transform: Translate { id: metadataShift }

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
                x: 99
                y: 60
                spacing: 8

                IslandButton {
                    id: previousButton
                    width: 23
                    height: 23
                    iconOnly: true
                    bare: true
                    iconSource: Qt.resolvedUrl("../assets/icons/previous-light.svg")
                    iconSize: 12
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
                    iconSource: controller.mediaPlaying
                                ? Qt.resolvedUrl("../assets/icons/pause-light.svg")
                                : Qt.resolvedUrl("../assets/icons/play-light.svg")
                    iconSize: 13
                    accessibleName: controller.mediaPlaying ? "Pause" : "Play"
                    onClicked: controller.togglePlayback()
                }
                IslandButton {
                    id: nextButton
                    width: 23
                    height: 23
                    iconOnly: true
                    bare: true
                    iconSource: Qt.resolvedUrl("../assets/icons/next-light.svg")
                    iconSize: 12
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
                y: 91
                width: 174
                height: 17
                enabled: controller.mediaSeekable
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
                        color: controller.mediaArtworkAccent.length > 0
                               ? controller.mediaArtworkAccent : root.colors.text
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
                    visible: seekArea.containsMouse || seekArea.pressed
                    opacity: visible ? 1 : 0
                    x: Math.max(0, Math.min(parent.width - width, seekArea.mouseX - width / 2))
                    y: -17
                    width: seekPreviewText.implicitWidth + 10
                    height: 15
                    radius: 7.5
                    color: "#252527"
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
                radius: 14
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
        enabled: !controller.timerPanelOpen && !root.claudePanelOpen
        visible: opacity > 0.001
        opacity: enabled ? 1 : 0
        scale: enabled ? 1 : 0.94
        x: 390
        y: 17
        width: 175
        height: 106
        transform: Translate {
            y: clockPane.enabled ? 0 : -5
            Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }
        }
        Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }
        Accessible.name: controller.timeText + " " + controller.meridiemText
                         + ". " + (controller.networkName.length > 0
                                   ? controller.networkName + ". " : "")
                         + controller.networkStatus
                         + (controller.batteryAvailable ? ". " + controller.powerText : "")
                         + (tilingManager.enabled ? ". " + tilingManager.statusText : "")

        Row {
            id: clockRow
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 3

            RollingDigits {
                id: expandedClockDigits
                text: controller.timeText
                color: root.colors.text
                fontFamily: root.uiFont
                fontPixelSize: 25
                fontWeight: Font.DemiBold
                letterSpacing: -1
                reducedMotion: root.reducedMotion
                rollDirection: 1
            }
            MorphingLabel {
                anchors.bottom: expandedClockDigits.bottom
                anchors.bottomMargin: 4
                text: controller.meridiemText
                color: root.colors.secondary
                fontFamily: root.uiFont
                fontPixelSize: 9
                fontWeight: Font.DemiBold
                reducedMotion: root.reducedMotion
            }
            ClaudeBadge {
                anchors.verticalCenter: expandedClockDigits.verticalCenter
                anchors.verticalCenterOffset: 1
                iconSize: 17
                reducedMotion: root.reducedMotion
            }
        }

        Item {
            id: calendarView
            y: 48
            width: parent.width
            height: 62
            opacity: actionHover.hovered || tilingManager.adjusting
                     || root.tilingFeedbackActive ? 0 : 1
            scale: actionHover.hovered || tilingManager.adjusting
                   || root.tilingFeedbackActive ? 0.96 : 1
            transform: Translate { id: calendarShift }

            Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 110 } }
            Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Qt.formatDate(root.currentDate, "ddd").toUpperCase()
                color: root.colors.secondary
                font.family: root.uiFont
                font.pixelSize: 8
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1
            }

            Rectangle {
                id: todayHighlight
                x: (parent.width - 146) / 2 + root.currentDayIndex * 21 + 1.5
                y: 32
                width: 17
                height: 17
                radius: 9
                color: root.colors.text

                Behavior on x {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.content
                        easing.type: MotionTokens.easeOut
                    }
                }
            }

            Row {
                id: calendarDays
                y: 19
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 1

                Repeater {
                    model: 7

                    Item {
                        id: dayCell
                        required property int index
                        readonly property date calendarDate: root.weekDate(index)
                        width: 20
                        height: 39
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
                            text: Qt.formatDate(dayCell.calendarDate, "ddd").charAt(0).toUpperCase()
                            color: dayCell.index === root.currentDayIndex
                                   ? root.colors.secondary : root.colors.tertiary
                            font.family: root.uiFont
                            font.pixelSize: 7
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 13
                            width: 17
                            height: 17
                            radius: 9
                            color: "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: dayCell.calendarDate.getDate()
                                color: dayCell.index === root.currentDayIndex
                                       ? "#050505" : root.colors.secondary
                                font.family: root.uiFont
                                font.pixelSize: 7
                                font.weight: Font.DemiBold
                                font.features: { "tnum": 1 }
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
                        Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
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
                        Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
                    }
                    Rectangle {
                        x: 20
                        y: 16
                        width: 15
                        height: 9
                        radius: 3
                        color: tilingManager.previewSlot === 2
                               ? root.colors.accent : root.colors.tertiary
                        Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state } }
                    }
                    Rectangle {
                        visible: tilingManager.adjusting
                                 && tilingManager.interactionKind === "RESIZING"
                        x: Math.max(2, Math.min(parent.width - 3,
                            tilingManager.interactionProgress * parent.width))
                        y: 2
                        width: tilingManager.interactionConstrained ? 3 : 2
                        height: parent.height - 4
                        radius: 1
                        color: root.colors.accent
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
        }

        Item {
            id: actionReveal
            x: 2
            y: 49
            width: parent.width - 4
            height: 59

            HoverHandler { id: actionHover }

            Row {
                anchors.centerIn: parent
                spacing: 6
                opacity: actionHover.hovered ? 1 : 0
                scale: actionHover.hovered ? 1 : 0.94
                enabled: opacity > 0.5

                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 110 } }
                Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : 150; easing.type: Easing.OutBack } }

                IslandButton {
                    width: 30
                    height: 30
                    iconOnly: true
                    quiet: true
                    iconSource: Qt.resolvedUrl("../assets/icons/timer-light.svg")
                    invertedIconSource: Qt.resolvedUrl("../assets/icons/timer-dark.svg")
                    iconSize: 16
                    selected: controller.timerPanelOpen
                    accessibleName: "Open timer"
                    onClicked: controller.openTimer()
                }
                IslandButton {
                    width: 30
                    height: 30
                    iconOnly: true
                    quiet: true
                    iconSource: Qt.resolvedUrl("../assets/icons/grid-light.svg")
                    invertedIconSource: Qt.resolvedUrl("../assets/icons/grid-dark.svg")
                    iconSize: 16
                    selected: tilingManager.enabled
                    accessibleName: tilingManager.enabled
                                    ? "Disable Dwindle tiling"
                                    : "Enable Dwindle tiling"
                    onClicked: tilingManager.toggleEnabled()
                }
                IslandButton {
                    width: 30
                    height: 30
                    iconOnly: true
                    quiet: true
                    iconSource: Qt.resolvedUrl("../assets/icons/speaker-light.svg")
                    invertedIconSource: Qt.resolvedUrl("../assets/icons/speaker-muted-dark.svg")
                    iconSize: 16
                    selected: controller.muted
                    accessibleName: (controller.muted ? "Unmute" : "Mute") + ", " + controller.volume + "%"
                    onClicked: controller.toggleMute()
                }
                IslandButton {
                    width: 30
                    height: 30
                    iconOnly: true
                    quiet: true
                    iconSource: Qt.resolvedUrl("../assets/icons/pin-light.svg")
                    invertedIconSource: Qt.resolvedUrl("../assets/icons/pin-off-dark.svg")
                    iconSize: 15
                    selected: controller.pinned
                    rotatesOnSelection: true
                    accessibleName: controller.pinned ? "Unpin island" : "Keep island open"
                    onClicked: controller.togglePinned()
                }
                IslandButton {
                    width: 30
                    height: 30
                    iconOnly: true
                    bare: true
                    iconSource: Qt.resolvedUrl("../assets/icons/dismiss-light.svg")
                    iconSize: 15
                    accessibleName: "Collapse"
                    onClicked: {
                        controller.setPinned(false)
                        controller.setExpanded(false)
                    }
                }
            }
        }
    }

    Rectangle {
        id: fileShelfChip
        visible: !controller.timerPanelOpen && !root.claudePanelOpen
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

    ClaudePanel {
        id: claudePanelHost
        z: 30
        anchors.fill: parent
        enabled: root.claudePanelOpen
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
                PauseAnimation { duration: claudePanelHost.enabled && !root.reducedMotion ? 45 : 0 }
                NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.state; easing.type: MotionTokens.easeOut }
            }
        }
        Behavior on scale { NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }
    }
}
