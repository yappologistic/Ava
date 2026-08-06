import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Window 6.5

Window {
    id: window

    visible: true
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.NoDropShadowWindowHint
    title: "Dynamic Island"

    readonly property string uiFont: "SN Pro"
    readonly property string iconFont: "Segoe Fluent Icons"
    readonly property int compactWidth: 150
    readonly property int compactHeight: 39
    // Measured from all 48,446 source frames: the stable open shell is about 4.5:1.
    readonly property int expandedWidth: Math.max(520, Math.min(584, Screen.width - 40))
    readonly property int expandedHeight: 128
    readonly property int dragWidth: Math.min(420, expandedWidth)
    readonly property int dragHeight: 116
    readonly property int canvasWidth: expandedWidth + 40
    readonly property int canvasHeight: expandedHeight + 10
    readonly property bool dragActive: dropTarget.containsDrag
    readonly property int islandTargetWidth: dragActive ? dragWidth
                                                        : (controller.expanded ? expandedWidth : compactWidth)
    readonly property int islandTargetHeight: dragActive ? dragHeight
                                                         : (controller.expanded ? expandedHeight : compactHeight)

    property real islandVisualWidth: compactWidth
    property real islandVisualHeight: compactHeight
    property real islandWidthVelocity: 0
    property real islandHeightVelocity: 0
    property bool motionReady: false
    property bool tilingFeedbackActive: false

    // A lightly underdamped, axis-staggered response measured against the reference.
    // Height leads while opening; width leads while closing. Angular frequency keeps
    // the response time-based while FrameAnimation samples it at the display cadence.
    readonly property real openWidthFrequency: 24
    readonly property real openHeightFrequency: 29
    readonly property real closeWidthFrequency: 30
    readonly property real closeHeightFrequency: 24
    readonly property real openWidthDamping: 0.78
    readonly property real openHeightDamping: 0.82
    readonly property real closeWidthDamping: 0.76
    readonly property real closeHeightDamping: 0.78

    readonly property real surfaceHeight: islandVisualHeight
    readonly property real morphProgress: Math.max(0, Math.min(1,
        (surfaceHeight - compactHeight) / (expandedHeight - compactHeight)))
    readonly property real dynamicCornerRadius: 17 + 11 * Math.sqrt(morphProgress)
    readonly property real dynamicEarWidth: 9 + 7 * Math.sqrt(morphProgress)
    readonly property real dynamicEarDepth: 9 + 9 * Math.sqrt(morphProgress)
    readonly property real islandCaptureWidth: islandVisualWidth + dynamicEarWidth * 2
    readonly property real islandCaptureHeight: islandVisualHeight + 8
    readonly property bool nativeInputMaskEnabled: !qaMode

    width: canvasWidth
    height: canvasHeight
    x: Screen.virtualX + Math.round((Screen.width - width) / 2)
    y: Screen.virtualY

    property QtObject colors: QtObject {
        readonly property color black: "#000000"
        readonly property color raised: "#151515"
        readonly property color hover: "#232323"
        readonly property color divider: "#242424"
        readonly property color text: "#f5f5f7"
        readonly property color secondary: "#a1a1a6"
        readonly property color tertiary: "#6e6e73"
        readonly property color accent: "#5ac8fa"
        readonly property color green: "#63e6a5"
        readonly property color timer: "#ff9f0a"
    }

    function snapMorphToTarget() {
        islandVisualWidth = islandTargetWidth
        islandVisualHeight = islandTargetHeight
        islandWidthVelocity = 0
        islandHeightVelocity = 0
    }

    function advanceMorph(frameTime) {
        if (!motionReady || controller.reducedMotion) {
            snapMorphToTarget()
            return
        }

        // Clamp a resumed/stalled frame, then integrate in small fixed substeps.
        // This avoids a single large impulse without quantizing visible updates.
        const elapsed = Math.min(Math.max(frameTime, 0), 1 / 30)
        if (elapsed <= 0)
            return

        const opening = islandTargetWidth > compactWidth + 0.5
        const widthFrequency = opening ? openWidthFrequency : closeWidthFrequency
        const heightFrequency = opening ? openHeightFrequency : closeHeightFrequency
        const widthDamping = opening ? openWidthDamping : closeWidthDamping
        const heightDamping = opening ? openHeightDamping : closeHeightDamping
        const steps = Math.max(1, Math.ceil(elapsed * 240))
        const stepTime = elapsed / steps

        let width = islandVisualWidth
        let height = islandVisualHeight
        let widthVelocity = islandWidthVelocity
        let heightVelocity = islandHeightVelocity

        for (let step = 0; step < steps; ++step) {
            const widthAcceleration = widthFrequency * widthFrequency
                                      * (islandTargetWidth - width)
                                      - 2 * widthDamping * widthFrequency * widthVelocity
            widthVelocity += widthAcceleration * stepTime
            width += widthVelocity * stepTime

            const heightAcceleration = heightFrequency * heightFrequency
                                       * (islandTargetHeight - height)
                                       - 2 * heightDamping * heightFrequency * heightVelocity
            heightVelocity += heightAcceleration * stepTime
            height += heightVelocity * stepTime
        }

        if (Math.abs(islandTargetWidth - width) < 0.04
                && Math.abs(widthVelocity) < 0.45) {
            width = islandTargetWidth
            widthVelocity = 0
        }
        if (Math.abs(islandTargetHeight - height) < 0.04
                && Math.abs(heightVelocity) < 0.45) {
            height = islandTargetHeight
            heightVelocity = 0
        }

        islandVisualWidth = width
        islandVisualHeight = height
        islandWidthVelocity = widthVelocity
        islandHeightVelocity = heightVelocity
    }

    Component.onCompleted: {
        snapMorphToTarget()
        motionReady = true
    }

    Connections {
        target: controller
        function onReducedMotionChanged() {
            if (controller.reducedMotion)
                window.snapMorphToTarget()
        }
    }

    Connections {
        target: tilingManager
        function onEnabledChanged() {
            window.tilingFeedbackActive = true
            controller.setExpanded(true)
            tilingFeedbackTimer.restart()
        }
    }

    Timer {
        id: tilingFeedbackTimer
        interval: 1100
        repeat: false
        onTriggered: {
            window.tilingFeedbackActive = false
            if (!controller.pinned && !islandHover.hovered && !qaMode)
                controller.setExpanded(false)
        }
    }

    FrameAnimation {
        id: morphFrameClock
        running: window.motionReady && !controller.reducedMotion
                 && (Math.abs(window.islandVisualWidth - window.islandTargetWidth) > 0.04
                     || Math.abs(window.islandVisualHeight - window.islandTargetHeight) > 0.04
                     || Math.abs(window.islandWidthVelocity) > 0.45
                     || Math.abs(window.islandHeightVelocity) > 0.45)
        onTriggered: window.advanceMorph(frameTime)
    }

    Timer {
        id: hoverOpenDelay
        interval: 280
        repeat: false
        running: islandHover.hovered && !controller.expanded && !window.dragActive
        onTriggered: controller.setExpanded(true)
    }

    Timer {
        id: leaveCloseDelay
        interval: 560
        repeat: false
        running: controller.expanded && !controller.pinned
                 && !islandHover.hovered && !qaMode
                 && !window.tilingFeedbackActive
        onTriggered: controller.setExpanded(false)
    }

    Rectangle {
        anchors.fill: parent
        visible: qaBackdrop
        color: "#5f6974"
    }

    Item {
        id: islandHost
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: window.islandVisualWidth
        height: window.islandVisualHeight

        HoverHandler { id: islandHover }

        NotchSurface {
            id: surface
            z: 1
            x: -window.dynamicEarWidth
            width: parent.width + window.dynamicEarWidth * 2
            height: window.surfaceHeight
            surfaceColor: colors.black
            bottomRadius: window.dynamicCornerRadius
            earWidth: window.dynamicEarWidth
            earDepth: window.dynamicEarDepth
        }

        MouseArea {
            id: shellInput
            z: 2
            x: -window.dynamicEarWidth
            width: parent.width + window.dynamicEarWidth * 2
            height: window.surfaceHeight
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor
            enabled: !window.dragActive
            Accessible.name: controller.expanded ? "Dynamic Island" : "Open Dynamic Island"
            Accessible.role: Accessible.Button
            onClicked: {
                if (!controller.expanded)
                    controller.setExpanded(true)
            }
        }

        Item {
            id: compactContent
            z: 4
            anchors.fill: parent
            enabled: !controller.expanded && !window.dragActive
            opacity: enabled ? 1 : 0
            scale: enabled ? 1 : 0.96
            transformOrigin: Item.Top

            Behavior on opacity {
                NumberAnimation { duration: controller.reducedMotion ? 0 : 100; easing.type: Easing.OutQuad }
            }
            Behavior on scale {
                NumberAnimation { duration: controller.reducedMotion ? 0 : 150; easing.type: Easing.OutCubic }
            }

            Column {
                visible: false
                anchors.left: parent.left
                anchors.leftMargin: 17
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -1
                spacing: 0

                Text {
                    text: controller.compactDateText
                    color: colors.secondary
                    font.family: window.uiFont
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.65
                }
                Text {
                    visible: controller.mediaAvailable
                    text: controller.mediaPlaying ? "MEDIA PLAYING" : "MEDIA PAUSED"
                    color: controller.mediaPlaying ? colors.green : colors.tertiary
                    font.family: window.uiFont
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.5
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -1
                spacing: 4
                visible: !controller.timerActive && !controller.timerRinging

                Text {
                    text: controller.timeText
                    color: colors.text
                    font.family: window.uiFont
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.2
                }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    text: controller.meridiemText
                    color: colors.secondary
                    font.family: window.uiFont
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
            }

            Row {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -1
                spacing: 7
                visible: controller.timerActive || controller.timerRinging

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 15
                    height: 15
                    source: Qt.resolvedUrl("../assets/icons/timer-orange.svg")
                    sourceSize.width: 30
                    sourceSize.height: 30
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: controller.timerRinging ? "Time's up" : controller.timerRemainingText
                    color: controller.timerRinging ? colors.timer : colors.text
                    font.family: window.uiFont
                    font.pixelSize: controller.timerRinging ? 13 : 16
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.2
                    font.features: { "tnum": 1 }
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 5
                    height: 5
                    radius: 3
                    color: colors.timer
                    opacity: controller.timerPaused ? 0.38 : 1

                    SequentialAnimation on opacity {
                        running: controller.timerActive && !controller.timerPaused
                                 && !controller.expanded && !controller.reducedMotion
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: 650; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1; duration: 650; easing.type: Easing.InOutSine }
                    }
                }
            }
        }

        Item {
            id: expandedContent
            z: 4
            anchors.fill: parent
            visible: false
            enabled: false
            opacity: enabled ? 1 : 0
            scale: enabled ? 1 : 0.965
            transformOrigin: Item.Top

            Behavior on opacity {
                SequentialAnimation {
                    PauseAnimation { duration: expandedContent.enabled && !controller.reducedMotion ? 65 : 0 }
                    NumberAnimation { duration: controller.reducedMotion ? 0 : 150; easing.type: Easing.OutQuad }
                }
            }
            Behavior on scale {
                NumberAnimation { duration: controller.reducedMotion ? 0 : 210; easing.type: Easing.OutCubic }
            }

            Text {
                x: 24
                y: 15
                text: controller.dateText
                color: colors.text
                font.family: window.uiFont
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 17
                y: 9
                spacing: 5

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: controller.timeText + " " + controller.meridiemText
                    color: colors.secondary
                    font.family: window.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    rightPadding: 4
                }

                IslandButton {
                    iconOnly: true
                    glyph: controller.pinned ? "\uE77A" : "\uE718"
                    selected: controller.pinned
                    accessibleName: controller.pinned ? "Unpin island" : "Keep island open"
                    onClicked: controller.togglePinned()
                }

                IslandButton {
                    iconOnly: true
                    glyph: "\uE711"
                    accessibleName: "Collapse"
                    onClicked: {
                        controller.setPinned(false)
                        controller.setExpanded(false)
                    }
                }
            }

            Rectangle {
                x: 24
                y: 50
                width: parent.width - 48
                height: 1
                color: colors.divider
            }

            Item {
                id: primaryPane
                x: 24
                y: 67
                width: parent.width - 294
                height: 151

                Item {
                    anchors.fill: parent
                    visible: controller.mediaAvailable

                    Rectangle {
                        id: artworkBackground
                        width: 104
                        height: 104
                        radius: 16
                        color: colors.raised
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: controller.mediaArtworkUrl
                            visible: controller.mediaArtworkUrl.length > 0
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: false
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: controller.mediaArtworkUrl.length === 0
                            text: "\uE8D6"
                            color: colors.secondary
                            font.family: window.iconFont
                            font.pixelSize: 30
                        }
                    }

                    Column {
                        x: 122
                        y: 2
                        width: parent.width - x
                        spacing: 3

                        Text {
                            width: parent.width
                            text: controller.mediaTitle
                            color: colors.text
                            elide: Text.ElideRight
                            maximumLineCount: 1
                            font.family: window.uiFont
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            text: controller.mediaArtist.length > 0
                                  ? controller.mediaArtist : controller.mediaSource
                            color: colors.secondary
                            elide: Text.ElideRight
                            maximumLineCount: 1
                            font.family: window.uiFont
                            font.pixelSize: 11
                        }
                    }

                    Row {
                        x: 120
                        y: 55
                        spacing: 8

                        IslandButton {
                            iconOnly: true
                            glyph: "\uE892"
                            enabled: controller.mediaCanPrevious
                            accessibleName: "Previous track"
                            onClicked: controller.previousTrack()
                        }
                        IslandButton {
                            iconOnly: true
                            glyph: controller.mediaPlaying ? "\uE769" : "\uE768"
                            accented: true
                            accessibleName: controller.mediaPlaying ? "Pause" : "Play"
                            onClicked: controller.togglePlayback()
                        }
                        IslandButton {
                            iconOnly: true
                            glyph: "\uE893"
                            enabled: controller.mediaCanNext
                            accessibleName: "Next track"
                            onClicked: controller.nextTrack()
                        }
                    }

                    Rectangle {
                        x: 0
                        y: 121
                        width: parent.width
                        height: 3
                        radius: 2
                        color: colors.raised

                        Rectangle {
                            width: parent.width * controller.mediaProgress
                            height: parent.height
                            radius: 2
                            color: colors.text

                            Behavior on width {
                                NumberAnimation { duration: 220; easing.type: Easing.Linear }
                            }
                        }
                    }

                    Text {
                        x: 0
                        y: 131
                        text: controller.mediaPositionText
                        color: colors.tertiary
                        font.family: window.uiFont
                        font.pixelSize: 9
                        font.features: { "tnum": 1 }
                    }
                    Text {
                        anchors.right: parent.right
                        y: 131
                        text: controller.mediaDurationText
                        color: colors.tertiary
                        font.family: window.uiFont
                        font.pixelSize: 9
                        font.features: { "tnum": 1 }
                    }
                }

                Item {
                    anchors.fill: parent
                    visible: !controller.mediaAvailable

                    Row {
                        y: 9
                        spacing: 7

                        Text {
                            text: controller.timeText
                            color: colors.text
                            font.family: window.uiFont
                            font.pixelSize: 49
                            font.weight: Font.DemiBold
                            font.letterSpacing: -1.5
                            font.features: { "tnum": 1 }
                        }
                        Text {
                            anchors.bottom: parent.children[0].bottom
                            anchors.bottomMargin: 7
                            text: controller.meridiemText
                            color: colors.secondary
                            font.family: window.uiFont
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        y: 77
                        text: controller.dateText
                        color: colors.secondary
                        font.family: window.uiFont
                        font.pixelSize: 14
                    }
                    Text {
                        y: 111
                        text: "No active Windows media session"
                        color: colors.tertiary
                        font.family: window.uiFont
                        font.pixelSize: 10
                    }
                }
            }

            Rectangle {
                x: parent.width - 254
                y: 66
                width: 1
                height: 153
                color: colors.divider
            }

            Item {
                id: systemPane
                x: parent.width - 230
                y: 68
                width: 206
                height: 151

                Row {
                    width: parent.width
                    spacing: 11

                    Text {
                        text: "\uE701"
                        color: colors.secondary
                        font.family: window.iconFont
                        font.pixelSize: 16
                    }
                    Column {
                        width: parent.width - 28
                        spacing: 2
                        Text {
                            width: parent.width
                            text: controller.networkName.length > 0
                                  ? controller.networkName : controller.networkStatus
                            color: colors.text
                            elide: Text.ElideRight
                            font.family: window.uiFont
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: controller.networkName.length > 0 ? controller.networkStatus : ""
                            color: colors.tertiary
                            font.family: window.uiFont
                            font.pixelSize: 9
                        }
                    }
                }

                Row {
                    y: 51
                    width: parent.width
                    spacing: 11

                    Text {
                        text: controller.batteryAvailable ? "\uE850" : "\uE83F"
                        color: colors.secondary
                        font.family: window.iconFont
                        font.pixelSize: 16
                    }
                    Text {
                        width: parent.width - 28
                        text: controller.powerText
                        color: colors.text
                        elide: Text.ElideRight
                        font.family: window.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }

                Row {
                    y: 101
                    width: parent.width
                    spacing: 10

                    IslandButton {
                        width: 32
                        height: 32
                        iconOnly: true
                        glyph: controller.muted ? "\uE74F" : "\uE767"
                        selected: controller.muted
                        accessibleName: controller.muted ? "Unmute" : "Mute"
                        onClicked: controller.toggleMute()
                    }

                    Slider {
                        id: volumeSlider
                        anchors.verticalCenter: parent.verticalCenter
                        width: 124
                        from: 0
                        to: 100
                        value: controller.volume
                        live: true
                        onMoved: controller.setVolume(Math.round(value))
                        Accessible.name: "System volume"

                        background: Rectangle {
                            x: volumeSlider.leftPadding
                            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - 2
                            width: volumeSlider.availableWidth
                            height: 4
                            radius: 2
                            color: colors.raised

                            Rectangle {
                                width: volumeSlider.visualPosition * parent.width
                                height: parent.height
                                radius: 2
                                color: colors.text
                            }
                        }
                        handle: Rectangle {
                            x: volumeSlider.leftPadding
                               + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                            width: volumeSlider.hovered ? 12 : 10
                            height: width
                            radius: width / 2
                            color: colors.text
                            Behavior on width { NumberAnimation { duration: 100 } }
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: controller.volume + "%"
                        color: colors.tertiary
                        font.family: window.uiFont
                        font.pixelSize: 9
                        font.features: { "tnum": 1 }
                    }
                }
            }

            Rectangle {
                x: 24
                y: 232
                width: parent.width - 48
                height: 1
                color: colors.divider
            }

            Row {
                x: 24
                y: 244
                width: parent.width - 48
                spacing: 9

                Text {
                    text: "\uE8B7"
                    color: controller.droppedFileCount > 0 ? colors.accent : colors.tertiary
                    font.family: window.iconFont
                    font.pixelSize: 15
                }
                Text {
                    anchors.verticalCenter: parent.children[0].verticalCenter
                    width: parent.width - (controller.droppedFileCount > 0 ? 152 : 26)
                    text: controller.droppedFileCount > 0
                          ? controller.lastDroppedFile
                          : "Drop files on the island to keep them within reach"
                    color: controller.droppedFileCount > 0 ? colors.secondary : colors.tertiary
                    elide: Text.ElideMiddle
                    font.family: window.uiFont
                    font.pixelSize: 10
                }
                IslandButton {
                    visible: controller.droppedFileCount > 0
                    text: "Show"
                    fixedWidth: 58
                    quiet: true
                    onClicked: controller.revealLastDroppedFile()
                }
                IslandButton {
                    visible: controller.droppedFileCount > 0
                    iconOnly: true
                    glyph: "\uE711"
                    accessibleName: "Clear file shelf"
                    onClicked: controller.clearDroppedFiles()
                }
            }
        }

        ExpandedPanel {
            id: expandedPanel
            z: 4
            anchors.fill: parent
            colors: window.colors
            uiFont: window.uiFont
            iconFont: window.iconFont
            expanded: controller.expanded
            dragActive: window.dragActive
            reducedMotion: controller.reducedMotion
            tilingFeedbackActive: window.tilingFeedbackActive
        }

        Item {
            id: dragContent
            z: 5
            anchors.fill: parent
            enabled: window.dragActive
            opacity: enabled ? 1 : 0
            scale: enabled ? 1 : 0.94

            Behavior on opacity {
                NumberAnimation { duration: controller.reducedMotion ? 0 : 130 }
            }
            Behavior on scale {
                NumberAnimation { duration: controller.reducedMotion ? 0 : 180; easing.type: Easing.OutCubic }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 48
                text: "\uE8B7"
                color: colors.accent
                font.family: window.iconFont
                font.pixelSize: 23
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 82
                text: "Release to add to the file shelf"
                color: colors.text
                font.family: window.uiFont
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 106
                text: "Nothing is copied or uploaded"
                color: colors.tertiary
                font.family: window.uiFont
                font.pixelSize: 10
            }
        }

        DropArea {
            id: dropTarget
            z: 10
            x: -window.dynamicEarWidth
            width: parent.width + window.dynamicEarWidth * 2
            height: window.surfaceHeight
            keys: ["text/uri-list"]
            onDropped: function(drop) {
                controller.handleDrop(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }
}
