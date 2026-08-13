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
        visible: root.active && root.provider.playlists.length === 0
                 && !root.provider.browserBusy
        text: root.provider.browserMessage.length > 0
              ? root.provider.browserMessage : "No playlists"
        color: root.colors.tertiary
        font.family: root.uiFont
        font.pixelSize: 8
    }

    Rectangle {
        id: playlistLoadingDot
        anchors.centerIn: parent
        visible: root.active && root.provider.browserBusy
                 && root.provider.playlists.length === 0
        width: 3
        height: 3
        radius: 1.5
        color: root.accentColor

        SequentialAnimation on opacity {
            running: playlistLoadingDot.visible && !root.reducedMotion
            loops: Animation.Infinite
            NumberAnimation { to: 0.28; duration: 360 }
            NumberAnimation { to: 0.8; duration: 360 }
        }
    }

    ListView {
        id: playlistReel
        anchors.fill: parent
        model: root.active ? root.provider.playlists : []
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 5200
        maximumFlickVelocity: 1100
        snapMode: ListView.SnapToItem
        reuseItems: true
        cacheBuffer: 40
        currentIndex: -1

        delegate: Item {
            id: playlistRow
            required property int index
            required property var modelData
            x: 2
            width: playlistReel.width - 4
            height: 20
            activeFocusOnTab: true
            readonly property real viewportCenter: y + height / 2
                                                   - playlistReel.contentY
            readonly property real topReveal: Math.max(0.25, Math.min(1,
                (viewportCenter - 2) / 17))
            readonly property real bottomReveal: Math.max(0.18, Math.min(1,
                (playlistReel.height - viewportCenter + 9) / 43))
            readonly property real reelReveal: Math.min(topReveal, bottomReveal)
            readonly property bool added: root.provider.lastAddedPlaylistId
                                          === modelData.id
            opacity: (playlistHover.hovered || activeFocus ? 1 : 0.92)
                     * reelReveal
            scale: playlistHover.hovered || activeFocus ? 1 : 0.995
            Accessible.name: added
                             ? modelData.title + ", track added"
                             : "Add current track to " + modelData.title
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

            HoverHandler { id: playlistHover }
            TapHandler {
                onTapped: root.provider.addCurrentTrackToPlaylist(
                              playlistRow.modelData.id)
            }
            Keys.onReturnPressed: root.provider.addCurrentTrackToPlaylist(modelData.id)
            Keys.onEnterPressed: root.provider.addCurrentTrackToPlaylist(modelData.id)
            Keys.onSpacePressed: root.provider.addCurrentTrackToPlaylist(modelData.id)

            Rectangle {
                x: 1
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 3
                color: Qt.rgba(root.accentColor.r, root.accentColor.g,
                               root.accentColor.b, 0.26)

                Image {
                    anchors.fill: parent
                    visible: source.toString().length > 0
                    source: playlistRow.modelData.artworkUrl ?? ""
                    sourceSize: Qt.size(28, 28)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    smooth: true
                }
            }

            Text {
                x: 21
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 41
                text: playlistRow.modelData.title
                color: root.colors.text
                elide: Text.ElideRight
                maximumLineCount: 1
                font.family: root.uiFont
                font.pixelSize: 8
                font.weight: Font.Medium
            }

            Image {
                anchors.right: parent.right
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                width: 12
                height: 12
                source: playlistRow.added
                        ? Qt.resolvedUrl("../assets/icons/fluent-media/check-regular.svg")
                        : Qt.resolvedUrl("../assets/icons/fluent-media/add-regular.svg")
                sourceSize: Qt.size(24, 24)
                opacity: playlistRow.added || playlistHover.hovered
                         || playlistRow.activeFocus ? 0.95 : 0.52
                scale: playlistRow.added ? 1.05 : 0.94

                Behavior on opacity {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.hover
                    }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0
                                                    : MotionTokens.directSettle
                        easing.type: MotionTokens.easeOut
                    }
                }
            }
        }
    }
}
