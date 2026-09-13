"""Contract tests for OCR request normalization and bbox mapping."""

from __future__ import annotations

import json
import math
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "service"))

from engine import (  # noqa: E402
    MAX_IMAGE_BYTES,
    _read_staged_image,
    normalize_languages,
    pixel_bbox_to_pdf,
    validate_request,
)


class EngineContractTest(unittest.TestCase):
    def test_languages_are_normalized_and_deduplicated(self) -> None:
        self.assertEqual(normalize_languages([" EN ", "fr", "en", "FR"]), ["en", "fr"])
        self.assertEqual(normalize_languages(None), ["en"])

    def test_invalid_language_shape_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "languages must be an array"):
            normalize_languages("en")

    def test_language_codes_must_look_like_language_codes(self) -> None:
        # Codes are used to build model file names, so a traversal-shaped value
        # must be refused here rather than passed to easyocr.
        for code in ["../../etc", "en/../..", "e", "toolongcode", "en-US", ""]:
            with self.subTest(code=code):
                if not code.strip():
                    self.assertEqual(normalize_languages([code]), ["en"])
                    continue
                with self.assertRaises(ValueError):
                    normalize_languages([code])

        self.assertEqual(normalize_languages(["ch_sim", "EN"]), ["ch_sim", "en"])

    def test_request_schema_accepts_what_the_runtime_normalizes(self) -> None:
        # The schema is the published wire contract; the service normalizes case
        # before shape-checking. A value the runtime accepts must not be rejected
        # by the schema, and junk that the runtime refuses must still be refused.
        schema = json.loads(
            (Path(__file__).resolve().parents[1] / "schemas" / "ocr-sidecar.schema.json").read_text(
                encoding="utf-8"
            )
        )
        pattern = schema["oneOf"][0]["properties"]["languages"]["items"]["pattern"]

        for value in ["en", "EN", "ch_sim", "CH_SIM"]:
            with self.subTest(value=value):
                self.assertIsNotNone(re.match(pattern, value))
                self.assertEqual(normalize_languages([value]), [value.strip().lower()])

        for value in ["../../etc", "en-US", "e", "toolongcode"]:
            with self.subTest(value=value):
                self.assertIsNone(re.match(pattern, value))
                with self.assertRaises(ValueError):
                    normalize_languages([value])

    def test_staged_image_is_read_by_descriptor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "page-1.png")
            with open(path, "wb") as handle:
                handle.write(b"raster-bytes")

            self.assertEqual(_read_staged_image(path), b"raster-bytes")

            missing = os.path.join(directory, "absent.png")
            with self.assertRaises(FileNotFoundError):
                _read_staged_image(missing)

            with self.assertRaises((ValueError, OSError)):
                _read_staged_image(directory)

    @unittest.skipUnless(hasattr(os, "symlink") and hasattr(os, "O_NOFOLLOW"), "symlinks unavailable")
    def test_staged_image_refuses_a_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = os.path.join(directory, "target.png")
            with open(target, "wb") as handle:
                handle.write(b"raster-bytes")

            link = os.path.join(directory, "page-1.png")
            try:
                os.symlink(target, link)
            except (OSError, NotImplementedError):
                self.skipTest("symlink creation not permitted")

            with self.assertRaises(OSError):
                _read_staged_image(link)

    def test_staged_image_size_cap_is_sane(self) -> None:
        self.assertGreater(MAX_IMAGE_BYTES, 0)

    def test_request_limits_and_media_box_are_validated(self) -> None:
        normalized = validate_request(
            {
                "page": 1,
                "dpi": 300,
                "image": "page.png",
                "languages": ["EN"],
                "media_box": {"x": 10, "y": 20, "width": 600, "height": 800},
                "rotation": 90,
            }
        )
        self.assertEqual(normalized["languages"], ["en"])
        self.assertEqual(normalized["rotation"], 90)

        with self.assertRaisesRegex(ValueError, "dpi"):
            validate_request({"page": 1, "dpi": 1201, "image": "page.png"})

    def test_bbox_top_left_and_nonzero_origin(self) -> None:
        result = pixel_bbox_to_pdf(
            [[0, 0], [100, 0], [100, 100], [0, 100]],
            1000,
            1000,
            {"x": 10, "y": 20, "width": 600, "height": 800},
        )
        self.assertEqual(result, {"x": 10.0, "y": 740.0, "width": 60.0, "height": 80.0})

    def test_bbox_rotation_90(self) -> None:
        result = pixel_bbox_to_pdf(
            [[0, 0], [100, 0], [100, 100], [0, 100]],
            1000,
            800,
            {"x": 0, "y": 0, "width": 600, "height": 800},
            90,
        )
        self.assertEqual(result, {"x": 0.0, "y": 0.0, "width": 75.0, "height": 80.0})

    def test_invalid_bbox_never_emits_non_finite_values(self) -> None:
        result = pixel_bbox_to_pdf(
            [[math.nan, 0], [math.inf, 1]],
            1000,
            1000,
            {"x": 0, "y": 0, "width": 600, "height": 800},
        )
        self.assertTrue(all(math.isfinite(value) for value in result.values()))
        self.assertEqual(result["width"], 0.0)


if __name__ == "__main__":
    unittest.main()
