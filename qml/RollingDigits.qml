pragma ComponentBehavior: Bound

import QtQuick 6.5
import Ava 1.0

Item {
    id: root

    property string text: ""
    property color color: "#f5f5f7"
    property string fontFamily: "Inter"
    property int fontPixelSize: 16
    property int fontWeight: Font.Normal
    property real letterSpacing: 0
    property bool reducedMotion: false
    property int staggerMs: 18
    property bool staggerFromRight: true
    property string widthReference: ""
    property string slotReference: widthReference
    property int horizontalAlignment: Text.AlignLeft

    readonly property real digitWidth: Math.ceil(digitMetrics.advanceWidth)
    readonly property real colonWidth: Math.ceil(colonMetrics.advanceWidth)
    readonly property real percentWidth: Math.ceil(percentMetrics.advanceWidth)
    readonly property int cellCount: Math.max(text.length,
                                               widthReference.length,
                                               slotReference.length)

    function isDigit(character) {
        return character >= "0" && character <= "9"
    }

    function canDissolve(character) {
        return character === " " || isDigit(character)
    }

    function characterAtCell(index) {
        const offset = horizontalAlignment === Text.AlignRight
                       ? cellCount - text.length : 0
        if (index < offset || index >= offset + text.length)
            return " "
        return text.charAt(index - offset)
    }

    function characterWidth(character) {
        if (character === ":")
            return colonWidth
        if (character === "%")
            return percentWidth
        return digitWidth
    }

    function measuredWidth(value) {
        let result = 0
        for (let index = 0; index < value.length; ++index)
            result += characterWidth(value.charAt(index))
        if (value.length > 1)
            result += letterSpacing * (value.length - 1)
        return Math.max(0, result)
    }

    implicitWidth: Math.max(measuredWidth(text), measuredWidth(widthReference))
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

    TextMetrics {
        id: percentMetrics
        text: "%"
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: root.fontWeight
    }

    Row {
        id: glyphRow

        x: root.horizontalAlignment === Text.AlignRight
           ? root.width - width
           : (root.horizontalAlignment === Text.AlignHCenter
              ? (root.width - width) / 2 : 0)
        anchors.verticalCenter: parent.verticalCenter
        width: implicitWidth
        height: parent.height
        spacing: root.letterSpacing

        Repeater {
            model: root.cellCount

            Item {
                id: digitCell
                required property int index
                property string observedCharacter: root.characterAtCell(index)
                property string settledCharacter: ""
                property string incomingCharacter: ""
                property bool ready: false
                readonly property int transitionDelay: root.staggerMs <= 0 ? 0
                    : (root.staggerFromRight
                       ? (root.cellCount - 1 - index) * root.staggerMs
                       : index * root.staggerMs)

                width: root.characterWidth(observedCharacter)
                height: root.height
                clip: true

                Component.onCompleted: {
                    settledCharacter = observedCharacter
                    ready = true
                }

                onObservedCharacterChanged: {
                    if (!ready || root.reducedMotion
                            || !root.canDissolve(settledCharacter)
                            || !root.canDissolve(observedCharacter)) {
                        dissolve.stop()
                        settledCharacter = observedCharacter
                        incomingCharacter = ""
                        outgoing.opacity = 1
                        outgoing.scale = 1
                        incoming.opacity = 0
                        incoming.scale = 1
                        return
                    }
                    if (settledCharacter === observedCharacter)
                        return

                    if (dissolve.running && incoming.opacity >= outgoing.opacity
                            && incomingCharacter.length > 0)
                        settledCharacter = incomingCharacter
                    dissolve.stop()
                    incomingCharacter = observedCharacter
                    incoming.opacity = 0
                    incoming.scale = 0.975
                    outgoing.opacity = 1
                    outgoing.scale = 1
                    dissolve.start()
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

                    Behavior on color {
                        ColorAnimation {
                            duration: root.reducedMotion ? 0 : MotionTokens.hover
                        }
                    }
                }

                Text {
                    id: incoming
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: digitCell.incomingCharacter
                    color: root.color
                    opacity: 0
                    font.family: root.fontFamily
                    font.pixelSize: root.fontPixelSize
                    font.weight: root.fontWeight
                    font.features: { "tnum": 1 }
                    renderType: Text.QtRendering

                    Behavior on color {
                        ColorAnimation {
                            duration: root.reducedMotion ? 0 : MotionTokens.hover
                        }
                    }
                }

                ParallelAnimation {
                    id: dissolve

                    SequentialAnimation {
                        PauseAnimation { duration: digitCell.transitionDelay }
                        NumberAnimation {
                            target: outgoing
                            property: "opacity"
                            to: 0
                            duration: MotionTokens.state
                            easing.type: Easing.InOutQuad
                        }
                    }
                    SequentialAnimation {
                        PauseAnimation { duration: digitCell.transitionDelay }
                        NumberAnimation {
                            target: incoming
                            property: "opacity"
                            to: 1
                            duration: MotionTokens.state
                            easing.type: MotionTokens.easeOut
                        }
                    }
                    SequentialAnimation {
                        PauseAnimation { duration: digitCell.transitionDelay }
                        NumberAnimation {
                            target: outgoing
                            property: "scale"
                            from: 1
                            to: 0.985
                            duration: MotionTokens.state
                            easing.type: Easing.InOutQuad
                        }
                    }
                    SequentialAnimation {
                        PauseAnimation { duration: digitCell.transitionDelay }
                        NumberAnimation {
                            target: incoming
                            property: "scale"
                            from: 0.975
                            to: 1
                            duration: MotionTokens.state
                            easing.type: MotionTokens.easeOut
                        }
                    }

                    onFinished: {
                        digitCell.settledCharacter = digitCell.incomingCharacter
                        digitCell.incomingCharacter = ""
                        outgoing.opacity = 1
                        outgoing.scale = 1
                        incoming.opacity = 0
                        incoming.scale = 1
                    }
                }
            }
        }
    }
}
