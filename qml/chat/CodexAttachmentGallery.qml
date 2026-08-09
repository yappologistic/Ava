import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property var attachments: []
    property string uiFont: "Segoe UI"
    property bool reducedMotion: false
    property real devicePixelRatio: 1

    signal inspectImageRequested(string source, string name)

    readonly property int imageCount: attachments ? attachments.length : 0
    readonly property int galleryRows: imageCount > 2 ? 2 : 1
    readonly property real gap: 6
    readonly property real tileWidth: imageCount === 1
                                               ? width : Math.max(128, (width - gap) / 2)
    readonly property real tileHeight: imageCount === 1
                                                ? Math.min(220, Math.max(168, width * 0.54))
                                                : (imageCount === 2 ? 166 : 124)

    implicitHeight: imageCount > 0
                    ? galleryRows * (tileHeight + (galleryRows > 1 ? gap : 0)) : 0

    function activate(index) {
        if (!attachments || index < 0 || index >= attachments.length)
            return
        const attachment = attachments[index]
        const source = attachment.previewUrl || ""
        if (source.length === 0)
            return
        root.inspectImageRequested(source, attachment.name || "Image")
    }

    GridView {
        id: gallery
        anchors.fill: parent
        model: root.attachments || []
        flow: GridView.FlowTopToBottom
        cellWidth: root.tileWidth + root.gap
        cellHeight: root.tileHeight + root.gap
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 3600
        maximumFlickVelocity: 1800
        reuseItems: true
        keyNavigationWraps: false
        interactive: contentWidth > width + root.gap
        Accessible.name: root.imageCount === 1
                         ? "Attached image" : root.imageCount + " attached images"

        ScrollBar.horizontal: ScrollBar {
            policy: gallery.contentWidth > gallery.width + root.gap
                    ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        delegate: Rectangle {
            id: attachmentTile

            required property int index
            required property var modelData

            objectName: "attachmentTile" + index
            width: root.tileWidth
            height: root.tileHeight
            radius: 9
            color: "#0d0d0f"
            border.width: activeFocus ? 1 : 0
            border.color: "#79d8ce"
            activeFocusOnTab: previewSource.length > 0
            Accessible.role: previewSource.length > 0
                             ? Accessible.Button : Accessible.StaticText
            Accessible.name: previewSource.length > 0
                             ? "Open " + attachmentName : attachmentName

            readonly property string previewSource: modelData.previewUrl || ""
            readonly property string attachmentName: modelData.name || "Image"

            Image {
                id: thumbnail
                anchors.fill: parent
                anchors.margins: 1
                source: attachmentTile.previewSource
                sourceSize.width: Math.min(768,
                                           Math.ceil(width * root.devicePixelRatio))
                sourceSize.height: Math.min(768,
                                            Math.ceil(height * root.devicePixelRatio))
                asynchronous: true
                cache: false
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: false
            }

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: hoverHandler.hovered ? "#14ffffff" : "transparent"
                visible: thumbnail.status === Image.Ready

                Behavior on color {
                    ColorAnimation { duration: root.reducedMotion ? 0 : 90 }
                }
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - 24
                spacing: 7
                visible: thumbnail.status === Image.Error
                         || attachmentTile.previewSource.length === 0

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "\uE91B"
                    color: "#67676f"
                    font.family: "Segoe Fluent Icons"
                    font.pixelSize: 18
                }

                Text {
                    width: parent.width
                    text: attachmentTile.attachmentName
                    color: "#888890"
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    font.family: root.uiFont
                    font.pixelSize: 10
                }
            }

            HoverHandler {
                id: hoverHandler
                enabled: attachmentTile.previewSource.length > 0
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            }

            TapHandler {
                enabled: attachmentTile.previewSource.length > 0
                acceptedButtons: Qt.LeftButton
                onTapped: root.activate(attachmentTile.index)
            }

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                        || event.key === Qt.Key_Space) {
                    root.activate(attachmentTile.index)
                    event.accepted = true
                }
            }
        }
    }
}
