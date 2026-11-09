#!/usr/bin/env python3
"""Bounded driver for a read-only native WindowServer probe; never start yabai."""
import argparse
import json
import re
import subprocess

SCHEMA = "yabai.private-api-smoke/v1"
CHECKS = (
    "console_session", "connection", "managed_displays", "managed_spaces",
    "current_spaces", "space_types", "window_inventory", "production_snapshot",
    "snapshot_records", "window_bounds", "window_order", "snapshot_optional_fields",
)
COUNTS = ("display_count", "current_space_count", "window_count", "sample_count")


def run_probe(command, expected_os=None, revision="unknown", timeout=15):
    report = {"schema": SCHEMA, "source_revision": revision, "status": "fail"}
    try:
        process = subprocess.run(command, capture_output=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        return dict(report, failure="probe-timeout")
    except OSError:
        return dict(report, failure="probe-unavailable")
    if process.returncode != 0:
        return dict(report, failure="probe-exit", exit_code=process.returncode)
    try:
        native = json.loads(process.stdout)
    except (ValueError, UnicodeDecodeError):
        return dict(report, failure="invalid-json")
    if not isinstance(native, dict) or native.get("schema") != SCHEMA:
        return dict(report, failure="invalid-schema")
    version, build = native.get("os_version"), native.get("os_build")
    arch, translated = native.get("architecture"), native.get("translated")
    if (not isinstance(version, str) or not re.fullmatch(r"(?:26|27)\.\d+\.\d+", version)
            or not isinstance(build, str) or not re.fullmatch(r"[0-9A-Za-z.]+", build)
            or arch != "arm64" or type(translated) is not bool):
        return dict(report, failure="invalid-host-metadata")
    # Whitelist output: do not forward native stderr or unknown payload fields.
    report.update(os_version=version, os_build=build, architecture=arch, translated=translated)
    if translated:
        return dict(report, failure="translated-process-not-native-evidence")
    if expected_os is not None and version.split(".")[0] != expected_os:
        return dict(report, failure="unexpected-os")
    checks = native.get("checks")
    if not isinstance(checks, dict):
        return dict(report, failure="invalid-checks")
    report["checks"] = {name: checks.get(name) is True for name in CHECKS}
    if not all(report["checks"].values()):
        return dict(report, failure="private-api-check-failed")
    for name in COUNTS:
        value = native.get(name)
        if type(value) is not int or value <= 0:
            return dict(report, failure="invalid-counts")
        report[name] = value
    if (report["display_count"] > 64 or report["current_space_count"] > report["display_count"]
            or report["window_count"] > 100000 or report["sample_count"] != min(report["window_count"], 32)):
        return dict(report, failure="invalid-counts")
    report["status"] = "pass"
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", required=True)
    parser.add_argument("--expected-os", choices=("26", "27"))
    parser.add_argument("--source-revision", default="unknown")
    args = parser.parse_args()
    report = run_probe([args.probe], args.expected_os, args.source_revision)
    print(json.dumps(report, sort_keys=True))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
