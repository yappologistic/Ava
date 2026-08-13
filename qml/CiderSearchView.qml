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
    property string initialQuery: ""

    clip: true

    Image {
        x: 2
        y: 3
        width: 13
        height: 13
        source: Qt.resolvedUrl("../assets/icons/fluent-media/search-regular.svg")
        sourceSize: Qt.size(26, 26)
        opacity: searchInput.activeFocus ? 0.96 : 0.48

        Behavior on opacity {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.hover
            }
        }
    }

    TextInput {
        id: searchInput
        x: 19
        y: 0
        width: parent.width - 22
        height: 20
        enabled: root.active
        color: root.colors.text
        selectionColor: Qt.rgba(root.accentColor.r, root.accentColor.g,
                                root.accentColor.b, 0.34)
        selectedTextColor: root.colors.text
        font.family: root.uiFont
        font.pixelSize: 9
        verticalAlignment: TextInput.AlignVCenter
        clip: true
        maximumLength: 100
        selectByMouse: true
        Accessible.name: "Search music"

        Component.onCompleted: {
            if (root.initialQuery.length > 0)
                text = root.initialQuery
        }

        onTextChanged: {
            if (!root.active)
                return
            searchDelay.restart()
        }
        Keys.onDownPressed: {
            if (resultsReel.count > 0) {
                resultsReel.currentIndex = 0
                resultsReel.currentItem.forceActiveFocus()
            }
        }
        Keys.onEscapePressed: {
            text = ""
            root.provider.search("")
        }
    }

    Text {
        x: searchInput.x
        y: 4
        visible: searchInput.text.length === 0 && !searchInput.activeFocus
        text: "Search music"
        color: root.colors.tertiary
        font.family: root.uiFont
        font.pixelSize: 8
    }

    Rectangle {
        x: searchInput.x
        y: 18
        width: searchInput.width
        height: 1
        color: searchInput.activeFocus ? root.accentColor : root.colors.raised
        opacity: searchInput.activeFocus ? 0.48 : 0.22

        Behavior on color {
            ColorAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.hover
            }
        }
        Behavior on opacity {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.hover
            }
        }
    }

    Timer {
        id: searchDelay
        interval: 280
        repeat: false
        onTriggered: root.provider.search(searchInput.text)
    }

    Text {
        anchors.centerIn: resultsReel
        visible: root.active && root.provider.searchResults.length === 0
                 && !root.provider.browserBusy
                 && (searchInput.text.length > 0
                     || root.provider.browserMessage.length > 0)
        text: root.provider.browserMessage.length > 0
              ? root.provider.browserMessage : "No matches"
        color: root.colors.tertiary
        font.family: root.uiFont
        font.pixelSize: 8
    }

    Rectangle {
        id: searchLoadingDot
        anchors.centerIn: resultsReel
        visible: root.active && root.provider.browserBusy
                 && root.provider.searchResults.length === 0
        width: 3
        height: 3
        radius: 1.5
        color: root.accentColor

        SequentialAnimation on opacity {
            running: searchLoadingDot.visible && !root.reducedMotion
            loops: Animation.Infinite
            NumberAnimation { to: 0.28; duration: 360 }
            NumberAnimation { to: 0.8; duration: 360 }
        }
    }

    ListView {
        id: resultsReel
        x: 0
        y: 22
        width: parent.width
        height: parent.height - y
        model: root.active ? root.provider.searchResults : []
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 5200
        maximumFlickVelocity: 1100
        snapMode: ListView.SnapToItem
        reuseItems: true
        cacheBuffer: 40
        currentIndex: -1

        delegate: Item {
            id: resultRow
            required property int index
            required property var modelData
            x: 2
            width: resultsReel.width - 4
            height: 20
            activeFocusOnTab: true
            readonly property real viewportCenter: y + height / 2
                                                   - resultsReel.contentY
            readonly property real topReveal: Math.max(0.26, Math.min(1,
                (viewportCenter - 2) / 16))
            readonly property real bottomReveal: Math.max(0.18, Math.min(1,
                (resultsReel.height - viewportCenter + 9) / 39))
            readonly property real reelReveal: Math.min(topReveal, bottomReveal)
            opacity: (resultHover.hovered || activeFocus ? 1 : 0.92)
                     * reelReveal
            scale: resultHover.hovered || activeFocus ? 1 : 0.995
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

            HoverHandler { id: resultHover }
            TapHandler {
                onTapped: root.provider.playMediaItem(resultRow.modelData.type,
                                                       resultRow.modelData.id,
                                                       resultRow.modelData.href,
                                                       resultRow.modelData.resourceType)
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
            Keys.onUpPressed: {
                if (index > 0) {
                    resultsReel.currentIndex = index - 1
                    resultsReel.currentItem.forceActiveFocus()
                } else {
                    searchInput.forceActiveFocus()
                }
            }
            Keys.onDownPressed: {
                if (index + 1 < resultsReel.count) {
                    resultsReel.currentIndex = index + 1
                    resultsReel.currentItem.forceActiveFocus()
                }
            }

            Rectangle {
                x: 2
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 3
                color: Qt.rgba(root.accentColor.r, root.accentColor.g,
                               root.accentColor.b, 0.22)
                scale: resultHover.hovered || resultRow.activeFocus ? 1 : 0.96

                Behavior on scale {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.hover
                        easing.type: MotionTokens.easeOut
                    }
                }

                Image {
                    anchors.fill: parent
                    visible: source.toString().length > 0
                    source: resultRow.modelData.artworkUrl ?? ""
                    sourceSize: Qt.size(28, 28)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    smooth: true
                }

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    visible: resultHover.hovered || resultRow.activeFocus
                    color: Qt.rgba(0, 0, 0, 0.32)

                    Image {
                        anchors.centerIn: parent
                        width: 8
                        height: 8
                        source: Qt.resolvedUrl(
                                    "../assets/icons/fluent-media/play-regular.svg")
                        sourceSize: Qt.size(16, 16)
                    }
                }
            }
            Text {
                x: 21
                y: 1
                width: parent.width - 23
                text: resultRow.modelData.title
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
                text: resultRow.modelData.subtitle
                color: root.colors.tertiary
                elide: Text.ElideRight
                maximumLineCount: 1
                font.family: root.uiFont
                font.pixelSize: 6
            }
        }
    }
}
