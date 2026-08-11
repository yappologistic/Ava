import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Window 6.5
import Ava 1.0

Window {
    id: window

    visible: true
    color: automationMode ? "#5f6974" : "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.NoDropShadowWindowHint
           | (automationMode ? Qt.Window : Qt.Tool)
    title: appLauncher.open ? "Ava Launcher" : "Ava"

    readonly property string uiFont: "Inter"
    readonly property string monoFont: "Geist Mono"
    readonly property string iconFont: "Segoe Fluent Icons"
    readonly property var islandController: controller
    readonly property int compactWidth: 150
    readonly property int compactHeight: 39
    // Measured from all 48,446 source frames: the stable open shell is about 4.5:1.
    readonly property int expandedWidth: Math.max(520, Math.min(584, Screen.width - 40))
    readonly property int baseExpandedHeight: 128
    readonly property int wallpaperExpandedHeight: 228
    readonly property int monitorExpandedHeight: 330
    readonly property int launcherExpandedHeight: Math.max(420, Math.min(468, Screen.height - 52))
    readonly property int expandedHeight: appLauncher.open
                                          ? launcherExpandedHeight
                                          : (controller.monitorDetailsOpen
                                          ? monitorExpandedHeight
                                          : (controller.wallpaperPanelOpen
                                          ? wallpaperExpandedHeight
                                          : baseExpandedHeight))
    readonly property int dragWidth: Math.min(420, expandedWidth)
    readonly property int dragHeight: 116
    readonly property int mediaPeekWidth: 230
    readonly property int codexPeekWidth: 242
    // Sized for the widest three-digit readouts without letting live values resize the pill.
    readonly property int monitorWidth: 288
    readonly property int timerSatelliteDiameter: compactHeight
    readonly property int timerSatelliteGap: 8
    readonly property int canvasWidth: expandedWidth + 40
    readonly property int canvasHeight: Math.max(wallpaperExpandedHeight,
                                                  launcherExpandedHeight,
                                                  monitorExpandedHeight)
                                                + islandSurfaceTop + 10
    readonly property int liquidGlassCaptureWidth: canvasWidth
    readonly property int liquidGlassCaptureHeight: Math.max(wallpaperExpandedHeight,
                                                               launcherExpandedHeight,
                                                               monitorExpandedHeight) + 4
    readonly property bool launcherOpen: appLauncher.open
    readonly property bool dragActive: dropTarget.containsDrag
    property bool shellExpandedVisual: controller.expanded || appLauncher.open
    property int retainedExpandedHeight: baseExpandedHeight
    property real hoverTension: islandHover.hovered && !controller.expanded
                                && !dragActive ? 1 : 0
    readonly property int islandTargetWidth: dragActive ? dragWidth
        : (shellExpandedVisual ? expandedWidth + Math.round(ringingPulse * 8)
                                + Math.round(pinPulse * 4)
                               : (compactActivity === "codex" ? codexPeekWidth
                                  : (compactActivity === "media" ? mediaPeekWidth
                                     : (compactActivity === "monitor" ? monitorWidth
                                        : compactWidth + Math.round(hoverTension * 4)))))
    readonly property int visualExpandedHeight: controller.expanded || appLauncher.open
                                                ? expandedHeight : retainedExpandedHeight
    readonly property int islandTargetHeight: dragActive ? dragHeight
        : (shellExpandedVisual ? visualExpandedHeight + Math.round(ringingPulse * 3)
                               : compactHeight + Math.round(hoverTension))

    property real islandVisualWidth: compactWidth
    property real islandVisualHeight: compactHeight
    property real islandWidthVelocity: 0
    property real islandHeightVelocity: 0
    property bool motionReady: false
    property bool tilingFeedbackActive: false
    property bool mediaPeekActive: false
    property string lastMediaKey: ""
    property bool lastTimerRinging: false
    property real ringingPulse: 0
    property real pinPulse: 0
    property int lastDroppedFileCount: 0
    property real dropTraceProgress: 0
    readonly property string compactActivity: codexBridge.compactVisible
                                              ? "codex"
                                              : (window.mediaPeekActive && controller.mediaAvailable
                                                 && (controller.monitorEnabled
                                                     || (!controller.timerActive
                                                         && !controller.timerRinging))
                                                 ? "media"
                                                 : (controller.monitorEnabled
                                                    ? "monitor"
                                                    : (controller.timerActive
                                                       || controller.timerRinging
                                                       ? "timer" : "clock")))
    readonly property bool codexSurfaceOpaque: codexBridge.panelOpen
                                                || compactActivity === "codex"
    readonly property bool liquidGlassVisualActive: controller.liquidGlassEnabled
                                                     && !codexSurfaceOpaque
    // The shell is a single continuous spring. Height leads while opening and
    // width leads while closing, with enough damping to avoid a rubbery tail.
    readonly property real openWidthFrequency: 25
    readonly property real openHeightFrequency: 30
    readonly property real closeWidthFrequency: 32
    readonly property real closeHeightFrequency: 27
    readonly property real openWidthDamping: 0.88
    readonly property real openHeightDamping: 0.90
    readonly property real closeWidthDamping: 0.92
    readonly property real closeHeightDamping: 0.90

    readonly property real surfaceHeight: islandVisualHeight
    readonly property bool pillMode: controller.pillMode
    readonly property real islandSurfaceTop: pillMode ? 8 : 0
    readonly property real morphProgress: Math.max(0, Math.min(1,
        (surfaceHeight - compactHeight) / (baseExpandedHeight - compactHeight)))
    readonly property real compactRevealProgress: controller.reducedMotion
        ? (shellExpandedVisual ? 0 : 1)
        : Math.max(0, Math.min(1,
            (compactHeight + 32 - surfaceHeight) / 32))
    readonly property real dynamicCornerRadius: 17 + 11 * Math.sqrt(morphProgress)
    readonly property real pillCornerRadius: Math.min(surfaceHeight / 2,
        19.5 + 8.5 * Math.sqrt(morphProgress))
    readonly property real dynamicEarWidth: 9 + 7 * Math.sqrt(morphProgress)
                                             + hoverTension * 2
    readonly property real dynamicEarDepth: 9 + 9 * Math.sqrt(morphProgress)
    readonly property real shellHorizontalOverflow: pillMode ? 0 : dynamicEarWidth
    readonly property real liquidGlassPointerX: islandHover.point.position.x
                                                + shellHorizontalOverflow
    readonly property real liquidGlassPointerY: islandHover.point.position.y
    readonly property bool liquidGlassPointerActive: islandHover.hovered
    readonly property bool liquidGlassReducedMotion: controller.reducedMotion
    readonly property real liquidGlassEdgeStrength: appLauncher.open ? 0.18 : 1.0
    readonly property bool timerSatelliteVisible: controller.monitorEnabled
                                                   && controller.timerActive
                                                   && !shellExpandedVisual
                                                   && !dragActive
    readonly property real islandCaptureLeft: (canvasWidth - islandVisualWidth) / 2
                                               - shellHorizontalOverflow
    readonly property real islandCaptureRight: (canvasWidth + islandVisualWidth) / 2
                                                + shellHorizontalOverflow
                                                + (timerSatelliteVisible
                                                   ? timerSatelliteGap
                                                     + timerSatelliteDiameter : 0)
    readonly property real islandCaptureWidth: islandCaptureRight - islandCaptureLeft
    readonly property real islandCaptureHeight: islandSurfaceTop
                                                + islandVisualHeight + 8
    readonly property bool nativeInputMaskEnabled: !qaMode && !automationMode
    readonly property bool keyboardCaptureArmed: appLauncher.open
                                                  || (codexBridge.panelOpen
                                                  && codexBridge.connected
                                                  && !codexBridge.active
                                                  && !codexBridge.awaitingApproval
                                                  && codexBridge.phase !== "completed"
                                                  && codexBridge.phase !== "error")

    Behavior on hoverTension {
        NumberAnimation {
            duration: controller.reducedMotion ? 0 : MotionTokens.directSettle
            easing.type: MotionTokens.easeOut
        }
    }

    width: canvasWidth
    height: canvasHeight
    x: Screen.virtualX + Math.round((Screen.width - width) / 2)
    y: Screen.virtualY

    Binding {
        target: liquidGlassBackdrop
        property: "active"
        value: window.liquidGlassVisualActive
               && (!qaMode || liveGlassQa)
               && (!automationMode || liveGlassQa)
    }

    function requestGlassRefresh() {
        if (window.liquidGlassVisualActive
                && (!qaMode || liveGlassQa)
                && (!automationMode || liveGlassQa))
            liquidGlassBackdrop.requestImmediateFrame()
    }

    onIslandVisualWidthChanged: requestGlassRefresh()
    onIslandVisualHeightChanged: requestGlassRefresh()
    onIslandSurfaceTopChanged: requestGlassRefresh()
    onPillModeChanged: requestGlassRefresh()
    onTimerSatelliteVisibleChanged: requestGlassRefresh()
    onLiquidGlassPointerXChanged: requestGlassRefresh()
    onLiquidGlassPointerYChanged: requestGlassRefresh()
    onLiquidGlassPointerActiveChanged: requestGlassRefresh()

    property QtObject colors: QtObject {
        readonly property color black: window.liquidGlassVisualActive
                                       ? "#34080c12" : "#000000"
        readonly property color raised: window.liquidGlassVisualActive
                                        ? "#24ffffff" : "#151515"
        readonly property color subtle: window.liquidGlassVisualActive
                                        ? "#14ffffff" : "#101010"
        readonly property color hover: window.liquidGlassVisualActive
                                       ? "#38ffffff" : "#232323"
        readonly property color divider: window.liquidGlassVisualActive
                                         ? "#28ffffff" : "#242424"
        readonly property color popover: window.liquidGlassVisualActive
                                         ? "#d4141820" : "#1a1a1a"
        readonly property color text: "#f5f5f7"
        readonly property color secondary: window.liquidGlassVisualActive
                                            ? "#cdd1d8" : "#a1a1a6"
        readonly property color tertiary: window.liquidGlassVisualActive
                                           ? "#a4abb6" : "#6e6e73"
        readonly property color accent: "#5ac8fa"
        readonly property color green: "#63e6a5"
        readonly property color calendarAccent: "#9ad9cc"
        readonly property color calendarSelection: "#17382f"
        readonly property color timer: "#ff9f0a"
        readonly property color warning: "#ffb340"
        readonly property color danger: "#ff5f57"
        readonly property color glassRim: "#52ffffff"
        readonly property color glassHighlight: "#28ffffff"
        readonly property color glassShadow: "#52000000"
        readonly property color glassCool: "#365ac8fa"
        readonly property color glassWarm: "#24ff7ac8"
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

        // Choose direction per axis from the live geometry. Compact activities
        // can be wider than the base clock pill, so width alone cannot tell us
        // whether the shell is opening or closing.
        const widthOpening = islandTargetWidth >= islandVisualWidth
        const heightOpening = islandTargetHeight >= islandVisualHeight
        const widthFrequency = widthOpening ? openWidthFrequency : closeWidthFrequency
        const heightFrequency = heightOpening ? openHeightFrequency : closeHeightFrequency
        const widthDamping = widthOpening ? openWidthDamping : closeWidthDamping
        const heightDamping = heightOpening ? openHeightDamping : closeHeightDamping
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
        if (controller.expanded || appLauncher.open)
            retainedExpandedHeight = expandedHeight
        snapMorphToTarget()
        motionReady = true
    }

    onExpandedHeightChanged: {
        if (controller.expanded || appLauncher.open)
            retainedExpandedHeight = expandedHeight
    }

    Connections {
        target: controller
        function onReducedMotionChanged() {
            if (controller.reducedMotion)
                window.snapMorphToTarget()
        }
        function onExpandedChanged() {
            if (controller.expanded) {
                delayedShellClose.stop()
                window.retainedExpandedHeight = window.expandedHeight
                window.shellExpandedVisual = true
            } else if (controller.reducedMotion) {
                window.shellExpandedVisual = false
            } else {
                delayedShellClose.restart()
            }
        }
        function onPinnedChanged() {
            if (!controller.reducedMotion)
                pinShellSettle.restart()
        }
        function onMediaChanged() {
            const mediaKey = controller.mediaTitle + "\n" + controller.mediaArtist
            if (controller.mediaAvailable && controller.mediaPlaying
                    && mediaKey.length > 1 && mediaKey !== window.lastMediaKey
                    && (controller.monitorEnabled
                        || (!controller.timerActive && !controller.timerRinging))
                    && !qaMode) {
                window.mediaPeekActive = true
                mediaPeekTimer.restart()
            }
            window.lastMediaKey = mediaKey
        }
        function onTimerChanged() {
            if (controller.timerRinging && !window.lastTimerRinging
                    && !controller.reducedMotion) {
                timerCompletionPulse.restart()
            } else if (!controller.timerRinging) {
                timerCompletionPulse.stop()
                window.ringingPulse = 0
            }
            window.lastTimerRinging = controller.timerRinging
        }
        function onDroppedFilesChanged() {
            if (controller.droppedFileCount > window.lastDroppedFileCount
                    && !controller.reducedMotion)
                dropCompletionTrace.restart()
            window.lastDroppedFileCount = controller.droppedFileCount
        }
        function onForegroundFullscreenChanged() {
            if (controller.foregroundFullscreen && !qaMode && !appLauncher.open) {
                window.mediaPeekActive = false
                if (!controller.timerRinging)
                    controller.setExpanded(false)
            }
        }
    }

    Connections {
        target: codexBridge
        function onAttentionRequested() {
            if (codexBridge.awaitingApproval) {
                controller.closeTimer()
                codexBridge.setPanelOpen(true)
                controller.setExpanded(true)
            }
        }
        function onPanelOpenChanged() {
            if (codexBridge.panelOpen) {
                controller.closeTimer()
                controller.setExpanded(true)
            }
        }
    }

    Connections {
        target: appLauncher
        function onOpenChanged() {
            if (appLauncher.open) {
                delayedShellClose.stop()
                window.mediaPeekActive = false
                controller.closeTimer()
                controller.closeWallpaperPanel()
                codexBridge.setPanelOpen(false)
                controller.setExpanded(true)
            } else if (!controller.pinned && !codexBridge.panelOpen
                       && !controller.timerPanelOpen
                       && !controller.wallpaperPanelOpen) {
                controller.setExpanded(false)
            }
        }
    }

    Timer {
        id: delayedShellClose
        interval: 45
        repeat: false
        onTriggered: window.shellExpandedVisual = false
    }

    Timer {
        id: mediaPeekTimer
        interval: 2400
        repeat: false
        onTriggered: window.mediaPeekActive = false
    }

    SequentialAnimation {
        id: timerCompletionPulse
        loops: 2
        PauseAnimation { duration: 110 }
        NumberAnimation {
            target: window
            property: "ringingPulse"
            to: 1
            duration: 140
            easing.type: MotionTokens.easeOut
        }
        NumberAnimation {
            target: window
            property: "ringingPulse"
            to: 0
            duration: 210
            easing.type: Easing.InOutCubic
        }
    }

    SequentialAnimation {
        id: dropCompletionTrace
        NumberAnimation {
            target: window
            property: "dropTraceProgress"
            from: 0
            to: 1
            duration: MotionTokens.reveal
            easing.type: MotionTokens.easeOut
        }
        PauseAnimation { duration: 80 }
        NumberAnimation {
            target: window
            property: "dropTraceProgress"
            to: 0
            duration: MotionTokens.state
            easing.type: Easing.InCubic
        }
    }

    SequentialAnimation {
        id: pinShellSettle
        NumberAnimation {
            target: window
            property: "pinPulse"
            from: 0
            to: 1
            duration: MotionTokens.press
            easing.type: MotionTokens.easeOut
        }
        NumberAnimation {
            target: window
            property: "pinPulse"
            to: 0
            duration: MotionTokens.directSettle
            easing.type: MotionTokens.easeOut
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
        interval: 240
        repeat: false
        running: islandHover.hovered && !controller.expanded && !window.dragActive
                 && !controller.foregroundFullscreen && !qaMode
        onTriggered: controller.setExpanded(true)
    }

    Timer {
        id: leaveCloseDelay
        interval: 480
        repeat: false
        running: controller.expanded && !controller.pinned
                 && !islandHover.hovered && !qaMode
                 && !window.tilingFeedbackActive && !appLauncher.open
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
        anchors.topMargin: window.islandSurfaceTop
        anchors.horizontalCenter: parent.horizontalCenter
        width: window.islandVisualWidth
        height: window.islandVisualHeight

        HoverHandler { id: islandHover }

        NotchSurface {
            id: surface
            z: 1
            x: -window.shellHorizontalOverflow
            width: parent.width + window.shellHorizontalOverflow * 2
            height: window.surfaceHeight
            surfaceColor: colors.black
            glassEnabled: window.liquidGlassVisualActive
            glassBackdrop: liquidGlassBackdrop
            reducedMotion: controller.reducedMotion
            pointerPosition: Qt.point(islandHover.point.position.x
                                      + window.shellHorizontalOverflow,
                                      islandHover.point.position.y)
            pointerActive: islandHover.hovered
            morphProgress: window.morphProgress
            rimColor: colors.glassRim
            highlightColor: colors.glassHighlight
            shadowColor: colors.glassShadow
            coolEdgeColor: colors.glassCool
            warmEdgeColor: colors.glassWarm
            pillMode: window.pillMode
            pillRadius: window.pillCornerRadius
            bottomRadius: window.dynamicCornerRadius
            earWidth: window.dynamicEarWidth
            earDepth: window.dynamicEarDepth
        }

        Rectangle {
            z: 3
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(0, window.surfaceHeight - 2)
            width: Math.max(0, (parent.width - 28) * window.dropTraceProgress)
            height: 2
            radius: 1
            color: colors.accent
            opacity: window.dropTraceProgress > 0 ? 0.92 : 0
        }

        MouseArea {
            id: shellInput
            z: 2
            x: -window.shellHorizontalOverflow
            width: parent.width + window.shellHorizontalOverflow * 2
            height: window.surfaceHeight
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor
            enabled: !window.dragActive
            Accessible.name: controller.expanded ? "Ava" : "Open Ava"
            Accessible.role: Accessible.Button
            onClicked: function(mouse) {
                if (window.compactActivity === "monitor"
                        && compactMonitor.containsCpuPoint(shellInput,
                                                           mouse.x,
                                                           mouse.y)) {
                    controller.openMonitorDetails()
                } else if (!controller.expanded) {
                    controller.setExpanded(true)
                }
            }
        }

        Item {
            id: compactContent
            z: 4
            anchors.fill: parent
            enabled: !controller.expanded && !window.dragActive
            visible: opacity > 0.001
            opacity: enabled ? window.compactRevealProgress : 0
            scale: 0.97 + 0.03 * opacity
            transformOrigin: Item.Center

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
                id: compactClock
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -1
                spacing: 4
                enabled: window.compactActivity === "clock"
                visible: opacity > 0.001
                opacity: enabled ? 1 : 0
                transform: Translate {
                    y: compactClock.enabled ? 0 : -3
                    Behavior on y { NumberAnimation { duration: controller.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }
                }
                Behavior on opacity { NumberAnimation { duration: controller.reducedMotion ? 0 : MotionTokens.state } }

                RollingDigits {
                    id: compactClockDigits
                    text: controller.timeText
                    color: colors.text
                    fontFamily: window.uiFont
                    fontPixelSize: 16
                    fontWeight: Font.DemiBold
                    letterSpacing: -0.2
                    reducedMotion: controller.reducedMotion
                }
            }

            MonitorCompact {
                id: compactMonitor
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -1
                width: parent.width
                height: 20
                controller: window.islandController
                colors: window.colors
                uiFont: window.uiFont
                reducedMotion: controller.reducedMotion
                onCpuDetailsRequested: controller.openMonitorDetails()
                enabled: window.compactActivity === "monitor"
                visible: opacity > 0.001
                opacity: enabled ? 1 : 0
                transform: Translate {
                    x: compactMonitor.enabled ? 0 : -3
                    Behavior on x {
                        NumberAnimation {
                            duration: controller.reducedMotion ? 0 : MotionTokens.activityHandoff
                            easing.type: MotionTokens.easeOut
                        }
                    }
                }
                Behavior on opacity {
                    NumberAnimation { duration: controller.reducedMotion ? 0 : MotionTokens.state }
                }
            }

            Row {
                id: compactMediaPeek
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -1
                spacing: 7
                enabled: window.compactActivity === "media"
                visible: opacity > 0.001
                opacity: enabled ? 1 : 0
                transform: Translate {
                    y: compactMediaPeek.enabled ? 0 : 3
                    Behavior on y {
                        NumberAnimation {
                            duration: controller.reducedMotion ? 0 : MotionTokens.activityHandoff
                            easing.type: MotionTokens.easeOut
                        }
                    }
                }

                Behavior on opacity {
                    NumberAnimation { duration: controller.reducedMotion ? 0 : MotionTokens.state }
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 27
                    height: 27
                    radius: Math.min(width, height) * 0.36
                    color: colors.raised

                    MorphingArtwork {
                        id: compactArtwork
                        anchors.fill: parent
                        source: controller.mediaArtworkUrl
                        reducedMotion: controller.reducedMotion
                        cornerRadius: parent.radius
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: opacity > 0.001
                        opacity: compactArtwork.hasArtwork ? 0 : 1
                        scale: compactArtwork.hasArtwork ? 0.9 : 1
                        text: "\uE8D6"
                        color: colors.secondary
                        font.family: window.iconFont
                        font.pixelSize: 12

                        Behavior on opacity {
                            NumberAnimation {
                                duration: controller.reducedMotion ? 0 : MotionTokens.state
                            }
                        }
                        Behavior on scale {
                            NumberAnimation {
                                duration: controller.reducedMotion ? 0 : MotionTokens.state
                                easing.type: MotionTokens.easeOut
                            }
                        }
                    }
                }

                MorphingLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 145
                    text: controller.mediaTitle
                    color: colors.text
                    elide: Text.ElideRight
                    fontFamily: window.uiFont
                    fontPixelSize: 10
                    fontWeight: Font.DemiBold
                    motionOffset: 2
                    reducedMotion: controller.reducedMotion
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Repeater {
                        model: 5
                        Rectangle {
                            required property int index
                            readonly property real audioLevel:
                                controller.audioPeakLevels.length > index
                                ? controller.audioPeakLevels[index] : 0
                            readonly property real meterGain:
                                [1.16, 0.88, 1.30, 0.94, 1.20][index]
                            readonly property real expressiveLevel:
                                Math.min(1, audioLevel * meterGain)
                            width: 3
                            height: 15
                            radius: 1.5
                            color: controller.mediaArtworkAccent.length > 0
                                   ? controller.mediaArtworkAccent : colors.green
                            transformOrigin: Item.Center
                            scale: controller.mediaPlaying && !controller.muted
                                   ? 0.08 + Math.pow(expressiveLevel, 0.66) * 0.92 : 0.08

                            Behavior on scale {
                                NumberAnimation {
                                    duration: controller.reducedMotion ? 0
                                              : 150 + Math.abs(index - 2) * 24
                                    easing.type: Easing.OutCubic
                                }
                            }
                            Behavior on color {
                                ColorAnimation {
                                    duration: controller.reducedMotion ? 0 : MotionTokens.content
                                    easing.type: Easing.OutCubic
                                }
                            }
                        }
                    }
                }
            }

            Row {
                id: compactTimer
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -1
                spacing: 7
                enabled: window.compactActivity === "timer"
                visible: opacity > 0.001
                opacity: enabled ? 1 : 0
                transform: Translate {
                    y: compactTimer.enabled ? 0 : 3
                    Behavior on y { NumberAnimation { duration: controller.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }
                }
                Behavior on opacity { NumberAnimation { duration: controller.reducedMotion ? 0 : MotionTokens.state } }

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
                RollingDigits {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !controller.timerRinging
                    text: controller.timerRemainingText
                    color: colors.text
                    fontFamily: window.uiFont
                    fontPixelSize: 16
                    fontWeight: Font.DemiBold
                    letterSpacing: -0.2
                    reducedMotion: controller.reducedMotion
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: controller.timerRinging
                    text: "Time's up"
                    color: colors.timer
                    font.family: window.uiFont
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 5
                    height: 5
                    radius: 3
                    color: colors.timer
                    opacity: controller.timerPaused ? 0.38 : 1

                    Behavior on opacity {
                        NumberAnimation {
                            duration: controller.reducedMotion ? 0 : MotionTokens.state
                        }
                    }
                }
            }

            Row {
                id: compactCodex
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -1
                spacing: 8
                enabled: window.compactActivity === "codex"
                visible: opacity > 0.001
                opacity: enabled ? 1 : 0
                transform: Translate {
                    y: compactCodex.enabled ? 0 : 3
                    Behavior on y { NumberAnimation { duration: controller.reducedMotion ? 0 : MotionTokens.activityHandoff; easing.type: MotionTokens.easeOut } }
                }
                Behavior on opacity { NumberAnimation { duration: controller.reducedMotion ? 0 : MotionTokens.state } }

                MorphingIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    iconWidth: 14
                    iconHeight: 14
                    reducedMotion: controller.reducedMotion
                    source: codexBridge.awaitingApproval
                            ? Qt.resolvedUrl("../assets/icons/codex-approval-amber.svg")
                            : (codexBridge.phase === "completed"
                               ? Qt.resolvedUrl("../assets/icons/codex-complete-green.svg")
                               : Qt.resolvedUrl("../assets/icons/codex-terminal-blue.svg"))
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    MorphingLabel {
                        width: 158
                        text: codexBridge.statusText
                        color: colors.text
                        elide: Text.ElideRight
                        fontFamily: window.uiFont
                        fontPixelSize: 10
                        fontWeight: Font.DemiBold
                        reducedMotion: controller.reducedMotion
                    }
                    MorphingLabel {
                        width: 158
                        text: codexBridge.activityText
                        color: colors.tertiary
                        elide: Text.ElideRight
                        fontFamily: window.monoFont
                        fontPixelSize: 8
                        fontWeight: Font.Normal
                        reducedMotion: controller.reducedMotion
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: codexBridge.active
                    text: codexBridge.elapsedText
                    color: colors.tertiary
                    font.family: window.monoFont
                    font.pixelSize: 8
                    font.features: { "tnum": 1 }
                }
            }
        }

        TimerSatellite {
            id: timerSatellite
            z: 7
            x: parent.width + window.shellHorizontalOverflow
               + window.timerSatelliteGap
            y: 0
            width: window.timerSatelliteDiameter
            height: width
            controller: window.islandController
            colors: window.colors
            reducedMotion: controller.reducedMotion
            glassEnabled: window.liquidGlassVisualActive
            glassBackdrop: liquidGlassBackdrop
            active: window.timerSatelliteVisible
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
                    text: controller.timeText
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
            expanded: controller.expanded && !appLauncher.open
                      && !controller.monitorDetailsOpen
            dragActive: window.dragActive
            reducedMotion: controller.reducedMotion
            morphProgress: window.morphProgress
            shellCornerRadius: window.dynamicCornerRadius
            tilingFeedbackActive: window.tilingFeedbackActive
            codexOpen: codexBridge.panelOpen
            monoFont: window.monoFont
        }

        MonitorDetailsPanel {
            id: monitorDetailsPanel
            z: 10
            anchors.fill: parent
            controller: window.islandController
            colors: window.colors
            uiFont: window.uiFont
            monoFont: window.monoFont
            iconFont: window.iconFont
            reducedMotion: controller.reducedMotion
            open: controller.expanded && controller.monitorDetailsOpen
                  && !window.dragActive && !appLauncher.open
        }

        CodexPanel {
            id: codexPanel
            z: 20
            anchors.fill: parent
            colors: window.colors
            uiFont: window.uiFont
            monoFont: window.monoFont
            iconFont: window.iconFont
            reducedMotion: controller.reducedMotion
            open: controller.expanded && codexBridge.panelOpen
                  && !controller.monitorDetailsOpen
                  && !window.dragActive && !appLauncher.open
        }

        AppLauncherPanel {
            id: appLauncherPanel
            z: 30
            anchors.fill: parent
            colors: window.colors
            uiFont: window.uiFont
            iconFont: window.iconFont
            focusAccent: controller.mediaPlaying
                         && controller.mediaArtworkAccent.length > 0
                         ? controller.mediaArtworkAccent
                         : window.colors.accent
            reducedMotion: controller.reducedMotion
            open: appLauncher.open && !window.dragActive
        }

        Item {
            id: dragContent
            z: 5
            anchors.fill: parent
            enabled: window.dragActive
            opacity: enabled ? 1 : 0
            scale: enabled ? 1 : 0.97

            Behavior on opacity {
                NumberAnimation { duration: controller.reducedMotion ? 0 : MotionTokens.state }
            }
            Behavior on scale {
                NumberAnimation { duration: controller.reducedMotion ? 0 : MotionTokens.content; easing.type: MotionTokens.easeOut }
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
            x: -window.shellHorizontalOverflow
            width: parent.width + window.shellHorizontalOverflow * 2
            height: window.surfaceHeight
            keys: ["text/uri-list"]
            onDropped: function(drop) {
                controller.handleDrop(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }
}
