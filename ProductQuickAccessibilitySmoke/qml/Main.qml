import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs

import Loop.Quick

ApplicationWindow {
    id: window

    property var host: editorHost
    readonly property bool preferReducedMotion: host ? host.preferReducedMotion : false

    visible: true
    width: 1024
    height: 768
    title: host && host.displayTitle.length > 0 ? host.displayTitle : qsTr("Loop")

    Connections {
        target: host
        function onPresentationChanged() {
            if (host && host.displayTitle.length > 0) {
                window.title = host.displayTitle
            } else {
                window.title = qsTr("Loop")
            }
            if (host) {
                if (host.fullscreenRequested) {
                    window.visibility = Window.FullScreen
                } else if (window.visibility === Window.FullScreen) {
                    window.visibility = Window.Windowed
                }
            }
        }
    }

    menuBar: ShellMenuBar {
        host: window.host
        window: window
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open PDF")
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: {
            if (host) {
                host.openFileUrl(selectedFile)
            }
            if (host && host.focusRestoration) host.focusRestoration.restore()
        }
        onRejected: if (host && host.focusRestoration) host.focusRestoration.restore()
    }

    FileDialog {
        id: saveAsDialog
        title: qsTr("Save PDF As")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: {
            if (host) {
                host.saveAsFileUrl(selectedFile)
            }
            if (host && host.focusRestoration) host.focusRestoration.restore()
        }
        onRejected: if (host && host.focusRestoration) host.focusRestoration.restore()
    }

    FileDialog {
        id: preflightReportDialog
        title: qsTr("Export Preflight Report")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("JSON files (*.json)")]
        onAccepted: {
            if (host) {
                host.exportPreflightReportFileUrl(selectedFile)
            }
            if (host && host.focusRestoration) host.focusRestoration.restore()
        }
        onRejected: if (host && host.focusRestoration) host.focusRestoration.restore()
    }

    Connections {
        target: host
        function onPreflightReportExportRequested() {
            preflightReportDialog.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            id: stateBanner
            Layout.fillWidth: true
            visible: statusLabel.text.length > 0
            padding: 8

            Accessible.role: Accessible.StatusBar
            Accessible.name: qsTr("Document status")

            Label {
                id: statusLabel
                width: parent.width
                wrapMode: Text.WordWrap
                text: {
                    if (!host) {
                        return ""
                    }
                    switch (host.documentState) {
                    case "empty":
                        return qsTr("No document open. Use File → Open or drop a PDF path on the command line.")
                    case "opening":
                        return qsTr("Opening document…")
                    case "closing":
                        return qsTr("Closing document…")
                    case "error":
                        return host.typedError.length > 0
                                ? qsTr("Could not open the document (%1).").arg(host.typedError)
                                : qsTr("Could not open the document.")
                    case "ready":
                        if (host.cancelled) {
                            return qsTr("The last operation was cancelled.")
                        }
                        if (host.incomplete) {
                            return qsTr("Document loaded with incomplete support (some features were not honoured).")
                        }
                        if (host.unsupported) {
                            return qsTr("Document requires capabilities this build does not support.")
                        }
                        return ""
                    default:
                        return ""
                    }
                }
            }
        }

        Workspace {
            Layout.fillWidth: true
            Layout.fillHeight: true
            host: window.host
        }

        Pane {
            Layout.fillWidth: true
            padding: 6

            Accessible.role: Accessible.StatusBar
            Accessible.name: qsTr("Shell status")

            RowLayout {
                anchors.fill: parent
                spacing: 16

                Label {
                    text: host
                          ? qsTr("Document: %1").arg(host.documentShellStatus)
                          : qsTr("Document: NO_DOCUMENT")
                    Accessible.name: qsTr("Document status")
                }

                Label {
                    text: host
                          ? qsTr("Production: %1").arg(host.productionStateName)
                          : qsTr("Production: NOT_READY")
                    Accessible.name: qsTr("Production status")
                }

                Label {
                    text: host
                          ? qsTr("Preflight: %1").arg(host.preflightStateName)
                          : qsTr("Preflight: not-checked")
                    Accessible.name: qsTr("Preflight status")
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: host && host.hasDocument
                    text: qsTr("Page %1 / %2").arg(host.currentPage + 1).arg(host.pageCount)
                    Accessible.name: qsTr("Current page")
                }

                Label {
                    visible: host && host.hasDocument
                    text: qsTr("Zoom %1%").arg(Math.round(host.zoom * 100))
                    Accessible.name: qsTr("Current zoom")
                }

                Label {
                    visible: host && host.hasDocument && host.rotationDegrees !== 0
                    text: qsTr("Rotation %1°").arg(host.rotationDegrees)
                    Accessible.name: qsTr("Current rotation")
                }

                Label {
                    visible: host && host.documentState === "opening"
                    text: qsTr("Opening…")
                }
            }
        }
    }
}
