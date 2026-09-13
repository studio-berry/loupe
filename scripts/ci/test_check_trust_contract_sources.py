#!/usr/bin/env python3
"""Unit tests for the semantic-trust source audit."""

from __future__ import annotations

import unittest
from pathlib import Path

from check_trust_contract_sources import REQUIRED_MARKERS, relative


CONTROLLER = "LoopLibInteraction/sources/preflightcontroller.cpp"


class TrustContractSourceTest(unittest.TestCase):
    def test_controller_is_a_required_reducer_surface_not_an_exception(self) -> None:
        self.assertIn(CONTROLLER, REQUIRED_MARKERS)
        self.assertIn("reducePreflightVerdict", REQUIRED_MARKERS[CONTROLLER])

    def test_relative_paths_are_posix_paths(self) -> None:
        self.assertEqual(
            relative(Path(__file__).parents[2] / "PdfTool" / "pdftoolpreflight.cpp"),
            "PdfTool/pdftoolpreflight.cpp",
        )


if __name__ == "__main__":
    unittest.main()
