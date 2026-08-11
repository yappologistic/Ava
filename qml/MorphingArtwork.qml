pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Effects 6.5
import Ava 1.0

Item {
    id: root

    property url source
    property bool reducedMotion: false
    property int fillMode: Image.PreserveAspectCrop
    property real cornerRadius: 0
    property url displayedSource
    property url incomingSource
    property bool ready: false
    property bool waitingForIncoming: false
    readonly property bool hasArtwork: displayedSource.toString().length > 0
                                         || incomingSource.toString().length > 0

    function resetVisuals() {
        outgoing.opacity = 1
        outgoing.scale = 1
        incoming.opacity = 0
        incoming.scale = 0.96
    }

    function commitImmediately() {
        handoff.stop()
        waitingForIncoming = false
        displayedSource = source
        incomingSource = ""
        resetVisuals()
    }

    function beginHandoff() {
        if (!ready || reducedMotion) {
            commitImmediately()
            return
        }
        if (source.toString() === displayedSource.toString())
            return

        if (handoff.running && incoming.opacity >= outgoing.opacity
                && incomingSource.toString().length > 0)
            displayedSource = incomingSource
        handoff.stop()
        waitingForIncoming = false
        incomingSource = source
        resetVisuals()

        if (incomingSource.toString().length === 0)
            handoff.start()
        else {
            waitingForIncoming = true
            if (incoming.status === Image.Ready) {
                waitingForIncoming = false
                handoff.start()
            }
        }
    }

    Component.onCompleted: {
        displayedSource = source
        ready = true
        resetVisuals()
    }

    onSourceChanged: beginHandoff()
    onReducedMotionChanged: {
        if (reducedMotion)
            commitImmediately()
    }

    Accessible.ignored: true

    Item {
        id: artworkSource
        anchors.fill: parent
        visible: false
        layer.enabled: true

        Image {
            id: outgoing
            anchors.fill: parent
            source: root.displayedSource
            fillMode: root.fillMode
            asynchronous: true
            cache: false
            smooth: true
            mipmap: true
        }

        Image {
            id: incoming
            anchors.fill: parent
            source: root.incomingSource
            fillMode: root.fillMode
            asynchronous: true
            cache: false
            smooth: true
            mipmap: true
            opacity: 0
            scale: 0.96

            onStatusChanged: {
                if (!root.waitingForIncoming)
                    return
                if (status === Image.Ready) {
                    root.waitingForIncoming = false
                    handoff.start()
                } else if (status === Image.Error) {
                    root.waitingForIncoming = false
                    root.displayedSource = ""
                    root.incomingSource = ""
                    root.resetVisuals()
                }
            }
        }
    }

    Rectangle {
        id: artworkMask
        anchors.fill: parent
        radius: Math.max(0, root.cornerRadius)
        color: "white"
        antialiasing: true
        visible: false
        layer.enabled: true
        layer.smooth: true
        layer.samples: 4
    }

    MultiEffect {
        anchors.fill: parent
        source: artworkSource
        maskEnabled: true
        maskSource: artworkMask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }

    ParallelAnimation {
        id: handoff

        NumberAnimation {
            target: outgoing
            property: "opacity"
            to: 0
            duration: MotionTokens.state
            easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: outgoing
            property: "scale"
            to: 1.01
            duration: MotionTokens.content
            easing.type: MotionTokens.easeOut
        }
        NumberAnimation {
            target: incoming
            property: "opacity"
            to: 1
            duration: MotionTokens.content
            easing.type: MotionTokens.easeOut
        }
        NumberAnimation {
            target: incoming
            property: "scale"
            from: 0.96
            to: 1
            duration: MotionTokens.content
            easing.type: MotionTokens.easeOut
        }

        onFinished: {
            root.displayedSource = root.incomingSource
            root.incomingSource = ""
            root.resetVisuals()
        }
    }
}
