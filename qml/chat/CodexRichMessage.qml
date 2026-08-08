import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root

    property string markdown: ""
    signal openUrlRequested(string url)

    implicitHeight: content.implicitHeight

    Column {
        id: content
        width: parent.width
        spacing: 10

        Repeater {
            model: chatTextStyler.renderSegments(root.markdown)

            delegate: Loader {
                required property var modelData
                width: content.width
                height: item ? item.implicitHeight : 0
                sourceComponent: modelData.kind === "code"
                                 ? codeComponent : proseComponent

                onLoaded: item.segment = modelData
            }
        }
    }

    Component {
        id: proseComponent

        Item {
            property var segment: ({})
            implicitHeight: prose.contentHeight

            TextEdit {
                id: prose
                width: parent.width
                height: contentHeight
                text: parent.segment.html
                textFormat: TextEdit.RichText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                selectionColor: "#355b65"
                selectedTextColor: "#ffffff"
                color: "#dedee3"
                font.family: uiFont
                font.pixelSize: 13
                Accessible.name: root.markdown
                onLinkActivated: function(link) { root.openUrlRequested(link) }
            }
        }
    }

    Component {
        id: codeComponent

        CodexCodeBlock {
            property var segment: ({})
            code: segment.code
            highlightedHtml: segment.html
            language: segment.language
        }
    }
}
