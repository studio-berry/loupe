#!/usr/bin/env python3
"""Unit tests for the QML mirror-parity guard.

The pairs are fed in as in-memory bytes so every violation class - a drifted
twin, a source with no mirror, a mirror with no source - can be planted without
touching the tree the guard is run over. The mismatch this guard was written
for is real and is described in check_qml_mirror_parity.py.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.ci.check_qml_mirror_parity import (  # noqa: E402
    MIRROR_ROOT,
    SOURCE_ROOT,
    compare_mirrors,
    mirror_texts,
    source_texts,
)


class QmlMirrorParityTest(unittest.TestCase):
    # -- rule: byte-different twin ---------------------------------------------
    def test_flags_a_drifted_pair(self) -> None:
        violations = compare_mirrors({"PreflightPane.qml": b"a\nb\n"}, {"PreflightPane.qml": b"a\n"})
        self.assertEqual([violation.rule for violation in violations], ["mirror-drift"])
        self.assertEqual(violations[0].line, 2)
        self.assertEqual(
            violations[0].format(),
            "LoopEditor/qml/PreflightPane.qml:2: mirror-drift vs "
            "ProductQuickAccessibilitySmoke/qml/PreflightPane.qml (source 2 lines, mirror 1 lines)",
        )

    def test_flags_a_mirror_that_only_lost_its_line_endings(self) -> None:
        violations = compare_mirrors({"Main.qml": b"a\r\n"}, {"Main.qml": b"a\n"})
        self.assertEqual([violation.rule for violation in violations], ["mirror-drift"])

    # -- rule: one-sided file ---------------------------------------------------
    def test_flags_a_source_without_a_mirror(self) -> None:
        violations = compare_mirrors({"NewPane.qml": b"Item {}\n"}, {})
        self.assertEqual([violation.rule for violation in violations], ["mirror-missing"])
        self.assertEqual(
            violations[0].format(),
            "LoopEditor/qml/NewPane.qml:1: mirror-missing "
            "(no ProductQuickAccessibilitySmoke/qml/NewPane.qml twin)",
        )

    def test_flags_a_mirror_without_a_source(self) -> None:
        violations = compare_mirrors({}, {"RetiredPane.qml": b"Item {}\n"})
        self.assertEqual([violation.rule for violation in violations], ["mirror-orphan"])
        self.assertEqual(
            violations[0].format(),
            "ProductQuickAccessibilitySmoke/qml/RetiredPane.qml:1: mirror-orphan "
            "(no LoopEditor/qml/RetiredPane.qml source)",
        )

    # -- allowed: byte-identical pairs -----------------------------------------
    def test_allows_byte_identical_pairs(self) -> None:
        self.assertEqual(compare_mirrors({"Main.qml": b"Item {}\n"}, {"Main.qml": b"Item {}\n"}), [])

    def test_allows_many_identical_pairs(self) -> None:
        pairs = {f"Pane{index}.qml": b"Item {}\n" for index in range(10)}
        self.assertEqual(compare_mirrors(pairs, dict(pairs)), [])

    # -- reporting --------------------------------------------------------------
    def test_lists_every_mismatched_pair(self) -> None:
        violations = compare_mirrors(
            {"A.qml": b"a\n", "B.qml": b"b\nb\n", "C.qml": b"c\n"},
            {"A.qml": b"a\n", "B.qml": b"b\n", "D.qml": b"d\n"},
        )
        self.assertEqual(
            [violation.rule for violation in violations],
            ["mirror-drift", "mirror-missing", "mirror-orphan"],
        )

    def test_custom_roots_are_reported(self) -> None:
        violations = compare_mirrors(
            {"A.qml": b"a\n"},
            {"A.qml": b"b\n"},
            source_root="src/qml",
            mirror_root="mirror/qml",
        )
        self.assertTrue(violations[0].format().startswith("src/qml/A.qml:"))
        self.assertTrue(violations[0].format().endswith("vs mirror/qml/A.qml (source 1 lines, mirror 1 lines)"))

    # -- the real tree ----------------------------------------------------------
    def test_the_two_roots_are_the_documented_contract_ends(self) -> None:
        self.assertEqual(SOURCE_ROOT, "LoopEditor/qml")
        self.assertEqual(MIRROR_ROOT, "ProductQuickAccessibilitySmoke/qml")

    def test_both_roots_are_populated_with_the_same_names(self) -> None:
        sources = source_texts()
        mirrors = mirror_texts()
        self.assertGreaterEqual(len(sources), 10)
        self.assertEqual(sorted(sources), sorted(mirrors))


if __name__ == "__main__":
    unittest.main()
