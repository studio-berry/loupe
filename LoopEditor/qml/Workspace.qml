import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Loop.Quick

Item {
    id: root

    property var host: editorHost
    readonly property bool preferReducedMotion: host ? host.preferReducedMotion : false

    function workspaceIndex(workspaceValue) {
        switch (workspaceValue) {
        case EditorHost.Document: return 0
        case EditorHost.Preflight: return 1
        case EditorHost.ProductionPreview: return 2
        case EditorHost.Pages: return 3
        case EditorHost.Inspect: return 4
        case EditorHost.Fix: return 5
        case EditorHost.Compare: return 6
        default: return 0
        }
    }

    function setWorkspaceFromRail(workspaceValue) {
        if (!host || workspaceValue === EditorHost.Compare) {
            return
        }
        host.setWorkspace(workspaceValue)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ShellToolBar {
            Layout.fillWidth: true
            host: root.host
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Pane {
                id: workspaceRail
                Layout.preferredWidth: 132
                Layout.fillHeight: true
                padding: 8

                focus: true
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("Workspace rail")

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Repeater {
                        model: [
                            { label: qsTr("Document"), workspace: EditorHost.Document, enabled: true },
                            { label: qsTr("Preflight"), workspace: EditorHost.Preflight, enabled: true },
                            { label: qsTr("Production Preview"), workspace: EditorHost.ProductionPreview, enabled: true },
                            { label: qsTr("Pages / Production"), workspace: EditorHost.Pages, enabled: true },
                            { label: qsTr("Inspect"), workspace: EditorHost.Inspect, enabled: true },
                            { label: qsTr("Fix"), workspace: EditorHost.Fix, enabled: true },
                            { label: qsTr("Compare"), workspace: EditorHost.Compare, enabled: false }
                        ]

                        delegate: ToolButton {
                            required property string label
                            required property int workspace
                            required property bool enabled

                            Layout.fillWidth: true
                            text: label
                            checkable: true
                            enabled: enabled
                            checked: host && host.workspace === workspace
                            onClicked: root.setWorkspaceFromRail(workspace)
                            Accessible.name: workspace === EditorHost.Compare
                                ? qsTr("Compare workspace (product decision pending)")
                                : qsTr("%1 workspace").arg(label)
                            Accessible.description: workspace === EditorHost.Compare
                                ? qsTr("Compare is disabled until the product decision is approved.")
                                : ""
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            StackLayout {
                id: workspaceStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: host ? root.workspaceIndex(host.workspace) : 0

                DocumentPane {
                    id: documentPane
                    host: root.host
                }

                PreflightPane {
                    host: root.host
                }

                WorkspacePlaceholderPane {
                    host: root.host
                    titleText: qsTr("Production Preview")
                    descriptionText: qsTr("Soft proofing and output preview will appear here.")
                    Accessible.name: qsTr("Production Preview workspace")
                }

                WorkspacePlaceholderPane {
                    host: root.host
                    titleText: qsTr("Pages / Production")
                    descriptionText: qsTr("Page assembly and production geometry will appear here.")
                    Accessible.name: qsTr("Pages and Production workspace")
                }

                WorkspacePlaceholderPane {
                    host: root.host
                    titleText: qsTr("Inspect")
                    descriptionText: qsTr("Dedicated inspection tools will appear here. Use the document inspector dock for contextual selection.")
                    Accessible.name: qsTr("Inspect workspace")
                }

                WorkspacePlaceholderPane {
                    host: root.host
                    titleText: qsTr("Fix")
                    descriptionText: qsTr("Bounded corrective operations will appear here.")
                    Accessible.name: qsTr("Fix workspace")
                }

                WorkspacePlaceholderPane {
                    host: root.host
                    titleText: qsTr("Compare")
                    descriptionText: qsTr("Compare remains deferred pending the product decision.")
                    Accessible.name: qsTr("Compare workspace placeholder")
                }
            }
        }
    }

    KeyNavigation.tab: documentPane

    Connections {
        target: root.host
        function onWorkspaceChanged() {
            if (root.host) {
                workspaceStack.currentIndex = root.workspaceIndex(root.host.workspace)
            }
        }
        function onPresentationChanged() {
            if (root.host && root.host.workspaceRequest >= 0) {
                root.host.acknowledgeWorkspaceRequest()
            }
        }
    }
}
