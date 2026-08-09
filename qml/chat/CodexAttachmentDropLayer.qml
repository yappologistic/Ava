import QtQuick 6.5

Item {
    id: root

    property bool available: true
    property bool blocked: false
    property bool reducedMotion: false
    property string uiFont: "Segoe UI"

    signal filesDropped(var urls)

    readonly property bool dragActive: dropArea.containsDrag

    DropArea {
        id: dropArea
        anchors.fill: parent
        enabled: root.available && !root.blocked
        keys: ["text/uri-list"]

        onDropped: function(drop) {
            if (!drop.urls || drop.urls.length === 0)
                return
            root.filesDropped(drop.urls)
            drop.acceptProposedAction()
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 8
        visible: root.dragActive
        opacity: visible ? 1 : 0
        radius: 12
        color: "#d90c0c0f"
        border.width: 1
        border.color: "#5b8e8a"

        Behavior on opacity {
            NumberAnimation { duration: root.reducedMotion ? 0 : 100 }
        }

        Text {
            anchors.centerIn: parent
            text: "Drop to attach"
            color: "#d5d5da"
            font.family: root.uiFont
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }
}
