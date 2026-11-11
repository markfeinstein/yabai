#!/usr/bin/env python3
"""Extract one Keep a Changelog section without rewriting its Markdown."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SECTION_HEADING = re.compile(r"^## \[([^]]+)](?: - .+)?$")


def extract_section(changelog: str, section: str) -> str:
    lines = changelog.splitlines(keepends=True)
    start = None
    end = len(lines)

    for index, line in enumerate(lines):
        match = SECTION_HEADING.fullmatch(line.rstrip("\r\n"))
        if match is None:
            continue
        if start is None:
            if match.group(1) == section:
                start = index
        else:
            end = index
            break

    if start is None:
        raise ValueError(f"changelog section [{section}] was not found")

    selected = "".join(lines[start:end]).rstrip() + "\n"
    if not any(line.strip() and not line.startswith("## [") for line in lines[start + 1 : end]):
        raise ValueError(f"changelog section [{section}] is empty")
    return selected


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("changelog", type=Path)
    parser.add_argument("section")
    args = parser.parse_args()

    try:
        changelog = args.changelog.read_text(encoding="utf-8")
        sys.stdout.write(extract_section(changelog, args.section))
    except (OSError, UnicodeError, ValueError) as error:
        print(f"extract_changelog: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
