import QtQuick 6.5
import QtQuick.Controls 6.5

Button {
    id: control

    property string glyph: ""
    property url iconSource: ""
    property url invertedIconSource: ""
    property real iconSize: iconOnly ? 17 : 14
    property bool iconOnly: false
    property bool selected: false
    property bool accented: false
    property bool quiet: false
    property bool bare: false
    property int fixedWidth: 0
    property string accessibleName: ""
    property string uiFont: "Inter"
    property color accentColor: "#5ac8fa"
    property bool reducedMotion: controller.reducedMotion
    property bool motionReady: false
    readonly property string visualToken: glyph + "|" + iconSource + "|" + invertedIconSource

    implicitWidth: fixedWidth > 0 ? fixedWidth : (iconOnly ? 32 : Math.max(72, labelRow.implicitWidth + 22))
    implicitHeight: 32
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.32
    scale: down ? 0.90 : (hovered ? 1.022 : 1.0)

    Accessible.name: text.length > 0 ? text : accessibleName
    Accessible.role: Accessible.Button

    Behavior on scale {
        NumberAnimation {
            duration: control.reducedMotion ? 0
                                            : (control.down ? MotionTokens.press : MotionTokens.hover)
            easing.type: MotionTokens.easeOut
        }
    }
    Behavior on opacity {
        NumberAnimation { duration: control.reducedMotion ? 0 : MotionTokens.hover }
    }

    onVisualTokenChanged: {
        if (motionReady && !reducedMotion)
            iconSwap.restart()
    }
    Component.onCompleted: motionReady = true

    background: Rectangle {
        radius: control.iconOnly ? height / 2 : 9
        color: {
            if (control.bare && !control.accented && !control.selected)
                return control.down ? "#30ffffff" : (control.hovered ? "#18ffffff" : "transparent")
            if (control.down)
                return control.accented || control.selected ? "#d9f3ff" : "#303030"
            if (control.accented || control.selected)
                return control.hovered ? "#f5fbff" : "#f5f5f7"
            if (control.quiet)
                return control.hovered ? "#292929" : "#171717"
            return control.hovered ? "#292929" : "#1b1b1b"
        }
        border.width: control.bare && !control.visualFocus ? 0 : 1
        border.color: control.visualFocus
                      ? control.accentColor
                      : (control.accented || control.selected ? "#18ffffff" : "#22ffffff")

        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }

    contentItem: Item {
        implicitWidth: labelRow.implicitWidth
        implicitHeight: labelRow.implicitHeight

        Row {
            id: labelRow
            anchors.centerIn: parent
            spacing: control.glyph.length > 0 && control.text.length > 0 ? 7 : 0

            ParallelAnimation {
                id: iconSwap

                SequentialAnimation {
                    NumberAnimation {
                        target: labelRow
                        property: "scale"
                        from: 1
                        to: 0.76
                        duration: MotionTokens.press
                        easing.type: Easing.InCubic
                    }
                    NumberAnimation {
                        target: labelRow
                        property: "scale"
                        to: 1
                        duration: MotionTokens.hover
                        easing.type: MotionTokens.settle
                    }
                }
                SequentialAnimation {
                    NumberAnimation {
                        target: labelRow
                        property: "opacity"
                        from: 1
                        to: 0.42
                        duration: MotionTokens.press
                    }
                    NumberAnimation {
                        target: labelRow
                        property: "opacity"
                        to: 1
                        duration: MotionTokens.hover
                    }
                }
            }

            Text {
                visible: control.iconSource.toString().length === 0 && control.glyph.length > 0
                text: control.glyph
                color: control.accented || control.selected ? "#000000" : "#f5f5f7"
                font.family: "Segoe Fluent Icons"
                font.pixelSize: control.iconOnly ? 15 : 13
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Image {
                visible: control.iconSource.toString().length > 0
                width: control.iconSize
                height: control.iconSize
                sourceSize.width: Math.ceil(control.iconSize * 2)
                sourceSize.height: Math.ceil(control.iconSize * 2)
                source: (control.accented || control.selected)
                        && control.invertedIconSource.toString().length > 0
                        ? control.invertedIconSource : control.iconSource
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }

            Text {
                visible: control.text.length > 0
                text: control.text
                color: control.accented || control.selected ? "#000000" : "#f5f5f7"
                font.family: control.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
