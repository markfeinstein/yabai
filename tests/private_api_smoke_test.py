#!/usr/bin/env python3
"""Offline driver tests: synthetic reports are not live compatibility evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("smoke", Path(__file__).parent / "integration/private_api_smoke.py")
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


class SmokeDriverTests(unittest.TestCase):
    def setUp(self):
        self.native = dict(schema=smoke.SCHEMA, os_version="27.2.0", os_build="26B123",
                           architecture="arm64", translated=False,
                           checks=dict.fromkeys(smoke.CHECKS, True), display_count=2,
                           current_space_count=2, window_count=40, sample_count=32)

    def run_native(self, native, **kwargs):
        process = subprocess.CompletedProcess([], 0, json.dumps(native).encode(), b"private-stderr")
        with patch.object(smoke.subprocess, "run", return_value=process):
            return smoke.run_probe(["probe"], **kwargs)

    def test_expected_host_and_redaction(self):
        self.native["private-title"] = "do-not-log"
        result = self.run_native(self.native, expected_os="27", expected_arch="arm64", revision="abc123")
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["source_revision"], "abc123")
        self.assertNotIn("do-not-log", json.dumps(result))
        self.assertNotIn("private-title", result)
        self.assertNotIn("private-stderr", json.dumps(result))
        self.assertEqual(self.run_native(self.native, expected_os="26")["failure"], "unexpected-os")
        self.assertEqual(self.run_native(self.native, expected_arch="x86_64")["failure"], "unexpected-architecture")
        self.native["translated"] = True
        self.assertEqual(self.run_native(self.native)["failure"], "translated-process-not-native-evidence")

    def test_missing_failed_and_nonboolean_checks_are_failures(self):
        for name in smoke.CHECKS:
            for value in (None, False, 1, "true"):
                native = copy.deepcopy(self.native)
                native["checks"][name] = value
                self.assertEqual(self.run_native(native)["status"], "fail")
            native = copy.deepcopy(self.native)
            del native["checks"][name]
            self.assertEqual(self.run_native(native)["status"], "fail")

    def test_invalid_shapes_metadata_and_counts(self):
        for native in ([], None, {}, dict(self.native, schema="other"), dict(self.native, checks=[])):
            self.assertEqual(self.run_native(native)["status"], "fail")
        for field, values in {
            "os_version": (None, "13.0.0", "27", "27.2.0\nprivate"),
            "os_build": (None, "26B123\nprivate"), "architecture": (None, "other"),
            "translated": (None, 0), "display_count": (0, 65, True),
            "current_space_count": (0, 3), "window_count": (0, 100001),
            "sample_count": (0, 31, 33),
        }.items():
            for value in values:
                self.assertEqual(self.run_native(dict(self.native, **{field: value}))["status"], "fail")

    def test_timeout_unavailable_exit_and_bad_json(self):
        for exception, failure in ((subprocess.TimeoutExpired("probe", 15), "probe-timeout"),
                                   (FileNotFoundError(), "probe-unavailable")):
            with patch.object(smoke.subprocess, "run", side_effect=exception):
                self.assertEqual(smoke.run_probe(["probe"])["failure"], failure)
        for status, output, failure in ((1, b"private", "probe-exit"), (0, b"bad", "invalid-json")):
            process = subprocess.CompletedProcess([], status, output, b"private-stderr")
            with patch.object(smoke.subprocess, "run", return_value=process):
                report = smoke.run_probe(["probe"])
            self.assertEqual(report["failure"], failure)
            self.assertNotIn("private", json.dumps(report).replace(smoke.SCHEMA, ""))

    def test_real_entry_point_failure_and_process_timeout(self):
        driver = str(Path(__file__).parent / "integration/private_api_smoke.py")
        process = subprocess.run([sys.executable, "-B", driver, "--probe", "/does/not/exist"],
                                 capture_output=True, timeout=5)
        self.assertEqual(process.returncode, 1)
        self.assertEqual(json.loads(process.stdout)["failure"], "probe-unavailable")
        # Exercise actual process timeout handling without touching the desktop.
        report = smoke.run_probe([sys.executable, "-c", "import time; time.sleep(1)"], timeout=0.02)
        self.assertEqual(report["failure"], "probe-timeout")


if __name__ == "__main__":
    unittest.main()
