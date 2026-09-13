import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root

    property var host: editorHost
    property string titleText: ""
    property string descriptionText: ""

    padding: 24

    Accessible.role: Accessible.Grouping
    Accessible.name: titleText

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.bold: true
            text: root.titleText
            Accessible.name: root.titleText
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.descriptionText
            Accessible.name: root.descriptionText
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: root.host && root.host.hasDocument
            text: qsTr("Document: %1").arg(root.host.displayTitle)
        }

        Item { Layout.fillHeight: true }
    }
}
