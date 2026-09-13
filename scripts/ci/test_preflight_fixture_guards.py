#!/usr/bin/env python3
"""Regression checks for the preflight corpus and bleed-stress fixture guards."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "UnitTests" / "tst_preflightcorpus.cpp"
STRESS = ROOT / "UnitTests" / "tst_bleedstresstest.cpp"


class PreflightFixtureGuardTest(unittest.TestCase):
    def test_tracked_corpus_rows_fail_closed(self) -> None:
        source = CORPUS.read_text(encoding="utf-8")

        self.assertNotIn("QSKIP", source)
        self.assertEqual(source.count("QFAIL(s_pendingHint)"), 2)
        self.assertEqual(source.count("QFAIL(s_regenerateHint)"), 2)

    def test_generated_stress_rows_have_an_explicit_required_mode(self) -> None:
        source = STRESS.read_text(encoding="utf-8")

        self.assertEqual(source.count("requireStressFixtures()"), 3)
        self.assertEqual(source.count("QFAIL(qPrintable(message))"), 2)
        self.assertEqual(source.count("QSKIP(qPrintable(message))"), 2)
        self.assertIn("LOOP_REQUIRE_BLEED_STRESS_FIXTURES", source)


if __name__ == "__main__":
    unittest.main()
