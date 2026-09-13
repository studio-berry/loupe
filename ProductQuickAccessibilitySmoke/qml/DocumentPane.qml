import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Loop.Quick

Item {
    id: root

    property var host: editorHost
    property var documentModel: host ? host.documentModel : null

    function revealSearch() {
        tabBar.currentIndex = 2
        searchField.forceActiveFocus()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            Layout.preferredWidth: 230
            Layout.fillHeight: true
            padding: 8

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                TabBar {
                    id: tabBar
                    Layout.fillWidth: true

                    TabButton {
                        text: qsTr("Pages")
                    }
                    TabButton {
                        text: qsTr("Outline")
                    }
                    TabButton {
                        text: qsTr("Search")
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabBar.currentIndex

                    ListView {
                        id: pagesView
                        clip: true
                        focus: true
                        activeFocusOnTab: true
                        model: root.documentModel ? root.documentModel.pages : null
                        Accessible.name: qsTr("Page thumbnails")

                        delegate: ItemDelegate {
                            width: pagesView.width
                            text: qsTr("Page %1  %2 × %3").arg(pageNumber).arg(Math.round(pageWidth)).arg(Math.round(pageHeight))
                            highlighted: root.host && root.host.currentPage === index
                            Accessible.name: text
                            onClicked: if (root.host)
                                root.host.goToPage(index)
                        }
                    }

                    TreeView {
                        id: outlineView
                        clip: true
                        focus: true
                        activeFocusOnTab: true
                        model: root.documentModel ? root.documentModel.outline : null
                        Accessible.name: qsTr("Document outline")

                        delegate: ItemDelegate {
                            width: outlineView.width
                            text: model.display !== undefined ? model.display : title
                            Accessible.name: text
                            enabled: page >= 0
                            onClicked: if (root.host && page >= 0)
                                root.host.goToOutlinePage(page)
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: outlineView.count === 0
                            text: qsTr("No outline")
                        }
                    }

                    ColumnLayout {
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: searchField
                                Layout.fillWidth: true
                                placeholderText: qsTr("Find in document")
                                focus: true
                                Accessible.name: qsTr("Search text")
                                onAccepted: if (root.documentModel)
                                    root.documentModel.search(text)
                            }
                            Button {
                                text: qsTr("Find")
                                enabled: searchField.text.length > 0 && !!root.documentModel
                                onClicked: root.documentModel.search(searchField.text)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                text: qsTr("Previous")
                                enabled: root.host && root.host.commandEpoch >= 0 && root.host.isCommandEnabled("actionFindPrevious")
                                onClicked: if (root.host)
                                    root.host.invokeCommand("actionFindPrevious")
                            }
                            Button {
                                text: qsTr("Next")
                                enabled: root.host && root.host.commandEpoch >= 0 && root.host.isCommandEnabled("actionFindNext")
                                onClicked: if (root.host)
                                    root.host.invokeCommand("actionFindNext")
                            }
                            Label {
                                Layout.fillWidth: true
                                text: resultsView.count > 0 ? qsTr("%1 result(s)").arg(resultsView.count) : qsTr("No results")
                            }
                        }

                        ListView {
                            id: resultsView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: root.documentModel ? root.documentModel.searchResults : null
                            Accessible.name: qsTr("Search results")

                            delegate: ItemDelegate {
                                width: resultsView.width
                                text: qsTr("Page %1: %2").arg(page + 1).arg(context)
                                Accessible.name: text
                                onClicked: if (root.host)
                                    root.host.goToPage(page)
                            }
                        }
                    }
                }
            }
        }

        CanvasPane {
            id: canvasPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            host: root.host
            Accessible.name: qsTr("Document canvas pane")
        }

        InspectorPane {
            id: inspectorPane
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            host: root.host
            Accessible.name: qsTr("Contextual inspector dock")
        }
    }

    Connections {
        target: root.host
        function onPresentationChanged() {
            if (root.host && root.host.searchPanelVisible) {
                root.revealSearch()
                root.host.acknowledgeSearchPanel()
            }
        }
    }
}
