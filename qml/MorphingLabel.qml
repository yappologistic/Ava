import QtQuick 6.5

Item {
    id: root

    property string text: ""
    property color color: "#a1a1a6"
    property string fontFamily: "Inter"
    property int fontPixelSize: 9
    property int fontWeight: Font.DemiBold
    property real letterSpacing: 0
    property int elide: Text.ElideNone
    property int horizontalAlignment: Text.AlignLeft
    property bool reducedMotion: false
    property real motionOffset: 3
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
        font.letterSpacing: root.letterSpacing
    }

    TextMetrics {
        id: incomingMetrics
        text: root.incomingText
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
        font.letterSpacing: root.letterSpacing
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
        // A second selection can arrive before the previous morph completes.
        // Normalize both layers first so the next transition never inherits a
        // half-transparent or vertically displaced outgoing label.
        if (incomingText.length > 0)
            displayedText = incomingText
        outgoing.opacity = 1
        outgoing.y = 0
        incoming.opacity = 0
        incoming.y = root.motionOffset
        incomingText = text
        transition.start()
    }

    Text {
        id: outgoing
        x: 0
        width: parent.width
        y: 0
        text: root.displayedText
        color: root.color
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
        font.letterSpacing: root.letterSpacing
        elide: root.elide
        horizontalAlignment: root.horizontalAlignment
    }

    Text {
        id: incoming
        x: 0
        width: parent.width
        y: root.motionOffset
        opacity: 0
        text: root.incomingText
        color: root.color
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
        font.letterSpacing: root.letterSpacing
        elide: root.elide
        horizontalAlignment: root.horizontalAlignment
    }

    SequentialAnimation {
        id: transition
        ParallelAnimation {
            NumberAnimation { target: outgoing; property: "opacity"; to: 0; duration: MotionTokens.press }
            NumberAnimation { target: outgoing; property: "y"; to: -root.motionOffset; duration: MotionTokens.press; easing.type: Easing.InCubic }
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
                incoming.y = root.motionOffset
            }
        }
    }
}
