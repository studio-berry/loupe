#!/usr/bin/env python3
"""Unit tests for the GUI preflight-truth guard.

Every rule gets a planted-violation case (a snippet that must be reported) and
an allowed case (a Core-owned surface that must not be). The snippets are plain
strings rather than edits to real GUI files: the guard is run over the tree, so
planting a violation in the tree would make the guard fail its own repository.

The evasion cases are the heart of the suite. Each expression the guard was
falsified on -- a findings-model row count, an aggregate under a stats
namespace, `findings.length`, a backtick template verdict, and mixed-case
verdict copy -- is pinned here by its exact text, so the pattern cannot silently
regress to the shape that missed it.

The guard is deny-only: `RULES` is the whole policy and there is no allow-list
or suppression to launder a forbidden pattern past it.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.ci import check_preflight_truth_source as guard  # noqa: E402
from scripts.ci.check_preflight_truth_source import (  # noqa: E402
    RULES,
    gui_sources,
    in_scope,
    scan_text,
    scan_tree,
)


PREFLIGHT_PANE = "ProductQuickAccessibilitySmoke/qml/PreflightPane.qml"
MAIN_QML = "LoopEditor/qml/Main.qml"
EDITOR_HOST = "LoopEditor/editorhost.cpp"
SMOKE_MAIN = "ProductQuickAccessibilitySmoke/main.cpp"

# Every file the guard is expected to scan in this tree. The guard discovers
# them (`*.qml` under the two QML roots, plus the two host files), so adding a
# pane changes the discovered set automatically and fails the exact-set test
# below on purpose: the new pane gets reviewed against the rules instead of
# sliding into scope unnoticed.
EXPECTED_GUI_SOURCES = frozenset(
    {
        "LoopEditor/editorhost.cpp",
        "LoopEditor/qml/CanvasPane.qml",
        "LoopEditor/qml/DocumentPane.qml",
        "LoopEditor/qml/InspectorPane.qml",
        "LoopEditor/qml/Main.qml",
        "LoopEditor/qml/MenuModel.qml",
        "LoopEditor/qml/PreflightPane.qml",
        "LoopEditor/qml/ShellMenuBar.qml",
        "LoopEditor/qml/ShellToolBar.qml",
        "LoopEditor/qml/Workspace.qml",
        "LoopEditor/qml/WorkspacePlaceholderPane.qml",
        "ProductQuickAccessibilitySmoke/main.cpp",
        "ProductQuickAccessibilitySmoke/qml/CanvasPane.qml",
        "ProductQuickAccessibilitySmoke/qml/DocumentPane.qml",
        "ProductQuickAccessibilitySmoke/qml/InspectorPane.qml",
        "ProductQuickAccessibilitySmoke/qml/Main.qml",
        "ProductQuickAccessibilitySmoke/qml/MenuModel.qml",
        "ProductQuickAccessibilitySmoke/qml/PreflightPane.qml",
        "ProductQuickAccessibilitySmoke/qml/ShellMenuBar.qml",
        "ProductQuickAccessibilitySmoke/qml/ShellToolBar.qml",
        "ProductQuickAccessibilitySmoke/qml/Workspace.qml",
        "ProductQuickAccessibilitySmoke/qml/WorkspacePlaceholderPane.qml",
    }
)

# One representative usage per Core-owned surface the pane may render -- the
# Render-only half of the guard. Each must scan clean; the name of the surface
# is incidental, the usage is what the rules see.
CORE_OWNED_USAGE = {
    "preflightOperatorSummary": "text: root.host.preflightOperatorSummary",
    "preflightStateName": 'enabled: root.host.preflightStateName !== "running"',
    "preflightStateVisual": "property var stateVisual: host ? host.preflightStateVisual : ({})",
    "preflightStateColor": 'color: root.host ? root.host.preflightStateColor : "transparent"',
    "preflightProfiles": "model: root.host ? root.host.preflightProfiles : []",
    "preflightVariables": "model: root.host ? root.host.preflightVariables : []",
    "selectedPreflightProfileId": "if (model[i].id === root.host.selectedPreflightProfileId) return i",
    "hasPreflightReport": "enabled: root.host && root.host.hasPreflightReport",
    "runPreflight": "onClicked: if (root.host) root.host.runPreflight()",
    "cancelPreflight": "onClicked: if (root.host) root.host.cancelPreflight()",
    "selectPreflightProfile": "onActivated: if (root.host) root.host.selectPreflightProfile(currentValue)",
    "setPreflightVariable": "onEditingFinished: root.host.setPreflightVariable(model.name, text)",
    "requestPreflightReportExport": "onClicked: if (root.host) root.host.requestPreflightReportExport()",
    "exportPreflightReportFileUrl": "onAccepted: if (host) host.exportPreflightReportFileUrl(selectedFile)",
    "findingsModel": "model: root.findingsModel",
    "progress": "value: root.host ? root.host.preflight.progress : 0",
    "selection": "host.selectFinding(findingsModel.findingIdAt(currentIndex))",
}


class PreflightTruthSourceTest(unittest.TestCase):
    # -- rule: the GUI derives no completeness ---------------------------------
    def test_flags_inspection_complete_in_qml(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, "visible: preflight.inspectionComplete")
        self.assertEqual([violation.rule for violation in violations], ["gui-derives-completeness"])

    def test_flags_inspection_complete_in_the_editorhost_presentation_layer(self) -> None:
        violations = scan_text(EDITOR_HOST, "const bool pass = !result.inspectionComplete;")
        self.assertEqual([violation.rule for violation in violations], ["gui-derives-completeness"])

    def test_flags_inspection_complete_before_a_trailing_comment(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, "visible: preflight.inspectionComplete // Core's flag")
        self.assertEqual([violation.rule for violation in violations], ["gui-derives-completeness"])

    def test_does_not_flag_inspection_complete_inside_a_line_comment(self) -> None:
        self.assertEqual(scan_text(PREFLIGHT_PANE, "// never read inspectionComplete here"), [])

    def test_does_not_flag_inspection_complete_inside_a_block_comment(self) -> None:
        text = "/*\n * inspectionComplete is Core's; the pane must not read it\n */\n"
        self.assertEqual(scan_text(PREFLIGHT_PANE, text), [])

    def test_does_not_treat_a_quoted_inspection_complete_as_a_read(self) -> None:
        self.assertEqual(scan_text(PREFLIGHT_PANE, 'text: qsTr("inspectionComplete")'), [])

    # -- rule: the GUI aggregates no findings ----------------------------------
    def test_flags_a_verdict_derived_from_the_error_count(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, 'text: result.errors.length === 0 ? "PASS" : "FAIL"')
        self.assertIn("gui-aggregates-findings", {violation.rule for violation in violations})

    def test_flags_errors_is_empty_in_qml(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, "if (result.errors.isEmpty()) status = ok")
        self.assertEqual([violation.rule for violation in violations], ["gui-aggregates-findings"])

    def test_flags_warning_count_aggregation(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, "enabled: warnings.length > 0")
        self.assertEqual([violation.rule for violation in violations], ["gui-aggregates-findings"])

    def test_flags_a_verdict_from_the_findings_model_row_count(self) -> None:
        expression = 'text: host.preflight.findingsModel.rowCount === 0 ? qsTr("Passed") : qsTr("Failed")'
        violations = scan_text(PREFLIGHT_PANE, expression)
        self.assertEqual(
            sorted({violation.rule for violation in violations}),
            ["gui-aggregates-findings", "gui-derives-pass-fail-copy"],
        )

    def test_flags_a_cpp_findings_model_row_count(self) -> None:
        violations = scan_text(EDITOR_HOST, "const bool pass = m_preflight.findingsModel()->rowCount() == 0;")
        self.assertEqual([violation.rule for violation in violations], ["gui-aggregates-findings"])

    def test_flags_an_aggregate_under_a_stats_namespace(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, 'color: result.stats.errorCount > 0 ? "red" : "green"')
        self.assertEqual([violation.rule for violation in violations], ["gui-aggregates-findings"])

    def test_flags_findings_length_aggregation(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, "visible: host.preflight.findings.length > 0")
        self.assertEqual([violation.rule for violation in violations], ["gui-aggregates-findings"])

    def test_flags_an_aggregate_inside_a_template_interpolation(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, "text: `Findings: ${result.errors.length}`")
        self.assertEqual([violation.rule for violation in violations], ["gui-aggregates-findings"])

    # -- rule: the GUI writes no pass/fail copy --------------------------------
    def test_flags_a_pass_fail_literal_without_any_aggregation(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, 'text: result.ok ? qsTr("PASS") : qsTr("FAIL")')
        self.assertEqual([violation.rule for violation in violations], ["gui-derives-pass-fail-copy"])

    def test_flags_a_backtick_template_verdict_literal(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, "text: result.ok ? `PASS` : `FAIL`")
        self.assertEqual([violation.rule for violation in violations], ["gui-derives-pass-fail-copy"])

    def test_flags_mixed_case_verdict_copy(self) -> None:
        for expression in (
            'text: host.preflight.ok ? qsTr("Passed") : qsTr("Failed")',
            'text: qsTr("Passed")',
            'Accessible.name: qsTr("Failed")',
        ):
            with self.subTest(expression=expression):
                violations = scan_text(PREFLIGHT_PANE, expression)
                self.assertEqual([violation.rule for violation in violations], ["gui-derives-pass-fail-copy"])

    def test_flags_verdict_copy_before_a_trailing_comment(self) -> None:
        violations = scan_text(PREFLIGHT_PANE, 'text: qsTr("Failed") // Core wording')
        self.assertEqual([violation.rule for violation in violations], ["gui-derives-pass-fail-copy"])

    def test_does_not_flag_verdict_words_inside_a_comment(self) -> None:
        self.assertEqual(scan_text(PREFLIGHT_PANE, '// the badge renders "PASS" once Core says so'), [])

    def test_reports_the_path_and_line_of_each_violation(self) -> None:
        violations = scan_text(MAIN_QML, "import QtQuick\n\nvar pass = result.inspectionComplete\n")
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 3)
        self.assertTrue(violations[0].format().startswith(f"{MAIN_QML}:3: gui-derives-completeness"))

    # -- allowed: the Core-owned surfaces --------------------------------------
    def test_every_core_owned_surface_renders_without_a_violation(self) -> None:
        for surface, usage in CORE_OWNED_USAGE.items():
            with self.subTest(surface=surface):
                self.assertEqual(scan_text(PREFLIGHT_PANE, usage), [])

    def test_allows_finding_lookups_that_are_not_counts(self) -> None:
        for usage in (
            "model: root.findingsModel",
            "width: findingsView.width",
            "host.selectFinding(model.findingId)",
            "host.selectFinding(findingsModel.findingIdAt(currentIndex))",
        ):
            with self.subTest(usage=usage):
                self.assertEqual(scan_text(PREFLIGHT_PANE, usage), [])

    def test_allows_counts_that_are_not_finding_severity(self) -> None:
        for usage in (
            'text: qsTr("Page %1 / %2").arg(host.currentPage + 1).arg(host.pageCount)',
            "const bool hasSearchResults = ready && m_documentModel.searchResultCount() > 0;",
            "const int count = m_documentModel.searchResults()->rowCount();",
            "if (modelData.description.length > 0) text = modelData.description",
        ):
            with self.subTest(usage=usage):
                self.assertEqual(scan_text(EDITOR_HOST, usage), [])

    def test_allows_lower_case_state_names_and_harness_status(self) -> None:
        for path, usage in (
            (EDITOR_HOST, 'return QStringLiteral("pass");'),
            (EDITOR_HOST, "case pdfinteraction::PreflightController::State::Failed:"),
            (SMOKE_MAIN, 'fprintf(stdout, "product-quick-a11y-smoke status=%s\\n", passed ? "pass" : "fail");'),
        ):
            with self.subTest(usage=usage):
                self.assertEqual(scan_text(path, usage), [])

    def test_allows_reading_core_worded_prose_in_editorhost(self) -> None:
        text = "QString EditorHost::preflightOperatorSummary() const\n{\n    return m_preflight.operatorSummary();\n}\n"
        self.assertEqual(scan_text(EDITOR_HOST, text), [])

    # -- scope -----------------------------------------------------------------
    def test_scope_covers_the_gui_layers_only(self) -> None:
        self.assertTrue(in_scope(PREFLIGHT_PANE))
        self.assertTrue(in_scope(MAIN_QML))
        self.assertTrue(in_scope(SMOKE_MAIN))
        self.assertTrue(in_scope(EDITOR_HOST))
        self.assertFalse(in_scope("LoopLibCore/sources/pdfpreflightverdict.cpp"))
        self.assertFalse(in_scope("LoopLibInteraction/sources/preflightcontroller.cpp"))
        self.assertFalse(in_scope("PdfTool/pdftoolpreflight.cpp"))
        self.assertFalse(in_scope("UnitTests/tst_editorhosttest.cpp"))
        self.assertFalse(in_scope("docs/LOOP_SHELL_CONTRACT.md"))

    def test_scope_file_list_is_exactly_the_expected_set(self) -> None:
        """The discovered file list equals the expected names, not a count.

        `gui_sources()` globs `*.qml` under `ProductQuickAccessibilitySmoke/qml`
        and `LoopEditor/qml` and adds `ProductQuickAccessibilitySmoke/main.cpp`
        and `LoopEditor/editorhost.cpp`. A new pane under either root is picked
        up by discovery, which makes this assertion fail until the name is added
        to `EXPECTED_GUI_SOURCES` -- deliberate, so the new pane is placed in
        scope by a reviewer rather than arriving unnoticed, and the guard's CI
        line moves from 22 files to 23.
        """
        self.assertEqual(set(gui_sources()), set(EXPECTED_GUI_SOURCES))

    # -- the guard itself ------------------------------------------------------
    def test_the_real_tree_computes_no_preflight_truth(self) -> None:
        self.assertEqual([violation.format() for violation in scan_tree()], [])

    def test_every_rule_documents_why_it_exists(self) -> None:
        self.assertEqual(len({rule.id for rule in RULES}), len(RULES))
        for rule in RULES:
            with self.subTest(rule=rule.id):
                self.assertTrue(rule.detail.strip())

    def test_every_rule_declares_a_known_scan_view(self) -> None:
        for rule in RULES:
            with self.subTest(rule=rule.id):
                self.assertIn(rule.view, {"code", "uncommented"})

    def test_guard_is_deny_only_and_exposes_no_allow_list(self) -> None:
        """The rules are the whole policy; no allow-list can launder a pattern."""
        self.assertEqual([name for name in vars(guard) if "ALLOW" in name.upper()], [])
        self.assertEqual(
            {rule.id for rule in RULES},
            {"gui-derives-completeness", "gui-aggregates-findings", "gui-derives-pass-fail-copy"},
        )

    def test_no_forbidden_pattern_is_laundered_by_any_surface(self) -> None:
        cases = (
            ("result.errors.length > 0", "gui-aggregates-findings"),
            ("warnings.length > 0", "gui-aggregates-findings"),
            ("result.errors.isEmpty()", "gui-aggregates-findings"),
            ("host.preflight.inspectionComplete", "gui-derives-completeness"),
            ('text: qsTr("PASS")', "gui-derives-pass-fail-copy"),
        )
        for usage, rule in cases:
            with self.subTest(usage=usage):
                self.assertIn(rule, {violation.rule for violation in scan_text(PREFLIGHT_PANE, usage)})


if __name__ == "__main__":
    unittest.main()
