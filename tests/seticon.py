#!/usr/bin/env python3

import os
from pathlib import Path
import subprocess
import sys
import tempfile


REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT = REPO_ROOT / "scripts" / "seticon.py"

FAKE_COCOA = """
import os


class _Image:
    @classmethod
    def alloc(cls):
        return cls()

    def initWithContentsOfFile_(self, path):
        if path != os.environ["SETICON_EXPECTED_IMAGE"]:
            raise AssertionError(f"unexpected image path: {path}")
        self.path = path
        return None if os.environ.get("SETICON_IMAGE_MISSING") == "1" else self


class _Workspace:
    @classmethod
    def sharedWorkspace(cls):
        return cls()

    def setIcon_forFile_options_(self, image, binary, options):
        if image.path != os.environ["SETICON_EXPECTED_IMAGE"]:
            raise AssertionError("workspace received the wrong image")
        if binary != os.environ["SETICON_EXPECTED_BINARY"]:
            raise AssertionError(f"unexpected target path: {binary}")
        if options != 0:
            raise AssertionError(f"unexpected workspace options: {options}")
        return os.environ.get("SETICON_WORKSPACE_FAIL") != "1"


NSImage = _Image
NSWorkspace = _Workspace
"""


def run_case(module_dir, *arguments, **overrides):
    environment = os.environ.copy()
    environment.pop("SETICON_IMAGE_MISSING", None)
    environment.pop("SETICON_WORKSPACE_FAIL", None)
    environment["PYTHONPATH"] = str(module_dir)
    if len(arguments) == 2:
        environment["SETICON_EXPECTED_IMAGE"] = arguments[0]
        environment["SETICON_EXPECTED_BINARY"] = arguments[1]
    environment.update(overrides)
    return subprocess.run(
        [sys.executable, str(SCRIPT), *arguments],
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    with tempfile.TemporaryDirectory(prefix="yabai-seticon-test.") as directory:
        module_dir = Path(directory)
        (module_dir / "Cocoa.py").write_text(FAKE_COCOA, encoding="utf-8")

        result = run_case(module_dir)
        check(result.returncode == 2, "missing arguments did not fail with usage status")
        check("usage:" in result.stderr, "missing arguments did not print usage")

        import_failure_dir = module_dir / "import-failure"
        import_failure_dir.mkdir()
        (import_failure_dir / "Cocoa.py").write_text("raise ImportError('fixture import failure')\n", encoding="utf-8")
        result = run_case(import_failure_dir, "icon.png", "yabai")
        check(result.returncode == 1, "Cocoa import failure did not fail")
        check("could not import Cocoa" in result.stderr, "Cocoa import diagnostic missing")

        result = run_case(module_dir, "missing.png", "yabai", SETICON_IMAGE_MISSING="1")
        check(result.returncode == 1, "invalid image did not fail")
        check("could not load icon image" in result.stderr, "invalid image diagnostic missing")

        result = run_case(module_dir, "icon.png", "yabai", SETICON_WORKSPACE_FAIL="1")
        check(result.returncode == 1, "workspace rejection did not fail")
        check("could not set icon" in result.stderr, "workspace rejection diagnostic missing")

        result = run_case(module_dir, "icon.png", "yabai")
        check(result.returncode == 0, "successful icon assignment failed")

    print("seticon regression tests passed (5 cases)")


if __name__ == "__main__":
    main()
