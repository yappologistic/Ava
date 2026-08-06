pragma ComponentBehavior: Bound

import QtQuick 6.5

Item {
    id: root

    property string text: ""
    property color color: "#f5f5f7"
    property string fontFamily: "Inter"
    property int fontPixelSize: 16
    property int fontWeight: Font.Normal
    property real letterSpacing: 0
    property bool reducedMotion: false
    // Positive values roll forward; negative values roll backward for countdowns.
    property int rollDirection: 1

    readonly property real digitWidth: Math.ceil(digitMetrics.advanceWidth)
    readonly property real colonWidth: Math.ceil(colonMetrics.advanceWidth)
    readonly property real rollDistance: Math.max(8, Math.round(implicitHeight * 0.84))

    function isDigit(character) {
        return character >= "0" && character <= "9"
    }

    function characterWidth(character) {
        return character === ":" ? colonWidth : digitWidth
    }

    function measuredWidth(value) {
        let result = 0
        for (let index = 0; index < value.length; ++index)
            result += characterWidth(value.charAt(index))
        if (value.length > 1)
            result += letterSpacing * (value.length - 1)
        return Math.max(0, result)
    }

    implicitWidth: measuredWidth(text)
    implicitHeight: Math.ceil(typeMetrics.height)
    width: implicitWidth
    height: implicitHeight
    clip: true

    Accessible.name: text
    Accessible.role: Accessible.StaticText

    FontMetrics {
        id: typeMetrics
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
    }

    TextMetrics {
        id: digitMetrics
        text: "0"
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
        font.features: { "tnum": 1 }
    }

    TextMetrics {
        id: colonMetrics
        text: ":"
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
    }

    Row {
        anchors.fill: parent
        spacing: root.letterSpacing

        Repeater {
            model: root.text.length

            Item {
                id: digitCell
                required property int index
                property string observedCharacter: root.text.charAt(index)
                property string settledCharacter: ""
                property string incomingCharacter: ""
                property bool ready: false

                width: root.characterWidth(observedCharacter)
                height: root.height
                clip: true

                Component.onCompleted: {
                    settledCharacter = observedCharacter
                    ready = true
                }

                onObservedCharacterChanged: {
                    if (!ready || root.reducedMotion
                            || !root.isDigit(settledCharacter)
                            || !root.isDigit(observedCharacter)) {
                        roll.stop()
                        settledCharacter = observedCharacter
                        incomingCharacter = ""
                        outgoing.y = 0
                        outgoing.opacity = 1
                        incoming.opacity = 0
                        return
                    }
                    if (settledCharacter === observedCharacter)
                        return

                    roll.stop()
                    incomingCharacter = observedCharacter
                    incoming.y = root.rollDirection * root.rollDistance
                    incoming.opacity = 0
                    outgoing.y = 0
                    outgoing.opacity = 1
                    roll.start()
                }

                Text {
                    id: outgoing
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 0
                    text: digitCell.settledCharacter
                    color: root.color
                    font.family: root.fontFamily
                    font.pixelSize: root.fontPixelSize
                    font.weight: root.fontWeight
                    font.features: { "tnum": 1 }
                    renderType: Text.QtRendering
                }

                Text {
                    id: incoming
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: root.rollDirection * root.rollDistance
                    text: digitCell.incomingCharacter
                    color: root.color
                    opacity: 0
                    font.family: root.fontFamily
                    font.pixelSize: root.fontPixelSize
                    font.weight: root.fontWeight
                    font.features: { "tnum": 1 }
                    renderType: Text.QtRendering
                }

                ParallelAnimation {
                    id: roll

                    NumberAnimation {
                        target: outgoing
                        property: "y"
                        to: -root.rollDirection * root.rollDistance
                        duration: MotionTokens.content
                        easing.type: Easing.InOutCubic
                    }
                    NumberAnimation {
                        target: outgoing
                        property: "opacity"
                        to: 0
                        duration: MotionTokens.content
                        easing.type: Easing.InOutCubic
                    }
                    NumberAnimation {
                        target: incoming
                        property: "y"
                        to: 0
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

                    onFinished: {
                        digitCell.settledCharacter = digitCell.incomingCharacter
                        digitCell.incomingCharacter = ""
                        outgoing.y = 0
                        outgoing.opacity = 1
                        incoming.opacity = 0
                    }
                }
            }
        }
    }
}
