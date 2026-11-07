#!/usr/bin/env python3
import json
import plistlib
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"analyzer signatures: {message}", file=sys.stderr)
    raise SystemExit(1)


def text_field(diagnostic: dict, name: str) -> str:
    value = diagnostic.get(name, "")
    return value if isinstance(value, str) else str(value)


def analyzer_signatures(root: Path, report: dict) -> list[str]:
    files = report.get("files")
    diagnostics = report.get("diagnostics")
    if not isinstance(files, list) or not isinstance(diagnostics, list):
        fail("report is missing files or diagnostics arrays")

    signatures: list[str] = []
    for diagnostic in diagnostics:
        if not isinstance(diagnostic, dict):
            fail("report contains a non-dictionary diagnostic")
        location = diagnostic.get("location")
        if not isinstance(location, dict) or not isinstance(location.get("file"), int):
            fail("diagnostic is missing its file location")
        file_index = location["file"]
        if file_index < 0 or file_index >= len(files) or not isinstance(files[file_index], str):
            fail("diagnostic has an invalid file index")

        source = Path(files[file_index])
        if not source.is_absolute():
            source = root / source
        try:
            relative_source = source.resolve().relative_to(root)
        except ValueError:
            continue

        signature = [
            relative_source.as_posix(),
            text_field(diagnostic, "check_name"),
            text_field(diagnostic, "description"),
            text_field(diagnostic, "issue_context_kind"),
            text_field(diagnostic, "issue_context"),
            text_field(diagnostic, "issue_hash_content_of_line_in_context"),
            text_field(diagnostic, "issue_hash_function_offset"),
        ]
        signatures.append(json.dumps(signature, ensure_ascii=True, separators=(",", ":")))
    return sorted(signatures)


def self_test() -> None:
    root = Path("/tmp/analyzer-signatures-root").resolve()
    diagnostic = {
        "check_name": "core.NullDereference",
        "description": "Dereference of null pointer",
        "issue_context_kind": "function",
        "issue_context": "example",
        "issue_hash_content_of_line_in_context": "content-hash",
        "issue_hash_function_offset": "4",
        "location": {"file": 0},
    }
    report = {
        "files": ["src/example.c", "/outside/system.h"],
        "diagnostics": [diagnostic, dict(diagnostic), {**diagnostic, "location": {"file": 1}}],
    }
    signatures = analyzer_signatures(root, report)
    if len(signatures) != 2 or signatures[0] != signatures[1]:
        fail("self-test did not preserve duplicate project diagnostics")
    print("analyzer signature self-test passed")


def main() -> None:
    if sys.argv[1:] == ["--self-test"]:
        self_test()
        return
    if len(sys.argv) != 3:
        fail("usage: analyzer_signatures.py REPOSITORY_ROOT REPORT_PLIST")

    root = Path(sys.argv[1]).resolve()
    report_path = Path(sys.argv[2])
    try:
        with report_path.open("rb") as report_file:
            report = plistlib.load(report_file)
    except (OSError, plistlib.InvalidFileException) as error:
        fail(f"could not read {report_path}: {error}")

    for signature in analyzer_signatures(root, report):
        print(signature)


if __name__ == "__main__":
    main()
