import QtQuick 6.5

Item {
    id: root

    property color color: "#a8c7fa"
    property string fontFamily: "Geist Mono"
    property bool running: true
    property bool reducedMotion: false
    property int fontPixelSize: 12
    property int frameIndex: 0
    readonly property var frames: ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]

    implicitWidth: 13
    implicitHeight: 16

    Text {
        anchors.centerIn: parent
        text: root.reducedMotion ? "·" : root.frames[root.frameIndex]
        color: root.color
        renderType: Text.NativeRendering
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: Font.Medium
    }

    Timer {
        interval: 105
        repeat: true
        running: root.running && !root.reducedMotion
        onTriggered: root.frameIndex = (root.frameIndex + 1) % root.frames.length
    }

    onRunningChanged: {
        if (!running)
            frameIndex = 0
    }
}
