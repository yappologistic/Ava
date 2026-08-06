import QtQuick 6.5

Item {
    id: root

    property string text: ""
    property color color: "#a1a1a6"
    property string fontFamily: "Inter"
    property int fontPixelSize: 9
    property int fontWeight: Font.DemiBold
    property real letterSpacing: 0
    property bool reducedMotion: false
    property string displayedText: ""
    property string incomingText: ""
    property bool ready: false

    implicitWidth: Math.max(currentMetrics.advanceWidth, incomingMetrics.advanceWidth)
    implicitHeight: Math.ceil(typeMetrics.height)
    width: implicitWidth
    height: implicitHeight
    clip: true

    Accessible.name: root.text
    Accessible.role: Accessible.StaticText

    FontMetrics {
        id: typeMetrics
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
    }

    TextMetrics {
        id: currentMetrics
        text: root.displayedText
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
    }

    TextMetrics {
        id: incomingMetrics
        text: root.incomingText
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
    }

    Component.onCompleted: {
        displayedText = text
        ready = true
    }

    onTextChanged: {
        if (!ready || reducedMotion) {
            transition.stop()
            displayedText = text
            incomingText = ""
            outgoing.opacity = 1
            outgoing.y = 0
            incoming.opacity = 0
            return
        }
        if (text === displayedText)
            return
        transition.stop()
        incomingText = text
        incoming.y = 3
        incoming.opacity = 0
        transition.start()
    }

    Text {
        id: outgoing
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        text: root.displayedText
        color: root.color
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
        font.letterSpacing: root.letterSpacing
    }

    Text {
        id: incoming
        anchors.horizontalCenter: parent.horizontalCenter
        y: 3
        opacity: 0
        text: root.incomingText
        color: root.color
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
        font.letterSpacing: root.letterSpacing
    }

    SequentialAnimation {
        id: transition
        ParallelAnimation {
            NumberAnimation { target: outgoing; property: "opacity"; to: 0; duration: MotionTokens.press }
            NumberAnimation { target: outgoing; property: "y"; to: -3; duration: MotionTokens.press; easing.type: Easing.InCubic }
            NumberAnimation { target: incoming; property: "opacity"; to: 1; duration: MotionTokens.state }
            NumberAnimation { target: incoming; property: "y"; to: 0; duration: MotionTokens.state; easing.type: MotionTokens.easeOut }
        }
        ScriptAction {
            script: {
                root.displayedText = root.incomingText
                root.incomingText = ""
                outgoing.opacity = 1
                outgoing.y = 0
                incoming.opacity = 0
                incoming.y = 3
            }
        }
    }
}
