pragma ComponentBehavior: Bound

import QtQuick 6.5
import Ava 1.0

Item {
    id: root

    property bool playing: false
    property string kind: "playback"
    property color color: "#f5f5f7"
    property color activeColor: "#5ac8fa"
    property bool active: false
    property bool reducedMotion: false
    property real morphProgress: playing ? 1 : 0

    implicitWidth: 16
    implicitHeight: 16

    Behavior on morphProgress {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : MotionTokens.directSettle
            easing.type: MotionTokens.easeOut
        }
    }

    function iconSource(iconKind) {
        if (iconKind === "previous")
            return Qt.resolvedUrl("../assets/icons/fluent-media/previous-regular.svg")
        if (iconKind === "next")
            return Qt.resolvedUrl("../assets/icons/fluent-media/next-regular.svg")
        if (iconKind === "favorite")
            return Qt.resolvedUrl(root.active
                                  ? "../assets/icons/fluent-media/star-filled.svg"
                                  : "../assets/icons/fluent-media/star-regular.svg")
        if (iconKind === "queue")
            return Qt.resolvedUrl("../assets/icons/fluent-media/queue-regular.svg")
        if (iconKind === "lyrics")
            return Qt.resolvedUrl("../assets/icons/fluent-media/lyrics-regular.svg")
        if (iconKind === "connect")
            return Qt.resolvedUrl("../assets/icons/fluent-media/connect-regular.svg")
        return ""
    }

    Image {
        anchors.fill: parent
        visible: root.kind !== "playback"
        source: root.iconSource(root.kind)
        sourceSize: Qt.size(Math.ceil(width * 2), Math.ceil(height * 2))
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        opacity: root.active ? 1 : 0.92
        scale: root.active ? 1 : 0.96

        Behavior on opacity {
            NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
        }
        Behavior on scale {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.directSettle
                easing.type: MotionTokens.easeOut
            }
        }
    }

    Image {
        anchors.fill: parent
        visible: root.kind === "playback"
        source: Qt.resolvedUrl("../assets/icons/fluent-media/play-regular.svg")
        sourceSize: Qt.size(Math.ceil(width * 2), Math.ceil(height * 2))
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        opacity: 1 - root.morphProgress
        scale: 1 - root.morphProgress * 0.12
    }

    Image {
        anchors.fill: parent
        visible: root.kind === "playback"
        source: Qt.resolvedUrl("../assets/icons/fluent-media/pause-regular.svg")
        sourceSize: Qt.size(Math.ceil(width * 2), Math.ceil(height * 2))
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        opacity: root.morphProgress
        scale: 0.88 + root.morphProgress * 0.12
    }
}
