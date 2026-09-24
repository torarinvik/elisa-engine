#!/usr/bin/env python3
"""Tests for the compact native lighting image-reference workflow."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from compare_lighting_references import compare_capture, update_reference
from compare_renders import read_png
from png_image import encode_png


def solid_rgba(width: int, height: int, color: tuple[int, int, int, int]) -> bytes:
    return bytes(color) * (width * height)


class LightingReferenceTests(unittest.TestCase):
    def test_update_downsamples_and_exact_capture_passes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa lighting references ") as directory:
            root = Path(directory)
            capture = root / "capture.png"
            reference = root / "reference.png"
            capture.write_bytes(encode_png(320, 200,
                solid_rgba(320, 200, (30, 100, 220, 255))))

            update_reference(capture, reference)

            self.assertEqual(read_png(reference)[:2], (160, 100))
            peak, mean, passed = compare_capture(capture, reference, 0.35, 0.025)
            self.assertTrue(passed)
            self.assertEqual(peak, 0.0)
            self.assertEqual(mean, 0.0)

    def test_visual_change_over_tolerance_fails(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa lighting references ") as directory:
            root = Path(directory)
            capture = root / "capture.png"
            reference = root / "reference.png"
            capture.write_bytes(encode_png(320, 200,
                solid_rgba(320, 200, (30, 100, 220, 255))))
            update_reference(capture, reference)
            capture.write_bytes(encode_png(320, 200,
                solid_rgba(320, 200, (230, 20, 10, 255))))

            peak, mean, passed = compare_capture(capture, reference, 0.35, 0.025)

            self.assertFalse(passed)
            self.assertGreater(peak, 0.35)
            self.assertGreater(mean, 0.025)

    def test_non_sixteen_to_ten_capture_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa lighting references ") as directory:
            root = Path(directory)
            capture = root / "capture.png"
            reference = root / "reference.png"
            capture.write_bytes(encode_png(320, 240,
                solid_rgba(320, 240, (30, 100, 220, 255))))

            with self.assertRaisesRegex(ValueError, "16:10"):
                update_reference(capture, reference)
            self.assertFalse(reference.exists())


if __name__ == "__main__":
    unittest.main()
