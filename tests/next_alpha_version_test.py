#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from next_alpha_version import next_alpha_tag, parse_numeric_version  # noqa: E402


class NextAlphaVersionTests(unittest.TestCase):
    def test_first_release(self) -> None:
        self.assertEqual(next_alpha_tag([]), "v0.1.0-alpha")

    def test_increments_highest_numeric_version(self) -> None:
        tags = ["v0.1.24-alpha", "v0.1.9", "v0.2.3-beta", "not-a-version"]
        self.assertEqual(next_alpha_tag(tags), "v0.2.4-alpha")

    def test_reuses_tag_already_attached_to_head_for_reruns(self) -> None:
        tags = ["v0.1.24-alpha", "v0.1.25-alpha"]
        self.assertEqual(next_alpha_tag(tags, ["v0.1.25-alpha"]), "v0.1.25-alpha")

    def test_ignores_invalid_or_partial_tags(self) -> None:
        self.assertIsNone(parse_numeric_version("v1.2"))
        self.assertIsNone(parse_numeric_version("release-v1.2.3"))
        self.assertEqual(next_alpha_tag(["latest", "v0.3.8-alpha-1"]), "v0.3.9-alpha")


if __name__ == "__main__":
    unittest.main(verbosity=2)
