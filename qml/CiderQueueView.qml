pragma ComponentBehavior: Bound

import QtQuick 6.5
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
    property bool editing: false
    property int dragSourceRow: -1
    property int dragTargetRow: -1

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
            parts.push(entry.index + "\u001f" + entry.id + "\u001f"
                       + entry.title + "\u001f" + entry.artist + "\u001f"
                       + entry.artworkUrl)
        }
        return parts.join("\u001e")
    }

    function reorderPreviewOffset(row) {
        if (!editing || dragSourceRow < 0 || dragTargetRow < 0
                || dragSourceRow === dragTargetRow || row === dragSourceRow)
            return 0
        if (dragTargetRow > dragSourceRow
                && row > dragSourceRow && row <= dragTargetRow)
            return -20
        if (dragTargetRow < dragSourceRow
                && row >= dragTargetRow && row < dragSourceRow)
            return 20
        return 0
    }

    function finishReorder(sourceIndex, targetIndex) {
        provider.moveQueueIndex(sourceIndex, targetIndex)
        const snapshot = queueSnapshot()
        dragSourceRow = -1
        dragTargetRow = -1
        editing = false
        displayedSignature = queueSignature(snapshot)
        displayedQueue = snapshot
        incomingQueue = []
        resetLayers()
    }

    function cancelReorder() {
        dragSourceRow = -1
        dragTargetRow = -1
        editing = false
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
        if (editing)
            return
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
                property real dragOffset: 0
                property real reorderOffset: root.reorderPreviewOffset(index)
                property int settleSourceIndex: -1
                property int settleTargetIndex: -1
                readonly property real restingOpacity: 0.94 * reelReveal
                opacity: restingOpacity + (1 - restingOpacity) * clarity
                scale: 0.995 + clarity * 0.005
                z: reorderDrag.active ? 2 : 0
                transformOrigin: Item.Center
                transform: Translate {
                    y: queueLine.dragOffset + queueLine.reorderOffset
                }
                Accessible.name: "Play " + modelData.title + " by "
                                 + modelData.artist
                                 + (root.provider.queueEditable
                                    ? ". Delete removes it; Alt plus arrow moves it"
                                    : "")
                Accessible.role: Accessible.Button

                Behavior on clarity {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.hover
                        easing.type: MotionTokens.easeOut
                    }
                }

                Behavior on reorderOffset {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0
                                                    : MotionTokens.directSettle
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
                Keys.onDeletePressed: {
                    if (root.provider.queueEditable)
                        root.provider.removeQueueIndex(modelData.index)
                }
                Keys.onPressed: function(event) {
                    if (!root.provider.queueEditable
                            || !(event.modifiers & Qt.AltModifier))
                        return
                    if (event.key === Qt.Key_Up && index > 0) {
                        root.provider.moveQueueIndex(modelData.index,
                            queueReel.model[index - 1].index)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Down
                               && index + 1 < queueReel.count) {
                        root.provider.moveQueueIndex(modelData.index,
                            queueReel.model[index + 1].index)
                        event.accepted = true
                    }
                }

                Rectangle {
                    x: 1
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    radius: 3
                    color: Qt.rgba(root.accentColor.r, root.accentColor.g,
                                   root.accentColor.b, 0.22)

                    Image {
                        anchors.fill: parent
                        visible: source.toString().length > 0
                        source: queueLine.modelData.artworkUrl ?? ""
                        sourceSize: Qt.size(28, 28)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        smooth: true
                    }
                }
                Text {
                    x: 21
                    y: 1
                    width: parent.width - (root.provider.queueEditable ? 49 : 23)
                    text: queueLine.modelData.title
                    color: root.colors.text
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: root.uiFont
                    font.pixelSize: 8
                    font.weight: Font.Medium
                }
                Text {
                    x: 21
                    y: 11
                    width: parent.width - (root.provider.queueEditable ? 49 : 23)
                    text: queueLine.modelData.artist
                    color: root.colors.tertiary
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: root.uiFont
                    font.pixelSize: 6
                }

                Item {
                    id: reorderHandle
                    visible: root.provider.queueEditable
                    anchors.right: removeHandle.left
                    anchors.rightMargin: 1
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 18
                    opacity: reorderDrag.active ? 1
                             : (queueLineHover.hovered || queueLine.activeFocus
                                ? 0.68 : 0.20)
                    Accessible.name: "Move " + queueLine.modelData.title

                    Behavior on opacity {
                        NumberAnimation {
                            duration: root.reducedMotion ? 0 : MotionTokens.hover
                        }
                    }

                    Image {
                        anchors.centerIn: parent
                        width: 11
                        height: 11
                        source: Qt.resolvedUrl("../assets/icons/fluent-media/reorder-regular.svg")
                        sourceSize: Qt.size(22, 22)
                    }

                    DragHandler {
                        id: reorderDrag
                        target: null
                        xAxis.enabled: false
                        yAxis.enabled: true

                        onActiveTranslationChanged: {
                            queueLine.dragOffset = activeTranslation.y
                            root.dragTargetRow = Math.max(0, Math.min(
                                queueReel.count - 1,
                                Math.round((queueLine.y
                                            + activeTranslation.y) / 20)))
                        }
                        onActiveChanged: {
                            if (active) {
                                root.editing = true
                                root.dragSourceRow = queueLine.index
                                root.dragTargetRow = queueLine.index
                                queueLine.forceActiveFocus()
                                return
                            }
                            const destination = root.dragTargetRow
                            const targetEntry = queueReel.model[destination]
                            const sourceIndex = queueLine.modelData.index
                            queueLine.settleSourceIndex = sourceIndex
                            queueLine.settleTargetIndex = targetEntry
                                    ? targetEntry.index : sourceIndex
                            dragSettle.to = (destination - queueLine.index) * 20
                            dragSettle.restart()
                        }
                    }

                    NumberAnimation {
                        id: dragSettle
                        target: queueLine
                        property: "dragOffset"
                        duration: root.reducedMotion ? 0
                                                    : MotionTokens.directSettle
                        easing.type: MotionTokens.easeOut
                        onFinished: {
                            const sourceIndex = queueLine.settleSourceIndex
                            const targetIndex = queueLine.settleTargetIndex
                            queueLine.dragOffset = 0
                            queueLine.settleSourceIndex = -1
                            queueLine.settleTargetIndex = -1
                            if (sourceIndex >= 0 && targetIndex >= 0
                                    && sourceIndex !== targetIndex) {
                                root.finishReorder(sourceIndex, targetIndex)
                            } else {
                                root.cancelReorder()
                            }
                        }
                    }
                }

                Item {
                    id: removeHandle
                    visible: root.provider.queueEditable
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 18
                    opacity: removeHover.hovered || queueLine.activeFocus
                             ? 0.74 : 0
                    Accessible.name: "Remove " + queueLine.modelData.title
                    Accessible.role: Accessible.Button

                    Behavior on opacity {
                        NumberAnimation {
                            duration: root.reducedMotion ? 0 : MotionTokens.hover
                        }
                    }

                    HoverHandler { id: removeHover }
                    TapHandler {
                        onTapped: root.provider.removeQueueIndex(
                                      queueLine.modelData.index)
                    }
                    Image {
                        anchors.centerIn: parent
                        width: 10
                        height: 10
                        source: Qt.resolvedUrl("../assets/icons/fluent-media/delete-regular.svg")
                        sourceSize: Qt.size(20, 20)
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
