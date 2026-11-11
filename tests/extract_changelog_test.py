#!/usr/bin/env python3

import importlib.util
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "extract_changelog.py"
SPEC = importlib.util.spec_from_file_location("extract_changelog", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ExtractChangelogTests(unittest.TestCase):
    CHANGELOG = """# Changelog

## [Unreleased]

### Added
- New nightly behavior.

## [1.2.3] - 2026-11-11

Release introduction.

### Fixed
- Fixed the release.

## [1.2.2] - 2026-11-10
- Previous release.
"""

    def test_extracts_unreleased_section(self):
        self.assertEqual(
            MODULE.extract_section(self.CHANGELOG, "Unreleased"),
            "## [Unreleased]\n\n### Added\n- New nightly behavior.\n",
        )

    def test_extracts_versioned_section_with_date(self):
        self.assertEqual(
            MODULE.extract_section(self.CHANGELOG, "1.2.3"),
            "## [1.2.3] - 2026-11-11\n\nRelease introduction.\n\n### Fixed\n- Fixed the release.\n",
        )

    def test_rejects_missing_section(self):
        with self.assertRaisesRegex(ValueError, r"section \[9.9.9] was not found"):
            MODULE.extract_section(self.CHANGELOG, "9.9.9")

    def test_rejects_empty_section(self):
        with self.assertRaisesRegex(ValueError, r"section \[Unreleased] is empty"):
            MODULE.extract_section("## [Unreleased]\n\n## [1.0.0] - 2026-11-11\n- Change.\n", "Unreleased")


if __name__ == "__main__":
    unittest.main()
