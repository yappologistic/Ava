pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Effects 6.5
import Ava 1.0

Item {
    id: root

    required property var provider
    required property var colors
    required property color accentColor
    required property string uiFont
    required property string monoFont
    required property bool reducedMotion
    property bool active: false
    property var displayedQueue: []
    property var incomingQueue: []
    property string displayedSignature: ""
    property bool ready: false

    clip: true

    function queueSnapshot() {
        const snapshot = []
        const source = provider.queue
        for (let index = 0; index < source.length; ++index)
            snapshot.push(source[index])
        return snapshot
    }

    function queueSignature(entries) {
        const parts = []
        for (let index = 0; index < entries.length; ++index) {
            const entry = entries[index]
            parts.push(entry.index + "\u001f" + entry.title + "\u001f" + entry.artist)
        }
        return parts.join("\u001e")
    }

    function resetLayers() {
        outgoingLayer.y = 0
        outgoingLayer.opacity = 1
        incomingLayer.y = 7
        incomingLayer.opacity = 0
    }

    function settleTransition() {
        if (!queueTransition.running)
            return
        queueTransition.stop()
        displayedQueue = incomingQueue
        incomingQueue = []
        resetLayers()
    }

    function syncQueue() {
        const snapshot = queueSnapshot()
        const signature = queueSignature(snapshot)
        if (ready && signature === displayedSignature)
            return

        displayedSignature = signature
        if (!ready || reducedMotion || !active) {
            settleTransition()
            displayedQueue = snapshot
            incomingQueue = []
            resetLayers()
            ready = true
            return
        }

        settleTransition()
        incomingQueue = snapshot
        resetLayers()
        queueTransition.start()
    }

    onActiveChanged: {
        if (active)
            syncQueue()
        else
            settleTransition()
    }

    onReducedMotionChanged: {
        if (reducedMotion)
            settleTransition()
    }

    Component.onCompleted: syncQueue()

    Connections {
        target: root.provider
        function onChanged() { root.syncQueue() }
    }

    Text {
        anchors.centerIn: parent
        visible: root.active && root.displayedQueue.length === 0
                 && !queueTransition.running
        text: root.provider.queueAvailable ? "Nothing else in the queue" : "Queue unavailable"
        color: root.colors.tertiary
        font.family: root.uiFont
        font.pixelSize: 8
    }

    component QueueRows: Item {
        required property var entries
        property bool interactive: false

        Repeater {
            model: root.active ? parent.entries : []

            delegate: Item {
                id: queueLine
                required property int index
                required property var modelData
                x: 2
                y: index * 29
                width: parent.width - 4
                height: 29
                activeFocusOnTab: parent.interactive
                enabled: parent.interactive
                opacity: queueLineHover.hovered || activeFocus
                         ? 1 : (index === 0 ? 0.92 : 0.30)
                Accessible.name: "Play " + modelData.title + " by " + modelData.artist
                Accessible.role: Accessible.Button

                HoverHandler { id: queueLineHover }
                TapHandler {
                    onTapped: root.provider.playQueueIndex(queueLine.modelData.index)
                }
                Keys.onReturnPressed: root.provider.playQueueIndex(queueLine.modelData.index)
                Keys.onEnterPressed: root.provider.playQueueIndex(queueLine.modelData.index)
                Keys.onSpacePressed: root.provider.playQueueIndex(queueLine.modelData.index)

                layer.enabled: root.active && !root.reducedMotion && index > 0
                layer.smooth: true
                layer.effect: MultiEffect {
                    blurEnabled: true
                    blurMax: 12
                    blur: queueLineHover.hovered || queueLine.activeFocus
                          ? 0 : Math.min(0.36, 0.14 + queueLine.index * 0.10)
                    autoPaddingEnabled: false

                    Behavior on blur {
                        NumberAnimation {
                            duration: root.reducedMotion ? 0 : MotionTokens.hover
                            easing.type: MotionTokens.easeOut
                        }
                    }
                }

                Text {
                    x: 1
                    y: 5
                    width: 10
                    text: queueLine.index + 1
                    color: queueLineHover.hovered || queueLine.activeFocus
                           ? root.accentColor : root.colors.tertiary
                    font.family: root.monoFont
                    font.pixelSize: 7
                    font.features: { "tnum": 1 }
                }
                Text {
                    x: 15
                    y: 3
                    width: parent.width - 17
                    text: queueLine.modelData.title
                    color: root.colors.text
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: root.uiFont
                    font.pixelSize: 8
                    font.weight: Font.Medium
                }
                Text {
                    x: 15
                    y: 15
                    width: parent.width - 17
                    text: queueLine.modelData.artist
                    color: root.colors.tertiary
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: root.uiFont
                    font.pixelSize: 7
                }

                Behavior on opacity {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.hover
                        easing.type: MotionTokens.easeOut
                    }
                }
            }
        }
    }

    QueueRows {
        id: outgoingLayer
        anchors.fill: parent
        entries: root.displayedQueue
        interactive: root.active && !queueTransition.running
    }

    QueueRows {
        id: incomingLayer
        anchors.fill: parent
        entries: root.incomingQueue
        interactive: false
        y: 7
        opacity: 0
    }

    ParallelAnimation {
        id: queueTransition

        NumberAnimation {
            target: outgoingLayer
            property: "y"
            from: 0
            to: -7
            duration: root.reducedMotion ? 0 : MotionTokens.content
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            target: outgoingLayer
            property: "opacity"
            from: 1
            to: 0
            duration: root.reducedMotion ? 0 : MotionTokens.press
        }
        NumberAnimation {
            target: incomingLayer
            property: "y"
            from: 7
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
            root.displayedQueue = root.incomingQueue
            root.incomingQueue = []
            root.resetLayers()
        }
    }
}
