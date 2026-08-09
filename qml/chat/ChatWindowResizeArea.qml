import QtQuick 6.5

Item {
    id: root

    required property var targetWindow
    property int edgeThickness: 7
    property int cornerExtent: 14

    anchors.fill: parent
    enabled: targetWindow.visibility === Window.Windowed
    visible: enabled

    function beginResize(edges) {
        targetWindow.startSystemResize(edges)
    }

    MouseArea {
        x: root.cornerExtent
        y: 0
        width: Math.max(0, parent.width - root.cornerExtent * 2)
        height: root.edgeThickness
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.TopEdge)
    }
    MouseArea {
        x: root.cornerExtent
        anchors.bottom: parent.bottom
        width: Math.max(0, parent.width - root.cornerExtent * 2)
        height: root.edgeThickness
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.BottomEdge)
    }
    MouseArea {
        x: 0
        y: root.cornerExtent
        width: root.edgeThickness
        height: Math.max(0, parent.height - root.cornerExtent * 2)
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        y: root.cornerExtent
        width: root.edgeThickness
        height: Math.max(0, parent.height - root.cornerExtent * 2)
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.RightEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.cornerExtent
        height: root.cornerExtent
        cursorShape: Qt.SizeFDiagCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.TopEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        width: root.cornerExtent
        height: root.cornerExtent
        cursorShape: Qt.SizeBDiagCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.TopEdge | Qt.RightEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: root.cornerExtent
        height: root.cornerExtent
        cursorShape: Qt.SizeBDiagCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.BottomEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.cornerExtent
        height: root.cornerExtent
        cursorShape: Qt.SizeFDiagCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.BottomEdge | Qt.RightEdge)
    }
}
