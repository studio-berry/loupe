#!/usr/bin/env python3
"""Focused unit tests for check-change policy logic."""

from __future__ import annotations

import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("check-change.py")
SPEC = importlib.util.spec_from_file_location("check_change", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

POLICY_BRANCHES = {
    "default": "stable",
    "integration": "dev",
    "qualification": "unstable",
    "release": "stable",
    "protected": ["unstable", "stable"],
}


class CheckChangeTests(unittest.TestCase):
    def test_branch_slug_is_stable_and_safe(self) -> None:
        self.assertEqual(MODULE.branch_slug("feature/PDF bleed"), "feature-PDF-bleed")
        self.assertEqual(MODULE.branch_slug("..."), "change")

    def test_parse_name_status_handles_renames(self) -> None:
        raw = b"R100\0old/file.cpp\0new/file.cpp\0A\0changes/feature-x.md\0"
        changes = MODULE.parse_name_status(raw)
        self.assertEqual(changes[0].status, "R")
        self.assertEqual(changes[0].old_path, "old/file.cpp")
        self.assertEqual(changes[0].path, "new/file.cpp")
        self.assertEqual(changes[1].status, "A")

    def test_changelog_requires_all_fields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "entry.md"
            path.write_text("Category: fixed\nSummary: missing audience\n", encoding="utf-8")
            valid, reason = MODULE.parse_changelog(path, {"fixed"})
        self.assertFalse(valid)
        self.assertIn("Audience", reason)

    def test_changelog_accepts_internal_entry(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "entry.md"
            path.write_text(
                "# Agent policy\nCategory: internal\nAudience: developers\nBreaking-Change: no\nSummary: Add a verification gate.\n",
                encoding="utf-8",
            )
            valid, reason = MODULE.parse_changelog(path, {"internal"})
        self.assertTrue(valid, reason)

    def test_resolve_merge_base_uses_both_revisions(self) -> None:
        with patch.object(MODULE, "run_git", return_value="abc123\n") as run_git:
            self.assertEqual(MODULE.resolve_merge_base("base", "head"), "abc123")
        run_git.assert_called_once_with(["merge-base", "base", "head"])

    def test_skip_changelog_on_integration_branch(self) -> None:
        policy = {"branches": POLICY_BRANCHES}
        with patch.dict(os.environ, {"GITHUB_EVENT_NAME": ""}, clear=False):
            self.assertEqual(MODULE.skip_changelog_reason("dev", policy, False), "integration branch")
            self.assertEqual(MODULE.skip_changelog_reason("unstable", policy, False), "integration branch")
            self.assertEqual(MODULE.skip_changelog_reason("stable", policy, False), "integration branch")
            self.assertIsNone(MODULE.skip_changelog_reason("cdx/foo", policy, False))
            self.assertEqual(MODULE.skip_changelog_reason("cdx/foo", policy, True), "non-PR event")

    def test_skip_changelog_on_non_pr_github_event(self) -> None:
        policy = {"branches": POLICY_BRANCHES}
        with patch.dict(os.environ, {"GITHUB_EVENT_NAME": "push"}, clear=False):
            self.assertEqual(MODULE.skip_changelog_reason("cdx/foo", policy, False), "non-PR event")
        with patch.dict(os.environ, {"GITHUB_EVENT_NAME": "pull_request"}, clear=False):
            self.assertIsNone(MODULE.skip_changelog_reason("cdx/foo", policy, False))

    def test_check_changelog_demands_dev_fragment_when_branch_is_dev(self) -> None:
        policy = {"changelog": {"directory": "changes", "categories": ["fixed"]}}
        changes = [MODULE.Change("A", "changes/cursor-foo.md")]
        evidence = MODULE.check_changelog(changes, policy, "dev")
        self.assertEqual(evidence.result, "fail")
        self.assertIn("changes/dev.md", evidence.reason or "")

    def test_check_changelog_allows_stacked_topic_fragments(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            changes_dir = root / "changes"
            changes_dir.mkdir()
            body = (
                "Category: fixed\nAudience: developers\nBreaking-Change: no\n"
                "Summary: Wave fragment.\n"
            )
            for name in ("cdx-parent.md", "cdx-child.md"):
                (changes_dir / name).write_text(body, encoding="utf-8")
            policy = {"changelog": {"directory": "changes", "categories": ["fixed"]}}
            changes = [
                MODULE.Change("A", "changes/cdx-parent.md"),
                MODULE.Change("A", "changes/cdx-child.md"),
            ]
            with patch.object(MODULE, "ROOT", root):
                evidence = MODULE.check_changelog(changes, policy, "cdx/child")
        self.assertEqual(evidence.result, "pass")

    def test_check_changelog_allows_modified_topic_fragment(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            changes_dir = root / "changes"
            changes_dir.mkdir()
            (changes_dir / "cdx-session-05-widgets-libraries.md").write_text(
                "Category: changed\nAudience: developers\nBreaking-Change: yes\n"
                "Summary: Updated Session 05 fragment.\n",
                encoding="utf-8",
            )
            policy = {"changelog": {"directory": "changes", "categories": ["changed"]}}
            changes = [MODULE.Change("M", "changes/cdx-session-05-widgets-libraries.md")]
            with patch.object(MODULE, "ROOT", root):
                evidence = MODULE.check_changelog(changes, policy, "cdx/session-05-widgets-libraries")
        self.assertEqual(evidence.result, "pass")

    def test_check_changelog_validates_expected_fragment_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            changes_dir = root / "changes"
            changes_dir.mkdir()
            (changes_dir / "cdx-parent.md").write_text("Category: fixed\n", encoding="utf-8")
            (changes_dir / "cdx-child.md").write_text(
                "Category: fixed\nAudience: developers\nBreaking-Change: no\nSummary: Child.\n",
                encoding="utf-8",
            )
            policy = {"changelog": {"directory": "changes", "categories": ["fixed"]}}
            changes = [
                MODULE.Change("A", "changes/cdx-parent.md"),
                MODULE.Change("A", "changes/cdx-child.md"),
            ]
            with patch.object(MODULE, "ROOT", root):
                evidence = MODULE.check_changelog(changes, policy, "cdx/child")
        self.assertEqual(evidence.result, "pass")

    def test_changelog_evidence_still_required_on_topic_branch(self) -> None:
        policy = {
            "branches": POLICY_BRANCHES,
            "changelog": {"directory": "changes", "categories": ["fixed"]},
        }
        with patch.dict(os.environ, {"GITHUB_EVENT_NAME": "pull_request"}, clear=False):
            evidence = MODULE.changelog_evidence(
                [MODULE.Change("A", "changes/other.md")],
                policy,
                "cursor/foo-0158",
                skip=False,
            )
        self.assertEqual(evidence.result, "fail")
        self.assertIn("changes/cursor-foo-0158.md", evidence.reason or "")

    def test_changelog_evidence_skips_topic_fragment_after_merge_to_dev(self) -> None:
        policy = {
            "branches": POLICY_BRANCHES,
            "changelog": {"directory": "changes", "categories": ["fixed"]},
        }
        changes = [MODULE.Change("A", "changes/cursor-foo.md")]
        with patch.dict(os.environ, {"GITHUB_EVENT_NAME": ""}, clear=False):
            evidence = MODULE.changelog_evidence(changes, policy, "dev", skip=False)
        self.assertEqual(evidence.result, "not-applicable")
        self.assertEqual(evidence.reason, "integration branch")

    def test_parse_name_status_deleted_is_excluded_from_format(self) -> None:
        raw = b"D\0LoopLibCore/sources/gone.cpp\0M\0LoopLibCore/sources/keep.cpp\0"
        changes = MODULE.parse_name_status(raw)
        self.assertEqual(changes[0].status, "D")
        self.assertEqual(MODULE.format_sources(changes), ["LoopLibCore/sources/keep.cpp"])

    def test_format_sources_excludes_deleted_paths(self) -> None:
        changes = [
            MODULE.Change("D", "LoopLibCore/sources/old.cpp"),
            MODULE.Change("M", "LoopLibCore/sources/keep.cpp"),
            MODULE.Change("A", "LoopLibCore/sources/new.h"),
            MODULE.Change("R", "LoopLibCore/sources/renamed.cpp", "LoopLibCore/sources/was.cpp"),
            MODULE.Change("A", "changes/foo.md"),
        ]
        self.assertEqual(
            MODULE.format_sources(changes),
            [
                "LoopLibCore/sources/keep.cpp",
                "LoopLibCore/sources/new.h",
                "LoopLibCore/sources/renamed.cpp",
            ],
        )

    def test_clang_tidy_is_scheduled_after_test_targets_build(self) -> None:
        source = SCRIPT.read_text(encoding="utf-8")
        main_source = source[source.index("def main()") :]
        self.assertLess(
            main_source.index("for test in tests:"),
            main_source.index("add_clang_tidy_checks("),
        )

    def test_dry_run_source_checks_do_not_probe_prerequisites(self) -> None:
        evidence = []
        build_dir = Path("missing-build-directory")
        with patch.object(MODULE.shutil, "which", side_effect=AssertionError("probed tool")):
            MODULE.add_format_checks(
                evidence,
                ["UnitTests/tst_example.cpp"],
                dry_run=True,
                fix_format=False,
            )
            MODULE.add_clang_tidy_checks(
                evidence,
                ["UnitTests/tst_example.cpp"],
                build_dir,
                dry_run=True,
            )
        self.assertEqual([item.result for item in evidence], ["not-run", "not-run"])
        self.assertEqual([item.reason for item in evidence], ["dry-run", "dry-run"])

    def test_real_source_checks_keep_missing_prerequisites_incomplete(self) -> None:
        evidence = []
        with patch.object(MODULE.shutil, "which", return_value=None):
            MODULE.add_format_checks(
                evidence,
                ["UnitTests/tst_example.cpp"],
                dry_run=False,
                fix_format=False,
            )
            MODULE.add_clang_tidy_checks(
                evidence,
                ["UnitTests/tst_example.cpp"],
                Path("missing-build-directory"),
                dry_run=False,
            )
        self.assertEqual([item.result for item in evidence], ["incomplete", "incomplete"])

    def test_clang_tidy_falls_back_to_the_unversioned_binary(self) -> None:
        """A machine carrying only `clang-tidy` (PyPI wheel / LLVM installer) must still run the lane."""
        captured: list[list[str]] = []

        def record(evidence, name, command, cwd, dry_run):
            captured.append(command)

        with tempfile.TemporaryDirectory() as directory:
            build_dir = Path(directory)
            (build_dir / "compile_commands.json").write_text("[]", encoding="utf-8")
            with patch.object(MODULE, "add_result", side_effect=record), patch.object(
                MODULE.shutil,
                "which",
                side_effect=lambda name: "/opt/bin/clang-tidy" if name == "clang-tidy" else None,
            ):
                MODULE.add_clang_tidy_checks(
                    [],
                    ["LoopLibCore/sources/example.cpp"],
                    build_dir,
                    dry_run=False,
                )

        self.assertEqual(len(captured), 1)
        self.assertEqual(captured[0][0], "/opt/bin/clang-tidy")
        self.assertEqual(captured[0][-1], "LoopLibCore/sources/example.cpp")

    def test_clang_tidy_skips_manual_moc_includes(self) -> None:
        self.assertEqual(
            MODULE.clang_tidy_sources(
                [
                    "LoopLibCore/sources/example.cpp",
                    "UnitTests/tst_budgetexhaustiontest.cpp",
                ]
            ),
            ["LoopLibCore/sources/example.cpp"],
        )

    def test_classify_still_uses_deleted_paths(self) -> None:
        policy = {
            "module_boundaries": {
                "core": {"paths": ["LoopLibCore/**"], "targets": ["LoopLibCore"], "tests": []},
            }
        }
        changes = [MODULE.Change("D", "LoopLibCore/sources/gone.cpp")]
        self.assertEqual(MODULE.classify(changes, policy), ["core"])


if __name__ == "__main__":
    unittest.main()
