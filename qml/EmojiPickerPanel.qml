pragma ComponentBehavior: Bound

import QtQuick 6.5
import QtQuick.Controls 6.5
import Ava 1.0

Item {
    id: root

    property var colors
    property string uiFont: "Inter"
    property string iconFont: "Segoe Fluent Icons"
    property color focusAccent: "#5ac8fa"
    property bool reducedMotion: false
    property bool open: false
    readonly property var selectedItem: {
        const revision = emojiPicker.resultCount
        return grid.currentIndex >= 0 ? emojiPicker.itemAt(grid.currentIndex) : ({})
    }

    visible: open || opacity > 0.001
    enabled: open && !appLauncher.pasteDismissPending
    opacity: open && !appLauncher.pasteDismissPending ? 1 : 0
    scale: open && !appLauncher.pasteDismissPending ? 1 : 0.992
    transformOrigin: Item.Bottom
    Accessible.name: "Emoji and symbols"
    Accessible.role: Accessible.Pane

    Behavior on opacity {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : (appLauncher.pasteDismissPending
                                                ? MotionTokens.press
                                                : MotionTokens.state)
            easing.type: Easing.OutCubic
        }
    }
    Behavior on scale {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : (appLauncher.pasteDismissPending
                                                ? MotionTokens.press
                                                : MotionTokens.state)
            easing.type: Easing.OutCubic
        }
    }

    function resetSelection() {
        grid.currentIndex = emojiPicker.resultCount > 0 ? 0 : -1
        if (grid.currentIndex >= 0)
            grid.positionViewAtIndex(grid.currentIndex, GridView.Beginning)
    }

    function stepSelection(horizontal, vertical) {
        if (emojiPicker.resultCount <= 0)
            return
        const next = Math.max(0, Math.min(emojiPicker.resultCount - 1,
                                          grid.currentIndex + horizontal
                                          + vertical * emojiPicker.columnCount))
        grid.currentIndex = next
        grid.positionViewAtIndex(next, GridView.Contain)
    }

    function activate(keepOpen) {
        if (grid.currentIndex >= 0)
            emojiPicker.paste(grid.currentIndex, keepOpen, -1)
    }

    function openActions() {
        if (grid.currentIndex >= 0)
            actionsPopup.open()
    }

    function openKeywords() {
        if (grid.currentIndex < 0)
            return
        keywordField.text = emojiPicker.customKeywords(grid.currentIndex)
        keywordPopup.open()
        keywordField.forceActiveFocus(Qt.ShortcutFocusReason)
        keywordField.selectAll()
    }

    function handleKey(event) {
        if (event.key === Qt.Key_Escape) {
            if (actionsPopup.opened)
                actionsPopup.close()
            else if (tonePopup.opened)
                tonePopup.close()
            else if (keywordPopup.opened)
                keywordPopup.close()
            else
                emojiPicker.closePicker()
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            stepSelection(-1, 0)
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            stepSelection(1, 0)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            stepSelection(0, 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            stepSelection(0, -1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if ((event.modifiers & Qt.ControlModifier)
                    && (event.modifiers & Qt.ShiftModifier))
                activate(true)
            else if (event.modifiers & Qt.ControlModifier)
                emojiPicker.copy(grid.currentIndex, -1)
            else
                activate(false)
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier)
                   && (event.modifiers & Qt.AltModifier)
                   && event.key === Qt.Key_C) {
            emojiPicker.copyUnicode(grid.currentIndex)
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier)
                   && event.key === Qt.Key_Period) {
            emojiPicker.togglePinned(grid.currentIndex)
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier)
                   && event.key === Qt.Key_E) {
            openKeywords()
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier)
                   && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) {
            emojiPicker.setColumnCount(emojiPicker.columnCount + 1)
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier)
                   && event.key === Qt.Key_Minus) {
            emojiPicker.setColumnCount(emojiPicker.columnCount - 1)
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier)
                   && event.key === Qt.Key_0) {
            emojiPicker.setColumnCount(8)
            event.accepted = true
        }
    }

    onOpenChanged: {
        if (open) {
            resetSelection()
            focusTimer.restart()
        }
    }

    Connections {
        target: emojiPicker
        function onResultsChanged() { root.resetSelection() }
        function onQueryChanged() {
            if (searchField.text !== emojiPicker.query)
                searchField.text = emojiPicker.query
        }
        function onStatusMessageRequested(message) {
            toastLabel.text = message
            toast.opacity = 1
            toastTimer.restart()
        }
    }

    Timer {
        id: focusTimer
        interval: root.reducedMotion ? 0 : 55
        onTriggered: {
            searchField.forceActiveFocus(Qt.ShortcutFocusReason)
            searchField.selectAll()
        }
    }

    Rectangle {
        id: backButton
        x: 16
        y: 19
        width: 38
        height: 46
        radius: 12
        color: backHover.hovered ? colors.raised : "transparent"
        Accessible.name: "Back to applications"
        Accessible.role: Accessible.Button

        Text {
            anchors.centerIn: parent
            text: "\uE72B"
            color: colors.secondary
            font.family: root.iconFont
            font.pixelSize: 15
        }
        HoverHandler { id: backHover }
        TapHandler { onTapped: emojiPicker.closePicker() }
        Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover } }
    }

    TextField {
        id: searchField
        x: 60
        y: 20
        width: parent.width - 272
        height: 44
        leftPadding: 40
        rightPadding: 12
        topPadding: 0
        bottomPadding: 0
        placeholderText: emojiPicker.loading ? "Loading Unicode data…" : "Search emoji and symbols"
        placeholderTextColor: colors.tertiary
        color: colors.text
        selectionColor: root.focusAccent
        selectedTextColor: colors.black
        font.family: root.uiFont
        font.pixelSize: 14
        font.weight: Font.Medium
        selectByMouse: true
        Accessible.name: "Search emoji and symbols"
        Accessible.description: "Search by name, category, keyword, or Unicode code point"
        onTextEdited: emojiPicker.setQuery(text)
        Keys.onPressed: event => root.handleKey(event)

        background: Rectangle {
            radius: 13
            color: searchField.activeFocus ? colors.raised : colors.black
            border.width: searchField.activeFocus ? 1 : 0
            border.color: searchField.activeFocus
                          ? Qt.rgba(root.focusAccent.r, root.focusAccent.g,
                                    root.focusAccent.b, 0.56)
                          : "transparent"
            Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover } }
            Behavior on border.color { ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover } }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: "\uE721"
            color: searchField.activeFocus ? colors.text : colors.secondary
            font.family: root.iconFont
            font.pixelSize: 15
        }
    }

    ComboBox {
        id: categoryBox
        x: parent.width - 204
        y: 20
        width: 188
        height: 44
        model: emojiPicker.categories
        currentIndex: Math.max(0, emojiPicker.categories.indexOf(emojiPicker.category))
        font.family: root.uiFont
        font.pixelSize: 11
        Accessible.name: "Character category"
        onActivated: emojiPicker.setCategory(currentText)

        contentItem: Text {
            leftPadding: 14
            rightPadding: 32
            text: categoryBox.displayText
            color: colors.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.family: root.uiFont
            font.pixelSize: 11
            font.weight: Font.Medium
        }
        indicator: Text {
            x: categoryBox.width - width - 14
            anchors.verticalCenter: parent.verticalCenter
            text: "\uE70D"
            color: colors.secondary
            font.family: root.iconFont
            font.pixelSize: 10
        }
        background: Rectangle {
            radius: 13
            color: categoryBox.hovered ? colors.raised : colors.black
            border.width: categoryBox.activeFocus ? 1 : 0
            border.color: categoryBox.activeFocus ? root.focusAccent : "transparent"
            Behavior on color {
                ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
            }
        }
        popup: Popup {
            y: categoryBox.height + 6
            width: categoryBox.width
            height: Math.min(282, contentItem.implicitHeight + 12)
            padding: 6
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: categoryBox.popup.visible ? categoryBox.delegateModel : null
                currentIndex: categoryBox.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: Rectangle {
                radius: 13
                color: colors.popover
                border.width: 1
                border.color: colors.divider
            }
        }
        delegate: ItemDelegate {
            id: categoryDelegate
            required property int index
            required property string modelData
            width: categoryBox.width - 12
            height: 34
            highlighted: categoryBox.highlightedIndex === categoryDelegate.index
            contentItem: Text {
                text: categoryDelegate.modelData
                color: colors.text
                verticalAlignment: Text.AlignVCenter
                font.family: root.uiFont
                font.pixelSize: 10
            }
            background: Rectangle {
                radius: 8
                color: parent.highlighted ? colors.hover : "transparent"
            }
        }
    }

    Rectangle {
        x: 16
        y: 74
        width: parent.width - 32
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.12; color: colors.divider }
            GradientStop { position: 0.88; color: colors.divider }
            GradientStop { position: 1; color: "transparent" }
        }
    }

    GridView {
        id: grid
        x: 16
        y: 82
        width: parent.width - 32
        height: parent.height - y - 58
        clip: true
        model: emojiPicker
        cellWidth: width / emojiPicker.columnCount
        cellHeight: cellWidth
        currentIndex: emojiPicker.resultCount > 0 ? 0 : -1
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 4200
        maximumFlickVelocity: 2200
        highlightFollowsCurrentItem: false
        reuseItems: true
        Accessible.name: "Emoji and symbol results"
        Accessible.role: Accessible.List

        delegate: Item {
            id: characterCell
            required property int index
            required property string glyph
            required property string name
            required property string codePoints
            required property bool pinned
            required property bool supportsSkinTone
            required property bool symbol
            width: grid.cellWidth
            height: grid.cellHeight
            Accessible.name: name + ", " + codePoints
            Accessible.role: Accessible.ListItem
            Accessible.focused: grid.currentIndex === index

            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                radius: 12
                color: grid.currentIndex === characterCell.index ? colors.raised
                                                                  : cellHover.hovered ? colors.black
                                                                                      : "transparent"
                border.width: grid.currentIndex === characterCell.index ? 1 : 0
                border.color: Qt.rgba(root.focusAccent.r, root.focusAccent.g,
                                      root.focusAccent.b, 0.52)
                scale: cellMouse.pressed ? 0.94 : 1
                Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover } }
                Behavior on scale {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.press
                        easing.type: Easing.OutCubic
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: characterCell.glyph
                    color: colors.text
                    font.family: characterCell.symbol ? "Segoe UI Symbol" : "Segoe UI Emoji"
                    font.pixelSize: Math.max(22, Math.min(34, grid.cellWidth * 0.48))
                    renderType: Text.NativeRendering
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    width: 5
                    height: 5
                    radius: 3
                    color: root.focusAccent
                    visible: characterCell.pinned
                }
            }

            HoverHandler {
                id: cellHover
                onHoveredChanged: {
                    if (hovered)
                        grid.currentIndex = characterCell.index
                }
            }
            MouseArea {
                id: cellMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    grid.currentIndex = characterCell.index
                    if (mouse.button === Qt.RightButton)
                        root.openActions()
                }
                onDoubleClicked: {
                    grid.currentIndex = characterCell.index
                    root.activate(false)
                }
            }
        }
    }

    Column {
        anchors.centerIn: grid
        spacing: 8
        visible: emojiPicker.loading || (!emojiPicker.loading && emojiPicker.resultCount === 0)
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: emojiPicker.loading ? "\uE895" : "\uE721"
            color: colors.tertiary
            font.family: root.iconFont
            font.pixelSize: 22
            RotationAnimator on rotation {
                running: emojiPicker.loading && !root.reducedMotion
                from: 0
                to: 360
                duration: 850
                loops: Animation.Infinite
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: emojiPicker.loading ? "Loading Unicode 17.0"
                                      : emojiPicker.errorMessage.length > 0
                                        ? emojiPicker.errorMessage : "No matching characters"
            color: colors.secondary
            font.family: root.uiFont
            font.pixelSize: 11
        }
    }

    Item {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 50

        Rectangle {
            x: 18
            y: 0
            width: parent.width - 36
            height: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.12; color: colors.divider }
                GradientStop { position: 0.88; color: colors.divider }
                GradientStop { position: 1; color: "transparent" }
            }
        }

        Row {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9
            Text {
                text: root.selectedItem.glyph || ""
                color: colors.text
                font.family: "Segoe UI Emoji"
                font.pixelSize: 20
                renderType: Text.NativeRendering
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: 230
                    text: root.selectedItem.name || ""
                    color: colors.text
                    elide: Text.ElideRight
                    font.family: root.uiFont
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
                Text {
                    text: root.selectedItem.codePoints || ""
                    color: colors.tertiary
                    font.family: "Geist Mono"
                    font.pixelSize: 8
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 13
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Rectangle {
                width: pasteHint.width + 18
                height: 28
                radius: 9
                color: Qt.rgba(root.focusAccent.r, root.focusAccent.g,
                               root.focusAccent.b, hintHover.hovered ? 0.24 : 0.15)
                border.width: 1
                border.color: Qt.rgba(root.focusAccent.r, root.focusAccent.g,
                                      root.focusAccent.b,
                                      hintHover.hovered ? 0.46 : 0.28)
                Text {
                    id: pasteHint
                    anchors.centerIn: parent
                    text: "Paste   ↵"
                    color: colors.text
                    font.family: root.uiFont
                    font.pixelSize: 9
                    font.weight: Font.Medium
                }
                HoverHandler { id: hintHover }
                TapHandler {
                    id: pasteTap
                    onTapped: root.activate(false)
                }
                scale: pasteTap.pressed ? 0.96 : 1
                Behavior on color {
                    ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.press
                        easing.type: Easing.OutCubic
                    }
                }
            }
            Rectangle {
                width: 32
                height: 28
                radius: 9
                color: actionHover.hovered ? colors.raised : colors.black
                border.width: 0
                Text {
                    anchors.centerIn: parent
                    text: "\uE712"
                    color: colors.secondary
                    font.family: root.iconFont
                    font.pixelSize: 13
                }
                Accessible.name: "Character actions"
                Accessible.role: Accessible.Button
                HoverHandler { id: actionHover }
                TapHandler {
                    id: actionTap
                    onTapped: root.openActions()
                }
                scale: actionTap.pressed ? 0.94 : 1
                Behavior on color {
                    ColorAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : MotionTokens.press
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }

    Popup {
        id: actionsPopup
        x: parent.width - width - 16
        y: parent.height - height - 54
        width: 224
        padding: 6
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 14
            color: colors.popover
            border.width: 1
            border.color: colors.divider
        }
        contentItem: Column {
            spacing: 2
            Repeater {
                model: [
                    { label: "Copy", hint: "Ctrl Enter", action: "copy" },
                    { label: "Paste and Keep Open", hint: "Ctrl Shift Enter", action: "keep" },
                    { label: root.selectedItem.pinned ? "Unpin" : "Pin", hint: "Ctrl .", action: "pin" },
                    { label: "Copy Unicode", hint: "Ctrl Alt C", action: "unicode" },
                    { label: "Edit Keywords", hint: "Ctrl E", action: "keywords" },
                    { label: "Choose Skin Tone", hint: "", action: "tone" }
                ]
                delegate: Rectangle {
                    id: actionRow
                    required property var modelData
                    width: 212
                    height: 34
                    radius: 9
                    visible: modelData.action !== "tone" || root.selectedItem.supportsSkinTone
                    color: actionRowHover.hovered ? colors.hover : "transparent"
                    Text {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: actionRow.modelData.label
                        color: colors.text
                        font.family: root.uiFont
                        font.pixelSize: 10
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: actionRow.modelData.hint
                        color: colors.tertiary
                        font.family: root.uiFont
                        font.pixelSize: 8
                    }
                    HoverHandler { id: actionRowHover }
                    TapHandler {
                        onTapped: {
                            const action = actionRow.modelData.action
                            actionsPopup.close()
                            if (action === "copy")
                                emojiPicker.copy(grid.currentIndex, -1)
                            else if (action === "keep")
                                root.activate(true)
                            else if (action === "pin")
                                emojiPicker.togglePinned(grid.currentIndex)
                            else if (action === "unicode")
                                emojiPicker.copyUnicode(grid.currentIndex)
                            else if (action === "keywords")
                                root.openKeywords()
                            else if (action === "tone")
                                tonePopup.open()
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: tonePopup
        x: parent.width - width - 16
        y: parent.height - height - 54
        width: 310
        height: 64
        padding: 7
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 14
            color: colors.popover
            border.width: 1
            border.color: colors.divider
        }
        contentItem: Row {
            spacing: 3
            Repeater {
                model: emojiPicker.skinToneVariants(grid.currentIndex)
                delegate: Rectangle {
                    id: toneCell
                    required property int index
                    required property string modelData
                    width: 46
                    height: 46
                    radius: 10
                    color: toneHover.hovered || emojiPicker.defaultSkinTone === index
                           ? colors.hover : "transparent"
                    border.width: emojiPicker.defaultSkinTone === index ? 1 : 0
                    border.color: root.focusAccent
                    Text {
                        anchors.centerIn: parent
                        text: toneCell.modelData
                        font.family: "Segoe UI Emoji"
                        font.pixelSize: 24
                        renderType: Text.NativeRendering
                    }
                    HoverHandler { id: toneHover }
                    TapHandler {
                        onTapped: {
                            emojiPicker.setDefaultSkinTone(toneCell.index)
                            tonePopup.close()
                            emojiPicker.paste(grid.currentIndex, false, toneCell.index)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: keywordPopup
        x: 72
        y: 88
        width: parent.width - 144
        height: 124
        padding: 14
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 15
            color: colors.popover
            border.width: 1
            border.color: colors.divider
        }
        contentItem: Column {
            spacing: 10
            Text {
                text: "Custom keywords for " + (root.selectedItem.name || "character")
                color: colors.text
                font.family: root.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            TextField {
                id: keywordField
                width: parent.width
                height: 40
                placeholderText: "Separate keywords with spaces"
                color: colors.text
                placeholderTextColor: colors.tertiary
                selectionColor: root.focusAccent
                font.family: root.uiFont
                font.pixelSize: 10
                Keys.onReturnPressed: {
                    emojiPicker.setCustomKeywords(grid.currentIndex, text)
                    keywordPopup.close()
                    searchField.forceActiveFocus(Qt.ShortcutFocusReason)
                }
                background: Rectangle {
                    radius: 10
                    color: keywordField.activeFocus ? colors.raised : colors.black
                    border.width: keywordField.activeFocus ? 1 : 0
                    border.color: keywordField.activeFocus ? root.focusAccent : "transparent"
                }
            }
        }
    }

    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 58
        height: 30
        width: toastLabel.width + 24
        radius: 10
        color: colors.popover
        border.width: 1
        border.color: colors.divider
        opacity: 0
        visible: opacity > 0.001
        Text {
            id: toastLabel
            anchors.centerIn: parent
            color: colors.text
            font.family: root.uiFont
            font.pixelSize: 9
            font.weight: Font.Medium
        }
        Behavior on opacity {
            NumberAnimation { duration: root.reducedMotion ? 0 : MotionTokens.hover }
        }
        Timer {
            id: toastTimer
            interval: 1200
            onTriggered: toast.opacity = 0
        }
    }
}
