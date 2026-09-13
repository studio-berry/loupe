import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root

    property var host: editorHost
    property var commandMap: ({})

    function rebuildCommands() {
        const next = {}
        if (!host) {
            commandMap = next
            return
        }
        const descriptors = host.commandDescriptors()
        for (let index = 0; index < descriptors.length; ++index) {
            const entry = descriptors[index]
            next[entry.id] = entry
        }
        commandMap = next
    }

    function commandEnabled(commandId) {
        const entry = commandMap[commandId]
        return !!entry && entry.enabled === true
    }

    function invoke(commandId) {
        if (host) {
            host.invokeCommand(commandId)
        }
    }

    function shortcutText(commandId) {
        const entry = commandMap[commandId]
        return entry ? (entry.shortcutText || "") : ""
    }

    Component.onCompleted: rebuildCommands()

    Connections {
        target: host
        function onCommandEpochChanged() {
            root.rebuildCommands()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 6

        ToolButton {
            text: qsTr("Open")
            enabled: root.commandEnabled("actionOpen")
            onClicked: root.invoke("actionOpen")
            Accessible.name: qsTr("Open document")
        }
        ToolButton {
            text: qsTr("Save")
            enabled: root.commandEnabled("actionSave")
            onClicked: root.invoke("actionSave")
            Accessible.name: qsTr("Save document")
        }
        ToolButton {
            text: qsTr("Export")
            enabled: root.commandEnabled("actionSave_As")
            onClicked: root.invoke("actionSave_As")
            Accessible.name: qsTr("Export document")
        }
        ToolSeparator {}
        ToolButton {
            text: qsTr("Undo")
            enabled: root.commandEnabled("actionUndo")
            onClicked: root.invoke("actionUndo")
            Accessible.name: qsTr("Undo")
        }
        ToolButton {
            text: qsTr("Redo")
            enabled: root.commandEnabled("actionRedo")
            onClicked: root.invoke("actionRedo")
            Accessible.name: qsTr("Redo")
        }
        ToolSeparator {}
        ToolButton {
            text: qsTr("Select")
            checkable: true
            checked: true
            Accessible.name: qsTr("Select tool")
        }
        ToolButton {
            text: qsTr("Hand")
            checkable: true
            Accessible.name: qsTr("Hand tool")
        }
        ToolSeparator {}
        ToolButton {
            text: qsTr("Zoom In")
            enabled: root.commandEnabled("actionZoom_In")
            onClicked: root.invoke("actionZoom_In")
            Accessible.name: qsTr("Zoom in")
        }
        ToolButton {
            text: qsTr("Zoom Out")
            enabled: root.commandEnabled("actionZoom_Out")
            onClicked: root.invoke("actionZoom_Out")
            Accessible.name: qsTr("Zoom out")
        }
        ToolSeparator {}
        ToolButton {
            text: qsTr("Preflight")
            enabled: root.host !== null
            onClicked: if (root.host) root.host.setWorkspace(EditorHost.Preflight)
            Accessible.name: qsTr("Open preflight workspace")
        }
        ToolButton {
            text: qsTr("Preview")
            enabled: root.host !== null
            onClicked: if (root.host) root.host.setWorkspace(EditorHost.ProductionPreview)
            Accessible.name: qsTr("Open production preview workspace")
        }

        Item { Layout.fillWidth: true }
    }
}
