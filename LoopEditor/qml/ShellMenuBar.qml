import QtQuick
import QtQuick.Controls

MenuBar {
    id: root

    property var host: editorHost
    property var window: null

    readonly property var menuModel: MenuModel {
        host: root.host
        window: root.window
    }

    Repeater {
        model: root.menuModel.menuGroups

        delegate: Menu {
            required property string modelData
            title: {
                switch (modelData) {
                case "File": return qsTr("&File")
                case "Edit": return qsTr("&Edit")
                case "View": return qsTr("&View")
                case "Document": return qsTr("&Document")
                case "Production": return qsTr("&Production")
                case "Preflight": return qsTr("&Preflight")
                case "Help": return qsTr("&Help")
                case "Advanced": return qsTr("&Advanced")
                default: return modelData
                }
            }
            visible: modelData !== "Advanced" || (root.host && root.host.allowDeveloperDiagnostics)

            Instantiator {
                model: root.menuModel.descriptorsForGroup(modelData)

                delegate: MenuItem {
                    required property var modelData
                    text: root.menuModel.labelForEntry(modelData)
                    enabled: root.menuModel.commandEnabled(modelData.id)
                    onTriggered: root.menuModel.invoke(modelData.id)
                }

                onObjectAdded: function(index, object) {
                    parent.addItem(object)
                }
                onObjectRemoved: function(index, object) {
                    parent.removeItem(object)
                }
            }
        }
    }
}
