pragma ComponentBehavior: Bound

import QtQuick 6.5
import Ava 1.0

Item {
    id: root

    required property var provider
    required property var colors
    required property color accentColor
    required property string uiFont
    required property bool reducedMotion
    property bool active: false

    clip: true

    Text {
        anchors.centerIn: parent
        visible: root.active && root.provider.recentlyPlayed.length === 0
                 && !root.provider.browserBusy
        text: root.provider.browserMessage.length > 0
              ? root.provider.browserMessage : "No listening history"
        color: root.colors.tertiary
        font.family: root.uiFont
        font.pixelSize: 8
    }

    Rectangle {
        id: recentLoadingDot
        anchors.centerIn: parent
        visible: root.active && root.provider.browserBusy
                 && root.provider.recentlyPlayed.length === 0
        width: 3
        height: 3
        radius: 1.5
        color: root.accentColor

        SequentialAnimation on opacity {
            running: recentLoadingDot.visible && !root.reducedMotion
            loops: Animation.Infinite
            NumberAnimation { to: 0.28; duration: 360 }
            NumberAnimation { to: 0.8; duration: 360 }
        }
    }

    ListView {
        id: recentReel
        anchors.fill: parent
        model: root.active ? root.provider.recentlyPlayed : []
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 5200
        maximumFlickVelocity: 1100
        snapMode: ListView.SnapToItem
        reuseItems: true
        cacheBuffer: 40
        currentIndex: -1

        delegate: Item {
            id: recentRow
            required property int index
            required property var modelData
            x: 2
            width: recentReel.width - 4
            height: 20
            activeFocusOnTab: true
            readonly property real viewportCenter: y + height / 2
                                                   - recentReel.contentY
            readonly property real topReveal: Math.max(0.25, Math.min(1,
                (viewportCenter - 2) / 17))
            readonly property real bottomReveal: Math.max(0.18, Math.min(1,
                (recentReel.height - viewportCenter + 9) / 43))
            readonly property real reelReveal: Math.min(topReveal, bottomReveal)
            opacity: (recentHover.hovered || activeFocus ? 1 : 0.92)
                     * reelReveal
            scale: recentHover.hovered || activeFocus ? 1 : 0.995
            Accessible.name: "Play " + modelData.title
                             + (modelData.subtitle.length > 0
                                ? " by " + modelData.subtitle : "")
            Accessible.role: Accessible.Button

            Behavior on opacity {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.hover
                }
            }
            Behavior on scale {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.hover
                    easing.type: MotionTokens.easeOut
                }
            }

            HoverHandler { id: recentHover }
            TapHandler {
                onTapped: root.provider.playMediaItem(recentRow.modelData.type,
                                                       recentRow.modelData.id,
                                                       recentRow.modelData.href,
                                                       recentRow.modelData.resourceType)
            }
            Keys.onReturnPressed: root.provider.playMediaItem(
                                      modelData.type, modelData.id,
                                      modelData.href, modelData.resourceType)
            Keys.onEnterPressed: root.provider.playMediaItem(
                                     modelData.type, modelData.id,
                                     modelData.href, modelData.resourceType)
            Keys.onSpacePressed: root.provider.playMediaItem(
                                     modelData.type, modelData.id,
                                     modelData.href, modelData.resourceType)

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
                    source: recentRow.modelData.artworkUrl ?? ""
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
                width: parent.width - 23
                text: recentRow.modelData.title
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
                width: parent.width - 23
                text: recentRow.modelData.subtitle
                color: root.colors.tertiary
                elide: Text.ElideRight
                maximumLineCount: 1
                font.family: root.uiFont
                font.pixelSize: 6
            }
        }
    }
}
