import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property var promptModel
    property int activePromptIndex: -1
    property int hoveredPromptIndex: -1
    property bool enabledByLayout: true

    signal jumpRequested(int sourceRow, string itemId)

    readonly property int itemCount: markerList.count
    readonly property var hoveredItem: {
        // Keep this binding responsive while delegates are recycled during a
        // rail scroll. itemAtIndex() alone has no notify signal.
        const scrollPosition = markerList.contentY
        return !markerList.moving && hoveredPromptIndex >= 0
                && hoveredPromptIndex < itemCount
            ? markerList.itemAtIndex(hoveredPromptIndex) : null
    }

    width: hoveredItem ? 354 : 44
    visible: enabledByLayout && itemCount >= 2
    z: 30

    function closePreviewSoon() {
        previewCloseTimer.restart()
    }

    function focusRelative(index, delta) {
        const next = Math.max(0, Math.min(itemCount - 1, index + delta))
        markerList.positionViewAtIndex(next, ListView.Contain)
        Qt.callLater(function() {
            const item = markerList.itemAtIndex(next)
            if (item)
                item.forceActiveFocus()
        })
    }

    function revealActivePrompt() {
        if (activePromptIndex >= 0 && activePromptIndex < itemCount)
            markerList.positionViewAtIndex(activePromptIndex, ListView.Contain)
    }

    onActivePromptIndexChanged: Qt.callLater(revealActivePrompt)

    Behavior on width {
        NumberAnimation {
            duration: reducedMotion ? 0 : 120
            easing.type: Easing.OutCubic
        }
    }

    HoverHandler {
        id: navigatorHover
        onHoveredChanged: {
            if (hovered)
                previewCloseTimer.stop()
            else
                root.closePreviewSoon()
        }
    }

    Timer {
        id: previewCloseTimer
        interval: 90
        onTriggered: {
            if (!navigatorHover.hovered)
                root.hoveredPromptIndex = -1
        }
    }

    ListView {
        id: markerList
        x: 4
        y: 22
        width: 40
        height: Math.max(1, root.height - 44)
        model: root.promptModel
        spacing: 4
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 3800
        maximumFlickVelocity: 1800
        reuseItems: true
        cacheBuffer: 72
        currentIndex: root.activePromptIndex
        highlightFollowsCurrentItem: false
        Accessible.name: "Prompt navigation"

        onCountChanged: Qt.callLater(root.revealActivePrompt)
        onMovementStarted: root.hoveredPromptIndex = -1

        delegate: Item {
            id: marker

            required property int index
            required property string itemId
            required property int sourceRow
            required property string promptText
            required property string responseText

            readonly property int activeDistance: root.hoveredPromptIndex < 0
                                                  ? 99
                                                  : Math.abs(index - root.hoveredPromptIndex)
            readonly property bool current: index === root.activePromptIndex
            readonly property bool highlighted: index === root.hoveredPromptIndex

            width: markerList.width
            height: 8
            activeFocusOnTab: true

            Accessible.role: Accessible.Button
            Accessible.name: "Jump to prompt " + (index + 1) + ": "
                             + (promptText.length > 0 ? promptText : "User message")

            Rectangle {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                width: marker.highlighted ? 24
                       : (marker.activeDistance === 1 ? 16
                          : (marker.activeDistance === 2 ? 11 : 8))
                height: marker.current || marker.highlighted ? 2 : 1.5
                radius: height / 2
                color: marker.highlighted ? "#d0d0d5"
                       : (marker.current ? "#a7a7af" : "#55555d")

                Behavior on width {
                    NumberAnimation {
                        duration: reducedMotion ? 0 : 115
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on color {
                    ColorAnimation { duration: reducedMotion ? 0 : 100 }
                }
            }

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
                onHoveredChanged: {
                    if (hovered) {
                        previewCloseTimer.stop()
                        root.hoveredPromptIndex = marker.index
                    } else {
                        root.closePreviewSoon()
                    }
                }
            }

            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: {
                    root.jumpRequested(marker.sourceRow, marker.itemId)
                    root.hoveredPromptIndex = marker.index
                }
            }

            onActiveFocusChanged: {
                if (activeFocus)
                    root.hoveredPromptIndex = index
            }

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Down) {
                    root.focusRelative(marker.index, 1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Up) {
                    root.focusRelative(marker.index, -1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Home) {
                    root.focusRelative(0, 0)
                    event.accepted = true
                } else if (event.key === Qt.Key_End) {
                    root.focusRelative(root.itemCount - 1, 0)
                    event.accepted = true
                } else if (event.key === Qt.Key_Return
                           || event.key === Qt.Key_Enter
                           || event.key === Qt.Key_Space) {
                    root.jumpRequested(marker.sourceRow, marker.itemId)
                    event.accepted = true
                }
            }
        }
    }

    Rectangle {
        id: preview
        readonly property var item: root.hoveredItem

        x: 42
        y: item ? Math.max(0, Math.min(root.height - height,
                                      item.mapToItem(root, 0, item.height / 2).y
                                      + markerList.contentY * 0 - height / 2)) : 0
        width: 304
        height: previewColumn.implicitHeight + 22
        radius: 12
        color: "#18181b"
        opacity: item ? 1 : 0
        scale: item ? 1 : 0.98
        transformOrigin: Item.Left
        enabled: item !== null

        Behavior on y {
            NumberAnimation {
                duration: reducedMotion ? 0 : 115
                easing.type: Easing.OutCubic
            }
        }
        Behavior on opacity {
            NumberAnimation { duration: reducedMotion ? 0 : 105 }
        }
        Behavior on scale {
            NumberAnimation {
                duration: reducedMotion ? 0 : 125
                easing.type: Easing.OutCubic
            }
        }

        Column {
            id: previewColumn
            x: 12
            y: 10
            width: parent.width - 24
            spacing: responsePreview.visible ? 6 : 0

            Text {
                width: parent.width
                text: preview.item && preview.item.promptText.length > 0
                      ? preview.item.promptText : "User message"
                color: "#ddddE2"
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 1.25
                lineHeightMode: Text.ProportionalHeight
                font.family: uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            Text {
                id: responsePreview
                width: parent.width
                visible: text.length > 0
                text: preview.item ? preview.item.responseText : ""
                color: "#777780"
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                font.family: uiFont
                font.pixelSize: 10
            }
        }
    }
}
