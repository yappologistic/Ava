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
        width: Math.max(0, parent.width - root.cornerExtent * 2)
        height: root.edgeThickness
        cursorShape: Qt.SizeVerCursor
        onPressed: root.beginResize(Qt.TopEdge)
    }
    MouseArea {
        x: root.cornerExtent
        anchors.bottom: parent.bottom
        width: Math.max(0, parent.width - root.cornerExtent * 2)
        height: root.edgeThickness
        cursorShape: Qt.SizeVerCursor
        onPressed: root.beginResize(Qt.BottomEdge)
    }
    MouseArea {
        y: root.cornerExtent
        width: root.edgeThickness
        height: Math.max(0, parent.height - root.cornerExtent * 2)
        cursorShape: Qt.SizeHorCursor
        onPressed: root.beginResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        y: root.cornerExtent
        width: root.edgeThickness
        height: Math.max(0, parent.height - root.cornerExtent * 2)
        cursorShape: Qt.SizeHorCursor
        onPressed: root.beginResize(Qt.RightEdge)
    }
    MouseArea {
        width: root.cornerExtent
        height: root.cornerExtent
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.beginResize(Qt.TopEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        width: root.cornerExtent
        height: root.cornerExtent
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.beginResize(Qt.TopEdge | Qt.RightEdge)
    }
    MouseArea {
        anchors.bottom: parent.bottom
        width: root.cornerExtent
        height: root.cornerExtent
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.beginResize(Qt.BottomEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.cornerExtent
        height: root.cornerExtent
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.beginResize(Qt.BottomEdge | Qt.RightEdge)
    }
}
