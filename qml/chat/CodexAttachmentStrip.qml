import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property var attachmentModel: null
    property string uiFont: "Segoe UI"
    property bool reducedMotion: false
    property real devicePixelRatio: 1

    signal removeRequested(int index)

    readonly property int itemCount: attachmentList.count

    implicitHeight: itemCount > 0 ? 40 : 0

    ListView {
        id: attachmentList
        anchors.fill: parent
        orientation: ListView.Horizontal
        spacing: 6
        clip: true
        model: root.attachmentModel
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 3600
        maximumFlickVelocity: 1800
        reuseItems: true
        Accessible.name: "Message attachments"

        ScrollBar.horizontal: ScrollBar {
            policy: attachmentList.contentWidth > attachmentList.width
                    ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        delegate: Rectangle {
            id: attachmentChip

            required property int index
            required property string attachmentName
            required property string attachmentKind
            required property string previewUrl

            height: 38
            width: Math.min(214, attachmentLabel.implicitWidth + 64)
            radius: 10
            color: "#19191d"

            Image {
                id: attachmentImage
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 30
                visible: attachmentChip.attachmentKind === "image"
                         && attachmentChip.previewUrl.length > 0
                         && status !== Image.Error
                source: attachmentChip.previewUrl
                sourceSize.width: Math.min(96,
                                           Math.ceil(width * root.devicePixelRatio))
                sourceSize.height: Math.min(96,
                                            Math.ceil(height * root.devicePixelRatio))
                asynchronous: true
                cache: false
                fillMode: Image.PreserveAspectCrop
                smooth: true
                mipmap: false
            }

            Image {
                id: typeIcon
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                visible: !attachmentImage.visible
                source: attachmentChip.attachmentKind === "audio"
                        ? Qt.resolvedUrl("../../assets/icons/fluent-chat/audio.svg")
                        : Qt.resolvedUrl("../../assets/icons/fluent-chat/document.svg")
                sourceSize: Qt.size(20, 20)
                opacity: 0.72
            }

            Text {
                id: attachmentLabel
                x: attachmentImage.visible ? 40 : 32
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - x - 34
                text: attachmentChip.attachmentName
                color: "#c5c5cb"
                elide: Text.ElideMiddle
                font.family: root.uiFont
                font.pixelSize: 10
            }

            ChatIconButton {
                id: removeButton
                anchors.right: parent.right
                anchors.rightMargin: 3
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32
                iconSource: Qt.resolvedUrl("../../assets/icons/fluent-chat/dismiss.svg")
                iconSize: 14
                accessibleName: "Remove " + attachmentChip.attachmentName
                foregroundColor: "#888890"
                hoverColor: "#252529"
                pressedColor: "#303035"
                onClicked: root.removeRequested(attachmentChip.index)
            }
        }

        add: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: root.reducedMotion ? 0 : 120
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1
                duration: root.reducedMotion ? 0 : 140
                easing.type: Easing.OutCubic
            }
        }

        remove: Transition {
            NumberAnimation {
                property: "opacity"
                to: 0
                duration: root.reducedMotion ? 0 : 90
            }
        }
    }
}
