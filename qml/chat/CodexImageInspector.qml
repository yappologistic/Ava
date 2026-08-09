import QtQuick 6.5
import QtQuick.Controls 6.5

FocusScope {
    id: root

    property string uiFont: "Segoe UI"
    property bool reducedMotion: false
    property real devicePixelRatio: 1
    property bool opened: false
    property string currentSource: ""
    property string currentName: "Image"

    signal closed()

    function openImage(source, name) {
        if (!source || source.length === 0)
            return
        clearSourceTimer.stop()
        currentSource = source
        currentName = name && name.length > 0 ? name : "Image"
        opened = true
        forceActiveFocus()
    }

    function close() {
        if (!opened)
            return
        opened = false
        clearSourceTimer.restart()
        closed()
    }

    visible: opened || opacity > 0
    enabled: opened
    opacity: opened ? 1 : 0
    scale: opened ? 1 : 0.985
    Accessible.role: Accessible.Dialog
    Accessible.name: "Image preview, " + currentName

    Behavior on opacity {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : 150
            easing.type: Easing.OutCubic
        }
    }

    Behavior on scale {
        NumberAnimation {
            duration: root.reducedMotion ? 0 : 170
            easing.type: Easing.OutCubic
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            root.close()
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#e80a0a0c"
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: root.close()
    }

    Item {
        id: imageViewport
        anchors.fill: parent
        anchors.margins: 48

        Image {
            id: fullImage
            objectName: "inspectedImage"
            anchors.fill: parent
            source: root.currentSource
            sourceSize.width: Math.min(2560,
                                       Math.ceil(width * root.devicePixelRatio))
            sourceSize.height: Math.min(2560,
                                        Math.ceil(height * root.devicePixelRatio))
            asynchronous: true
            cache: false
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: false
        }

        MouseArea {
            x: (parent.width - width) / 2
            y: (parent.height - height) / 2
            width: fullImage.paintedWidth
            height: fullImage.paintedHeight
            acceptedButtons: Qt.LeftButton
            onClicked: function(mouse) { mouse.accepted = true }
        }

        Column {
            anchors.centerIn: parent
            width: Math.min(360, parent.width - 40)
            spacing: 9
            visible: fullImage.status === Image.Error

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 24
                height: 24
                source: Qt.resolvedUrl("../../assets/icons/fluent-chat/error.svg")
                sourceSize: Qt.size(20, 20)
                opacity: 0.64
            }

            Text {
                width: parent.width
                text: root.currentName
                color: "#a8a8af"
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                font.family: root.uiFont
                font.pixelSize: 11
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.top: parent.top
        anchors.topMargin: 16
        width: Math.max(0, parent.width - 84)
        text: root.currentName
        color: "#b7b7bd"
        elide: Text.ElideMiddle
        font.family: root.uiFont
        font.pixelSize: 10
    }

    ChatIconButton {
        id: closeButton
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.top: parent.top
        anchors.topMargin: 12
        width: 34
        height: 34
        iconSource: Qt.resolvedUrl("../../assets/icons/fluent-chat/dismiss.svg")
        accessibleName: "Close image preview"
        baseColor: "#1b1b1f"
        hoverColor: "#2b2b30"
        pressedColor: "#38383e"
        onClicked: root.close()
    }

    Timer {
        id: clearSourceTimer
        interval: root.reducedMotion ? 1 : 180
        onTriggered: {
            if (!root.opened)
                root.currentSource = ""
        }
    }
}
