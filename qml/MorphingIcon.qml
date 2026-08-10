import QtQuick 6.5
import Ava 1.0

Item {
    id: root

    property url source
    property bool reducedMotion: false
    property real iconWidth: width
    property real iconHeight: height
    property url displayedSource
    property url incomingSource
    property bool ready: false

    function resetVisuals() {
        outgoing.opacity = 1
        outgoing.scale = 1
        incoming.opacity = 0
        incoming.scale = 0.82
    }

    Component.onCompleted: {
        displayedSource = source
        ready = true
        resetVisuals()
    }

    onSourceChanged: {
        if (!ready || reducedMotion) {
            transition.stop()
            displayedSource = source
            incomingSource = ""
            resetVisuals()
            return
        }
        if (source === displayedSource)
            return
        transition.stop()
        incomingSource = source
        incoming.opacity = 0
        incoming.scale = 0.82
        transition.start()
    }

    Image {
        id: outgoing
        anchors.centerIn: parent
        width: root.iconWidth
        height: root.iconHeight
        source: root.displayedSource
        sourceSize.width: Math.ceil(root.iconWidth * 2)
        sourceSize.height: Math.ceil(root.iconHeight * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    Image {
        id: incoming
        anchors.centerIn: parent
        width: root.iconWidth
        height: root.iconHeight
        source: root.incomingSource
        sourceSize.width: Math.ceil(root.iconWidth * 2)
        sourceSize.height: Math.ceil(root.iconHeight * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    SequentialAnimation {
        id: transition
        ParallelAnimation {
            NumberAnimation {
                target: outgoing
                property: "opacity"
                to: 0
                duration: MotionTokens.press
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                target: outgoing
                property: "scale"
                to: 0.82
                duration: MotionTokens.state
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                target: incoming
                property: "opacity"
                to: 1
                duration: MotionTokens.state
                easing.type: MotionTokens.easeOut
            }
            NumberAnimation {
                target: incoming
                property: "scale"
                to: 1
                duration: MotionTokens.content
                easing.type: MotionTokens.easeOut
            }
        }
        ScriptAction {
            script: {
                root.displayedSource = root.incomingSource
                root.incomingSource = ""
                root.resetVisuals()
            }
        }
    }
}
