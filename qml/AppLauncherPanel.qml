pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Shapes 6.5

Item {
    id: root

    property var colors
    property string uiFont: "Inter"
    property string iconFont: "Segoe Fluent Icons"
    property color focusAccent: "#5ac8fa"
    property bool reducedMotion: false
    property bool open: false
    property real revealProgress: 0

    visible: open || opacity > 0.001
    enabled: open
    opacity: open ? 1 : 0
    scale: open ? 1 : 0.985
    transformOrigin: Item.Top
    transform: Translate {
        y: root.open ? 0 : -6
        Behavior on y {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : (root.open ? MotionTokens.content
                                                              : MotionTokens.state)
                easing.type: Easing.OutCubic
            }
        }
    }

    Behavior on focusAccent {
        ColorAnimation {
            duration: root.reducedMotion ? 0 : MotionTokens.content
            easing.type: Easing.OutCubic
        }
    }

    Accessible.name: "Application launcher"
    Accessible.role: Accessible.Pane

    Behavior on opacity {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : (root.open ? 170 : 120)
            easing.type: root.open ? Easing.OutCubic : Easing.InCubic
        }
    }
    Behavior on scale {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : (root.open ? 230 : 120)
            easing.type: Easing.OutCubic
        }
    }

    function resetSelection() {
        results.currentIndex = appLauncher.resultCount > 0 ? 0 : -1
        if (results.currentIndex >= 0)
            results.positionViewAtIndex(results.currentIndex, ListView.Beginning)
    }

    function stepSelection(amount) {
        if (appLauncher.resultCount <= 0)
            return
        const next = Math.max(0, Math.min(appLauncher.resultCount - 1,
                                          results.currentIndex + amount))
        results.currentIndex = next
        results.positionViewAtIndex(next, ListView.Contain)
    }

    function activateSelection() {
        if (results.currentIndex >= 0)
            appLauncher.launch(results.currentIndex)
    }

    onOpenChanged: {
        if (open) {
            revealProgress = 0
            revealAnimation.restart()
            resetSelection()
            focusSearch.restart()
        } else {
            focusSearch.stop()
            revealAnimation.stop()
            revealProgress = 0
        }
    }

    Connections {
        target: appLauncher
        function onResultsChanged() { root.resetSelection() }
        function onQueryChanged() {
            if (searchField.text !== appLauncher.query)
                searchField.text = appLauncher.query
        }
    }

    NumberAnimation {
        id: revealAnimation
        target: root
        property: "revealProgress"
        from: 0
        to: 1
        duration: root.reducedMotion ? 0 : MotionTokens.reveal
        easing.type: Easing.OutCubic
    }

    Timer {
        id: focusSearch
        interval: root.reducedMotion ? 0 : 75
        repeat: false
        onTriggered: {
            searchField.forceActiveFocus(Qt.ShortcutFocusReason)
            searchField.selectAll()
        }
    }

    Rectangle {
        id: searchGlow
        x: 19
        y: 17
        width: parent.width - 38
        height: 56
        radius: 17
        color: "transparent"
        border.width: 5
        border.color: root.focusAccent
        opacity: searchField.activeFocus ? 0.075 : 0
        scale: searchField.activeFocus ? 1.008 : 0.99

        Behavior on opacity {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.hover
                easing.type: Easing.OutCubic
            }
        }
        Behavior on scale {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.directSettle
                easing.type: Easing.OutCubic
            }
        }
    }

    TextField {
        id: searchField
        x: 22
        y: 20
        width: parent.width - 44
        height: 50
        leftPadding: 45
        rightPadding: 18
        topPadding: 0
        bottomPadding: 0
        text: appLauncher.query
        placeholderText: "Search applications"
        placeholderTextColor: colors.tertiary
        color: colors.text
        selectionColor: colors.accent
        selectedTextColor: colors.black
        font.family: root.uiFont
        font.pixelSize: 15
        font.weight: Font.Medium
        focus: root.open
        scale: root.open ? 1 : 0.987
        transformOrigin: Item.Center
        selectByMouse: true
        persistentSelection: true
        Accessible.name: "Search applications"
        Accessible.description: "Type an application name, then press Enter to open it"

        onTextEdited: appLauncher.setQuery(text)

        Behavior on scale {
            NumberAnimation {
                duration: root.reducedMotion ? 0 : MotionTokens.content
                easing.type: Easing.OutCubic
            }
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                appLauncher.closeLauncher()
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                root.stepSelection(1)
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                root.stepSelection(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_PageDown) {
                root.stepSelection(5)
                event.accepted = true
            } else if (event.key === Qt.Key_PageUp) {
                root.stepSelection(-5)
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.activateSelection()
                event.accepted = true
            }
        }

        background: Rectangle {
            radius: 14
            color: searchField.activeFocus ? "#161616" : "#121212"
            border.width: 1
            border.color: searchField.activeFocus
                          ? Qt.rgba(root.focusAccent.r, root.focusAccent.g,
                                    root.focusAccent.b, 0.48)
                          : "#2b2b2b"

            Behavior on color {
                ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
            }
            Behavior on border.color {
                ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
            }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: "\uE721"
            color: searchField.activeFocus ? colors.text : colors.secondary
            font.family: root.iconFont
            font.pixelSize: 16

            Behavior on color {
                ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
            }
        }

    }

    Item {
        id: resultRegion
        x: 14
        y: 82
        width: parent.width - 28
        height: Math.max(290, Math.floor((parent.height - y - 18) / 58) * 58)
        clip: true
        transform: Translate {
            y: (1 - root.revealProgress) * 7
        }

        Rectangle {
            id: slidingSelection
            x: 2
            y: results.y + (results.currentItem
                            ? results.currentItem.y - results.contentY : 0)
            width: parent.width - 4
            height: 58
            radius: 13
            color: "#202020"
            border.width: 1
            border.color: "#2c2c2c"
            visible: !appLauncher.loading && appLauncher.resultCount > 0
                     && results.currentIndex >= 0
            opacity: visible ? Math.min(1, root.revealProgress * 1.25) : 0

            Behavior on y {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.directSettle
                    easing.type: Easing.OutCubic
                }
            }
            Behavior on opacity {
                NumberAnimation {
                    duration: root.reducedMotion ? 0 : MotionTokens.hover
                }
            }

            Rectangle {
                x: 3
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 23
                radius: 2
                color: root.focusAccent
            }
        }

        ListView {
            id: results
            x: 2
            y: 0
            width: parent.width - 4
            height: parent.height
            z: 2
            clip: true
            model: appLauncher
            currentIndex: appLauncher.resultCount > 0 ? 0 : -1
            boundsBehavior: Flickable.StopAtBounds
            flickDeceleration: 4500
            maximumFlickVelocity: 2200
            keyNavigationWraps: false
            highlightFollowsCurrentItem: false
            reuseItems: true
            Accessible.name: "Application results"
            Accessible.role: Accessible.List

            delegate: Item {
                id: appRow
                required property int index
                required property string appId
                required property string appName
                required property string subtitle
                required property string iconSource

                width: results.width
                height: 58
                scale: rowTap.pressed ? 0.988 : 1
                transformOrigin: Item.Center
                Accessible.name: appName + ", " + subtitle
                Accessible.role: Accessible.ListItem
                Accessible.focused: results.currentIndex === index

                Component.onCompleted: appLauncher.requestIcon(appId)
                onAppIdChanged: appLauncher.requestIcon(appId)

                Behavior on scale {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.press
                        easing.type: Easing.OutCubic
                    }
                }

                Item {
                    id: rowContent
                    anchors.fill: parent
                    opacity: root.open ? 1 : 0
                    transform: Translate {
                        y: root.open ? 0 : -5
                        Behavior on y {
                            NumberAnimation {
                                duration: root.reducedMotion ? 0 : MotionTokens.content
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    Behavior on opacity {
                        SequentialAnimation {
                            PauseAnimation {
                                duration: root.reducedMotion || !root.open
                                          ? 0 : Math.min(appRow.index, 6) * 17
                            }
                            NumberAnimation {
                                duration: root.reducedMotion ? 0 : MotionTokens.state
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    Rectangle {
                        id: iconPlate
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 38
                        height: 38
                        radius: 10
                        color: results.currentIndex === appRow.index
                               ? "#2a2a2a" : "#191919"
                        border.width: 1
                        border.color: results.currentIndex === appRow.index
                                      ? "#343434" : "#242424"
                        scale: rowHover.hovered ? 1.045 : 1

                        Behavior on color {
                            ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
                        }
                        Behavior on border.color {
                            ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
                        }
                        Behavior on scale {
                            NumberAnimation {
                                duration: root.reducedMotion ? 0 : MotionTokens.hover
                                easing.type: Easing.OutCubic
                            }
                        }

                        Image {
                            id: appIcon
                            anchors.centerIn: parent
                            width: 28
                            height: 28
                            source: appRow.iconSource
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            smooth: true
                            mipmap: true
                            sourceSize.width: 56
                            sourceSize.height: 56
                            opacity: status === Image.Ready ? 1 : 0
                            scale: status === Image.Ready ? 1 : 0.9

                            Behavior on opacity {
                                NumberAnimation {
                                    duration: root.reducedMotion ? 0 : MotionTokens.state
                                    easing.type: Easing.OutCubic
                                }
                            }
                            Behavior on scale {
                                NumberAnimation {
                                    duration: root.reducedMotion ? 0 : MotionTokens.content
                                    easing.type: Easing.OutBack
                                    easing.overshoot: 0.35
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: opacity > 0.001
                            opacity: appIcon.status === Image.Ready ? 0 : 1
                            text: appRow.appName.length > 0
                                  ? appRow.appName.charAt(0).toUpperCase() : "?"
                            color: colors.secondary
                            font.family: root.uiFont
                            font.pixelSize: 14
                            font.weight: Font.DemiBold

                            Behavior on opacity {
                                NumberAnimation {
                                    duration: root.reducedMotion ? 0 : MotionTokens.hover
                                    easing.type: Easing.OutCubic
                                }
                            }
                        }
                    }

                    Text {
                        x: 63
                        y: 10
                        width: parent.width - 104
                        text: appRow.appName
                        color: colors.text
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        font.family: root.uiFont
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    Text {
                        x: 63
                        y: 31
                        width: parent.width - 104
                        text: appRow.subtitle
                        color: colors.tertiary
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        font.family: root.uiFont
                        font.pixelSize: 9
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: "\uE72A"
                        color: colors.secondary
                        font.family: root.iconFont
                        font.pixelSize: 13
                        opacity: results.currentIndex === appRow.index ? 0.8 : 0
                        transform: Translate {
                            x: results.currentIndex === appRow.index ? 0 : -5
                            Behavior on x {
                                NumberAnimation {
                                    duration: root.reducedMotion ? 0 : MotionTokens.hover
                                    easing.type: Easing.OutCubic
                                }
                            }
                        }

                        Behavior on opacity {
                            NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
                        }
                    }
                }

                HoverHandler {
                    id: rowHover
                    onHoveredChanged: {
                        if (hovered)
                            results.currentIndex = appRow.index
                    }
                }

                TapHandler {
                    id: rowTap
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        results.currentIndex = appRow.index
                        appLauncher.launch(appRow.index)
                    }
                }
            }
        }

        Item {
            anchors.fill: parent
            visible: appLauncher.loading
            opacity: visible ? 1 : 0

            Shape {
                id: loadingSpinner
                anchors.horizontalCenter: parent.horizontalCenter
                y: 76
                width: 26
                height: 26

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: colors.accent
                    strokeWidth: 2
                    capStyle: ShapePath.RoundCap
                    startX: 13
                    startY: 2
                    PathAngleArc {
                        centerX: 13
                        centerY: 13
                        radiusX: 11
                        radiusY: 11
                        startAngle: -90
                        sweepAngle: 248
                    }
                }
                RotationAnimator on rotation {
                    running: appLauncher.loading && !root.reducedMotion
                    from: 0
                    to: 360
                    duration: 780
                    loops: Animation.Infinite
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 113
                text: "Finding installed applications"
                color: colors.secondary
                font.family: root.uiFont
                font.pixelSize: 11
            }
        }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 65
            width: parent.width - 64
            spacing: 8
            visible: !appLauncher.loading && appLauncher.resultCount === 0

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: appLauncher.errorMessage.length > 0 ? "\uEA39" : "\uE721"
                color: appLauncher.errorMessage.length > 0 ? "#ffb15c" : colors.tertiary
                font.family: root.iconFont
                font.pixelSize: 25
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: appLauncher.errorMessage.length > 0
                      ? "Applications unavailable" : "No applications found"
                color: colors.text
                font.family: root.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: appLauncher.errorMessage.length > 0
                      ? appLauncher.errorMessage
                      : "Try another name or a shorter search"
                color: colors.tertiary
                wrapMode: Text.WordWrap
                font.family: root.uiFont
                font.pixelSize: 10
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: appLauncher.errorMessage.length > 0
                text: "Try again"
                Accessible.name: "Reload installed applications"
                onClicked: appLauncher.refresh()
                contentItem: Text {
                    text: parent.text
                    color: colors.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: root.uiFont
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
                background: Rectangle {
                    radius: 9
                    color: parent.hovered ? "#2b2b2b" : "#202020"
                    border.width: 1
                    border.color: "#353535"
                }
            }
        }
    }

}
