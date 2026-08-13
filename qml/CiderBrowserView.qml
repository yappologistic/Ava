pragma ComponentBehavior: Bound

import QtQuick 6.5
import Ava 1.0

Item {
    id: root

    required property var provider
    required property var colors
    required property color accentColor
    required property string uiFont
    required property string monoFont
    required property bool reducedMotion
    property bool active: false
    property int mode: 2
    property string initialSearchQuery: ""
    readonly property bool providerReady: provider.active && provider.connected
    signal modeRequested(int mode)

    function refreshCurrentMode() {
        if (!active)
            return
        if (mode === 2)
            provider.refreshQueue()
        else if (mode === 4)
            provider.refreshPlaylists()
        else if (mode === 6)
            provider.refreshRecentlyPlayed()
    }

    onModeChanged: refreshCurrentMode()
    onActiveChanged: refreshCurrentMode()
    onProviderReadyChanged: {
        if (providerReady)
            refreshCurrentMode()
    }
    Component.onCompleted: refreshCurrentMode()

    Loader {
        id: contentLoader
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: modeRail.left
        anchors.rightMargin: 5
        active: root.active
        sourceComponent: root.mode === 2 ? queueComponent
                         : (root.mode === 4 ? playlistComponent
                            : (root.mode === 5 ? searchComponent
                               : recentComponent))

        Behavior on opacity {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.state
                easing.type: MotionTokens.easeOut
            }
        }
    }

    Component {
        id: queueComponent
        CiderQueueView {
            provider: root.provider
            colors: root.colors
            accentColor: root.accentColor
            uiFont: root.uiFont
            monoFont: root.monoFont
            reducedMotion: root.reducedMotion
            active: root.active && root.mode === 2
        }
    }

    Component {
        id: playlistComponent
        CiderPlaylistView {
            provider: root.provider
            colors: root.colors
            accentColor: root.accentColor
            uiFont: root.uiFont
            reducedMotion: root.reducedMotion
            active: root.active && root.mode === 4
        }
    }

    Component {
        id: searchComponent
        CiderSearchView {
            provider: root.provider
            colors: root.colors
            accentColor: root.accentColor
            uiFont: root.uiFont
            reducedMotion: root.reducedMotion
            active: root.active && root.mode === 5
            initialQuery: root.initialSearchQuery
        }
    }

    Component {
        id: recentComponent
        CiderRecentView {
            provider: root.provider
            colors: root.colors
            accentColor: root.accentColor
            uiFont: root.uiFont
            reducedMotion: root.reducedMotion
            active: root.active && root.mode === 6
        }
    }

    Column {
        id: modeRail
        anchors.right: parent.right
        anchors.rightMargin: -4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7

        Repeater {
            model: [
                { mode: 2, name: "Editable queue", icon: "queue" },
                { mode: 4, name: "Add to playlist", icon: "playlist" },
                { mode: 5, name: "Search and play", icon: "search" },
                { mode: 6, name: "Recently played", icon: "history" }
            ]

            delegate: Item {
                id: modeButton
                required property var modelData
                width: 15
                height: 15
                activeFocusOnTab: true
                readonly property bool selected: root.mode === modelData.mode
                opacity: selected ? 1 : (modeHover.hovered || activeFocus ? 0.84 : 0.52)
                scale: selected ? 1 : (modeHover.hovered || activeFocus ? 1 : 0.98)
                Accessible.name: modelData.name
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

                HoverHandler { id: modeHover }
                TapHandler { onTapped: root.modeRequested(modeButton.modelData.mode) }
                Keys.onReturnPressed: root.modeRequested(modelData.mode)
                Keys.onEnterPressed: root.modeRequested(modelData.mode)
                Keys.onSpacePressed: root.modeRequested(modelData.mode)

                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 2
                    height: 2
                    radius: 1
                    visible: modeButton.selected
                    color: root.accentColor
                }

                Image {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 13
                    height: width
                    source: modeButton.modelData.icon === "search"
                            ? Qt.resolvedUrl("../assets/icons/fluent-media/search-regular.svg")
                            : (modeButton.modelData.icon === "history"
                               ? Qt.resolvedUrl("../assets/icons/fluent-media/history-regular.svg")
                               : (modeButton.modelData.icon === "playlist"
                                  ? Qt.resolvedUrl("../assets/icons/fluent-media/playlist-regular.svg")
                                  : Qt.resolvedUrl("../assets/icons/fluent-media/queue-regular.svg")))
                    sourceSize: Qt.size(26, 26)
                }
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.right: modeRail.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        visible: root.provider.browserMessage.length > 0
                 && ((root.mode === 2 && root.provider.queue.length > 0)
                     || (root.mode === 4 && root.provider.playlists.length > 0)
                     || (root.mode === 5 && root.provider.searchResults.length > 0)
                     || (root.mode === 6 && root.provider.recentlyPlayed.length > 0))
        text: root.provider.browserMessage
        color: root.accentColor
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        font.family: root.uiFont
        font.pixelSize: 6
    }
}
