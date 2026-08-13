pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Effects 6.5
import Ava 1.0

Item {
    id: root

    required property var provider
    required property var colors
    required property string uiFont
    required property bool reducedMotion
    property bool active: false
    property var displayedLines: []
    property var incomingLines: []
    property string displayedSignature: ""
    property bool ready: false

    clip: true

    function lyricSnapshot() {
        if (!provider.lyricsAvailable || !provider.lyricsSynchronized)
            return []

        const rows = []
        rows.push({ text: provider.previousLyric, role: "previous" })

        let focusText = provider.currentLyric
        let upcomingStart = 0
        if (focusText.length === 0 && provider.upcomingLyrics.length > 0) {
            focusText = provider.upcomingLyrics[0]
            upcomingStart = 1
        }
        rows.push({ text: focusText, role: "current" })

        for (let index = upcomingStart;
             index < provider.upcomingLyrics.length && rows.length < 5;
             ++index) {
            rows.push({ text: provider.upcomingLyrics[index], role: "upcoming" })
        }
        return rows
    }

    function lyricSignature(rows) {
        const parts = []
        for (let index = 0; index < rows.length; ++index)
            parts.push(rows[index].role + "\u001f" + rows[index].text)
        return parts.join("\u001e")
    }

    function resetLayers() {
        outgoingLayer.y = 0
        outgoingLayer.opacity = 1
        incomingLayer.y = 18
        incomingLayer.opacity = 0
    }

    function settleTransition() {
        if (!lyricTransition.running)
            return
        lyricTransition.stop()
        displayedLines = incomingLines
        incomingLines = []
        resetLayers()
    }

    function syncLyrics() {
        const snapshot = lyricSnapshot()
        const signature = lyricSignature(snapshot)
        if (ready && signature === displayedSignature)
            return

        displayedSignature = signature
        if (!ready || reducedMotion || !active) {
            settleTransition()
            displayedLines = snapshot
            incomingLines = []
            resetLayers()
            ready = true
            return
        }

        settleTransition()
        incomingLines = snapshot
        resetLayers()
        lyricTransition.start()
    }

    onActiveChanged: {
        if (active)
            syncLyrics()
        else
            settleTransition()
    }

    onReducedMotionChanged: {
        if (reducedMotion)
            settleTransition()
    }

    Component.onCompleted: syncLyrics()

    Connections {
        target: root.provider
        function onChanged() { root.syncLyrics() }
    }

    Text {
        anchors.centerIn: parent
        visible: root.active && !root.provider.lyricsAvailable
                 && !lyricTransition.running
        text: "Lyrics unavailable for this track"
        color: root.colors.tertiary
        elide: Text.ElideRight
        font.family: root.uiFont
        font.pixelSize: 8
    }

    Text {
        x: 4
        y: 31
        width: parent.width - 8
        visible: root.active && root.provider.lyricsAvailable
                 && !root.provider.lyricsSynchronized
        text: root.provider.currentLyric
        color: root.colors.text
        elide: Text.ElideRight
        font.family: root.uiFont
        font.pixelSize: 10
        font.weight: Font.DemiBold
    }

    Text {
        x: 4
        y: 50
        width: parent.width - 8
        visible: root.active && root.provider.lyricsAvailable
                 && !root.provider.lyricsSynchronized
        text: "Lyrics aren't time-synced for this track"
        color: root.colors.tertiary
        elide: Text.ElideRight
        font.family: root.uiFont
        font.pixelSize: 7
    }

    component LyricRows: Item {
        required property var entries

        Repeater {
            model: root.active ? parent.entries : []

            delegate: Text {
                id: lyricLine
                required property int index
                required property var modelData
                x: 4
                y: index * 18
                width: parent.width - 8
                text: modelData.text
                visible: text.length > 0
                color: modelData.role === "current"
                       ? root.colors.text : root.colors.tertiary
                opacity: modelData.role === "current"
                         ? 1 : (modelData.role === "previous"
                                ? 0.27 : Math.max(0.12, 0.48 - (index - 2) * 0.16))
                elide: Text.ElideRight
                font.family: root.uiFont
                font.pixelSize: modelData.role === "current"
                                ? 10 : (index < 3 ? 8 : 7)
                font.weight: modelData.role === "current"
                             ? Font.DemiBold : Font.Medium
                layer.enabled: root.active && !root.reducedMotion
                               && modelData.role !== "current"
                layer.smooth: true
                layer.effect: MultiEffect {
                    blurEnabled: true
                    blurMax: 12
                    blur: lyricLine.modelData.role === "previous"
                          ? 0.20 : Math.min(0.38, 0.12 + (lyricLine.index - 2) * 0.10)
                    autoPaddingEnabled: false
                }
            }
        }
    }

    LyricRows {
        id: outgoingLayer
        anchors.fill: parent
        entries: root.displayedLines
        visible: root.provider.lyricsSynchronized
    }

    LyricRows {
        id: incomingLayer
        anchors.fill: parent
        entries: root.incomingLines
        visible: root.provider.lyricsSynchronized
        y: 18
        opacity: 0
    }

    ParallelAnimation {
        id: lyricTransition

        NumberAnimation {
            target: outgoingLayer
            property: "y"
            from: 0
            to: -18
            duration: root.reducedMotion ? 0 : MotionTokens.content
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            target: outgoingLayer
            property: "opacity"
            from: 1
            to: 0
            duration: root.reducedMotion ? 0 : MotionTokens.state
        }
        NumberAnimation {
            target: incomingLayer
            property: "y"
            from: 18
            to: 0
            duration: root.reducedMotion ? 0 : MotionTokens.content
            easing.type: MotionTokens.easeOut
        }
        NumberAnimation {
            target: incomingLayer
            property: "opacity"
            from: 0
            to: 1
            duration: root.reducedMotion ? 0 : MotionTokens.content
            easing.type: MotionTokens.easeOut
        }

        onFinished: {
            root.displayedLines = root.incomingLines
            root.incomingLines = []
            root.resetLayers()
        }
    }
}
