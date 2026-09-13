import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root

    objectName: "preflightPane"

    property var host: editorHost
    property var findingsModel: host ? host.preflight.findingsModel : null

    padding: 8

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Preflight findings")
    Accessible.description: qsTr("Revision-bound preflight findings. Select a finding to inspect evidence on the canvas.")

    // Token-name -> glyph for the canonical non-colour icon. The name is LoopLibQuick's
    // stateIconName(), delivered through EditorHost as preflightStateVisual.icon; each canonical
    // name maps 1:1 to exactly one glyph so the kinds the badge can reach (Checkmark,
    // FilledCircle, FilledSquare, Hatched, Outline) stay visually distinct by shape wherever their
    // colour tokens collide - in HighContrast, StateIncomplete and StateNotChecked are both white
    // (#194). The mapping is presentation only and decides no state semantics: the colour and the
    // accessible name remain Core's.
    readonly property var preflightIconGlyphs: ({
        "Checkmark": "\u2713",
        "FilledCircle": "\u25CF",
        "FilledSquare": "\u25A0",
        "FilledTriangle": "\u25B2",
        "Hatched": "\u25A8",
        "Outline": "\u25CB",
        "BadgeOverlay": "\u25C6"
    })

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // Canonical state badge. The colour, the icon and the accessible name are rendered from
        // LoopLibQuick through EditorHost (preflightStateColor / preflightStateVisual); this file
        // decides nothing about pass or severity, and invents no state wording of its own. The icon
        // is preflightIconGlyphs[preflightStateVisual.icon], that token-name -> glyph lookup, so the
        // kinds stay distinct by shape where their colour tokens collide (HighContrast's white
        // StateIncomplete and StateNotChecked, #194).
        RowLayout {
            id: stateBadge
            Layout.fillWidth: true
            spacing: 8

            Label {
                id: stateBadgeIcon
                Layout.alignment: Qt.AlignTop
                font.pixelSize: 14
                color: root.host ? root.host.preflightStateColor : "transparent"
                text: root.host ? (root.preflightIconGlyphs[root.host.preflightStateVisual.icon] || "") : ""
                Accessible.ignored: true
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: {
                    if (!host)
                        return ""
                    if (host.preflightOperatorSummary)
                        return host.preflightOperatorSummary
                    return qsTr("Preflight status: %1").arg(host.preflightStateName)
                }
                Accessible.name: root.host ? root.host.preflightStateVisual.accessibleName : qsTr("Preflight status")
            }
        }

        ComboBox {
            id: profileSelector
            Layout.fillWidth: true
            model: root.host ? root.host.preflightProfiles : []
            textRole: "name"
            valueRole: "id"
            enabled: root.host && root.host.preflightStateName !== "running"
            currentIndex: {
                if (!root.host)
                    return -1
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === root.host.selectedPreflightProfileId)
                        return i
                }
                return -1
            }
            Accessible.name: qsTr("Preflight profile")
            Accessible.description: qsTr("Select a bundled or local validated preflight profile.")
            onActivated: function(index) {
                if (root.host && model[index])
                    root.host.selectPreflightProfile(model[index].id)
            }
        }

        Repeater {
            model: root.host ? root.host.preflightVariables : []
            delegate: RowLayout {
                Layout.fillWidth: true
                required property var modelData

                Label {
                    text: modelData.required ? qsTr("%1 (required)").arg(modelData.name) : modelData.name
                    Accessible.name: modelData.description.length > 0 ? modelData.description : text
                }
                CheckBox {
                    visible: modelData.type === "boolean"
                    checked: Boolean(modelData.value)
                    text: modelData.description
                    onToggled: if (root.host) root.host.setPreflightVariable(modelData.name, checked)
                }
                TextField {
                    Layout.fillWidth: true
                    visible: modelData.type !== "boolean"
                    text: modelData.value === undefined || modelData.value === null ? "" : String(modelData.value)
                    inputMethodHints: modelData.type === "string" ? Qt.ImhNone : Qt.ImhFormattedNumbersOnly
                    Accessible.name: qsTr("Preflight variable %1").arg(modelData.name)
                    onEditingFinished: if (root.host) root.host.setPreflightVariable(modelData.name, text)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: qsTr("Run Preflight")
                enabled: root.host && root.host.hasDocument && root.host.preflightStateName !== "running"
                Accessible.name: qsTr("Run preflight")
                Accessible.description: qsTr("Runs the selected validated preflight profile.")
                onClicked: root.host.runPreflight()
            }

            Button {
                text: qsTr("Cancel")
                enabled: root.host && root.host.preflightStateName === "running"
                Accessible.name: qsTr("Cancel preflight")
                Accessible.description: qsTr("Cancels the running preflight job.")
                onClicked: root.host.cancelPreflight()
            }

            Button {
                text: qsTr("Export Report")
                enabled: root.host && root.host.hasPreflightReport
                Accessible.name: qsTr("Export preflight report")
                Accessible.description: qsTr("Exports the retained normalized preflight report as JSON.")
                onClicked: root.host.requestPreflightReportExport()
            }

            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: root.host ? root.host.preflight.progress : 0
                enabled: root.host && root.host.preflightStateName === "running"
                Accessible.name: qsTr("Preflight progress")
            }
        }

        ListView {
            id: findingsView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            activeFocusOnTab: true
            model: root.findingsModel
            currentIndex: -1

            Accessible.name: qsTr("Preflight findings list")
            Accessible.description: qsTr("Use arrow keys to move between findings and Enter to navigate to evidence.")

            delegate: ItemDelegate {
                width: findingsView.width
                text: "%1 — %2".arg(model.severity).arg(model.message)
                highlighted: model.selected
                Accessible.name: model.message
                Accessible.description: qsTr("Severity %1, scope %2, page %3, object %4, check %5, evidence %6")
                    .arg(model.severity).arg(model.scope).arg(model.page).arg(model.objectId).arg(model.checkId).arg(model.evidenceIds.join(", "))
                onClicked: {
                    findingsView.currentIndex = index
                    if (host) {
                        host.selectFinding(model.findingId)
                    }
                }
            }

            Keys.onReturnPressed: {
                if (currentIndex >= 0 && host && findingsModel) {
                    host.selectFinding(findingsModel.findingIdAt(currentIndex))
                }
            }
        }
    }
}
