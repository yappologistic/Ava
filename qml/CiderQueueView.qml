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
        incomingLayer.y = 20
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

        ListView {
            id: queueReel
            anchors.fill: parent
            model: root.active ? parent.entries : []
            interactive: parent.interactive
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickDeceleration: 5200
            maximumFlickVelocity: 1100
            snapMode: ListView.SnapToItem
            reuseItems: true
            cacheBuffer: 40
            currentIndex: -1

            WheelHandler {
                enabled: queueReel.interactive
                         && queueReel.contentHeight > queueReel.height
                target: null
                orientation: Qt.Vertical
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

                onWheel: function(event) {
                    wheelSnap.stop()
                    const maximum = Math.max(0, queueReel.contentHeight
                                                - queueReel.height)
                    if (event.pixelDelta.y !== 0) {
                        queueReel.contentY = Math.max(0, Math.min(maximum,
                            queueReel.contentY - event.pixelDelta.y))
                    } else if (event.angleDelta.y !== 0) {
                        const direction = event.angleDelta.y > 0 ? -1 : 1
                        const currentSlot = Math.round(queueReel.contentY / 20)
                        const targetSlot = Math.max(0, Math.min(
                            Math.ceil(maximum / 20), currentSlot + direction))
                        wheelSnap.from = queueReel.contentY
                        wheelSnap.to = Math.min(maximum, targetSlot * 20)
                        wheelSnap.restart()
                    }
                    event.accepted = true
                }
                onActiveChanged: {
                    if (!active && !wheelSnap.running) {
                        const maximum = Math.max(0, queueReel.contentHeight
                                                    - queueReel.height)
                        const slot = Math.round(queueReel.contentY / 20)
                        wheelSnap.from = queueReel.contentY
                        wheelSnap.to = Math.min(maximum, slot * 20)
                        wheelSnap.restart()
                    }
                }
            }

            NumberAnimation {
                id: wheelSnap
                target: queueReel
                property: "contentY"
                duration: root.reducedMotion ? 0 : MotionTokens.state
                easing.type: MotionTokens.easeOut
            }

            delegate: Item {
                id: queueLine
                required property int index
                required property var modelData
                x: 2
                width: queueReel.width - 4
                height: 20
                activeFocusOnTab: queueReel.interactive
                enabled: queueReel.interactive
                readonly property real viewportCenter: y + height / 2
                                                       - queueReel.contentY
                readonly property real topReveal: Math.max(0.28, Math.min(1,
                    (viewportCenter - 3) / 17))
                readonly property real bottomReveal: Math.max(0.18, Math.min(1,
                    (queueReel.height - viewportCenter + 10) / 52))
                readonly property real reelReveal: Math.min(topReveal, bottomReveal)
                readonly property bool emphasized: queueLineHover.hovered || activeFocus
                property real clarity: emphasized ? 1 : 0
                readonly property real restingOpacity: 0.94 * reelReveal
                opacity: restingOpacity + (1 - restingOpacity) * clarity
                scale: 0.995 + clarity * 0.005
                transformOrigin: Item.Center
                Accessible.name: "Play " + modelData.title + " by " + modelData.artist
                Accessible.role: Accessible.Button

                Behavior on clarity {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.hover
                        easing.type: MotionTokens.easeOut
                    }
                }

                onActiveFocusChanged: {
                    if (activeFocus)
                        queueReel.positionViewAtIndex(index, ListView.Contain)
                }

                HoverHandler { id: queueLineHover }
                TapHandler {
                    onTapped: root.provider.playQueueIndex(queueLine.modelData.index)
                }
                Keys.onReturnPressed: root.provider.playQueueIndex(queueLine.modelData.index)
                Keys.onEnterPressed: root.provider.playQueueIndex(queueLine.modelData.index)
                Keys.onSpacePressed: root.provider.playQueueIndex(queueLine.modelData.index)

                layer.enabled: root.active && !root.reducedMotion
                layer.smooth: true
                layer.effect: MultiEffect {
                    blurEnabled: true
                    blurMax: 12
                    blur: (1 - queueLine.reelReveal) * 0.32
                          * (1 - queueLine.clarity)
                    autoPaddingEnabled: false
                }

                Text {
                    x: 1
                    y: 7
                    width: 10
                    text: queueLine.index + 1
                    color: queueLineHover.hovered || queueLine.activeFocus
                           ? root.accentColor : root.colors.tertiary
                    font.family: root.monoFont
                    font.pixelSize: 7
                    font.features: { "tnum": 1 }

                    Behavior on color {
                        ColorAnimation {
                            duration: root.reducedMotion ? 0 : MotionTokens.hover
                        }
                    }
                }
                Text {
                    x: 15
                    y: 1
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
                    y: 11
                    width: parent.width - 17
                    text: queueLine.modelData.artist
                    color: root.colors.tertiary
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: root.uiFont
                    font.pixelSize: 6
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
        y: 20
        opacity: 0
    }

    ParallelAnimation {
        id: queueTransition

        NumberAnimation {
            target: outgoingLayer
            property: "y"
            from: 0
            to: -20
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
            from: 20
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
