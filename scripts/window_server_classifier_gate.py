#!/usr/bin/env python3
"""Offline activation-gate validator for strict-redacted classifier corpora."""

from __future__ import annotations

import argparse
import json
import stat
import sys
from collections import defaultdict
from pathlib import Path
from typing import Optional

SCHEMA = "yabai.window-server-classifier-comparison/v2"
REQUIRED_OS = ("26.6", "27.0", "27.2")
REQUIRED_ROLES = {
    "primary-document",
    "root-dialog",
    "child-sheet",
    "floating-panel",
    "menu-extra-negative",
    "dock-negative",
    "system-overlay-negative",
    "electron-main",
    "electron-helper",
    "fullscreen",
    "split-view",
    "minimized",
    "restored",
    "stage-manager-off",
    "stage-manager-on",
    "stage-manager-shelf",
    "native-tab-active",
    "native-tab-inactive",
    "native-tab-detached",
    "native-tab-bar",
    "bit58-ordinary",
}
MANAGEABLE = "manageable"


def os_bucket(version: str) -> Optional[str]:
    for required in REQUIRED_OS:
        if version == required or version.startswith(required + "."):
            return required
    return None


def load_capture(path: Path) -> tuple[dict, list[dict]]:
    st = path.lstat()
    if stat.S_ISLNK(st.st_mode) or not stat.S_ISREG(st.st_mode):
        raise ValueError("input-not-regular")
    if st.st_mode & 0o077:
        raise ValueError("input-permissions-not-private")
    if st.st_size <= 0 or st.st_size > 50 * 1024 * 1024:
        raise ValueError("input-size-invalid")
    rows = []
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.endswith("\n"):
                raise ValueError(f"line-{line_number}-not-terminated")
            row = json.loads(line)
            if not isinstance(row, dict) or row.get("schema") != SCHEMA:
                raise ValueError(f"line-{line_number}-schema")
            rows.append(row)
    if not rows or rows[0].get("type") != "capture" or any(
        row.get("type") != "window" for row in rows[1:]
    ):
        raise ValueError("record-order")
    return rows[0], rows[1:]


def evaluate(captures: list[tuple[dict, list[dict]]]) -> dict:
    blockers: set[str] = set()
    observed_os: set[str] = set()
    roles_by_os: dict[str, set[str]] = defaultdict(set)
    ax_modes_by_os: dict[str, set[str]] = defaultdict(set)
    repeats: dict[tuple, set[int]] = defaultdict(set)
    fixture_rows: dict[tuple, list[dict]] = defaultdict(list)
    bit58_pairs: dict[tuple, dict[str, list[dict]]] = defaultdict(
        lambda: defaultdict(list)
    )
    source_revisions: set[str] = set()
    stage_states: set[str] = set()
    has_multi_display_separate_spaces = False
    capture_count = 0
    window_count = 0

    for header, windows in captures:
        capture_count += 1
        window_count += len(windows)
        bucket = os_bucket(str(header.get("os_version", "")))
        if bucket is None:
            blockers.add("unsupported-os-capture")
            continue
        observed_os.add(bucket)
        source_revisions.add(str(header.get("source_revision", "")))
        ax_mode = header.get("ax_mode")
        ax_modes_by_os[bucket].add(str(ax_mode))
        stage_states.add(str(header.get("stage_manager")))
        if header.get("display_count", 0) >= 2 and header.get("separate_spaces") == "yes":
            has_multi_display_separate_spaces = True
        if header.get("activation_state") != "diagnostic-only":
            blockers.add("activation-state-not-diagnostic")
        if header.get("fallback_algorithm") != "legacy-space-eligibility-v1":
            blockers.add("fallback-not-legacy")
        if header.get("rollback_policy") != "disable-candidate-and-use-legacy":
            blockers.add("rollback-policy-missing")
        if header.get("tag_layout_status") != "sampled":
            blockers.add(f"tag-layout-not-sampled:{bucket}")
        counts = header.get("counts") or {}
        if counts.get("false_positive") != 0:
            blockers.add(f"false-positive:{bucket}")
        if counts.get("unlabeled_disagreement") != 0:
            blockers.add(f"unlabeled-disagreement:{bucket}")
        if counts.get("pending_disagreement") != 0:
            blockers.add(f"pending-disagreement:{bucket}")
        if ax_mode == "auto" and counts.get("ax_matched", 0) == 0:
            blockers.add(f"ax-authorized-evidence-missing:{bucket}")

        capture_set = header.get("capture_set_id")
        repeat_index = header.get("repeat_index")
        scenario = header.get("scenario")
        repeat_key = (bucket, capture_set, scenario, ax_mode)
        if not isinstance(repeat_index, int) or repeat_index < 1:
            blockers.add("invalid-repeat-index")
        else:
            repeats[repeat_key].add(repeat_index)

        for row in windows:
            role = row.get("fixture_role")
            if role:
                roles_by_os[bucket].add(str(role))
            normal_disagreement = row.get("comparison_normal") in {
                "candidate-only",
                "legacy-only",
            }
            minimized_disagreement = row.get("comparison_include_minimized") in {
                "candidate-only",
                "legacy-only",
            }
            if normal_disagreement or minimized_disagreement:
                if not role:
                    blockers.add(f"unlabeled-window-disagreement:{bucket}")
                if row.get("adjudication_status") not in {"confirmed", "rejected"}:
                    blockers.add(f"unadjudicated-window-disagreement:{bucket}")
                if not row.get("adjudication_reason") or not row.get("evidence_ref"):
                    blockers.add(f"unexplained-window-disagreement:{bucket}")
            if row.get("expected_normal") == MANAGEABLE:
                if row.get("candidate_normal") != MANAGEABLE:
                    blockers.add(f"required-positive-normal-missed:{bucket}")
                if row.get("adjudication_status") != "confirmed":
                    blockers.add(f"required-positive-normal-unexplained:{bucket}")
            if row.get("expected_include_minimized") == MANAGEABLE:
                if row.get("candidate_include_minimized") != MANAGEABLE:
                    blockers.add(f"required-positive-minimized-missed:{bucket}")
                if row.get("adjudication_status") != "confirmed":
                    blockers.add(f"required-positive-minimized-unexplained:{bucket}")
            fixture_id = row.get("fixture_id")
            if fixture_id:
                key = (bucket, capture_set, ax_mode, fixture_id)
                fixture_rows[key].append(row)
            pair = row.get("control_pair_id")
            state = row.get("bit58_state")
            if pair:
                bit58_pairs[(bucket, pair)][str(state)].append(row)

    for required in REQUIRED_OS:
        if required not in observed_os:
            blockers.add(f"missing-os:{required}")
        missing_roles = REQUIRED_ROLES - roles_by_os.get(required, set())
        for role in sorted(missing_roles):
            blockers.add(f"missing-role:{required}:{role}")
        if ax_modes_by_os.get(required, set()) != {"auto", "unavailable"}:
            blockers.add(f"ax-mode-pair-missing:{required}")

    for key, indices in repeats.items():
        if len(indices) < 2:
            blockers.add("repeat-count-insufficient:" + ":".join(map(str, key)))

    for key, rows in fixture_rows.items():
        if len(rows) < 2:
            blockers.add("fixture-repeat-insufficient:" + ":".join(map(str, key)))
            continue
        stable_fields = (
            "legacy_normal",
            "legacy_include_minimized",
            "candidate_normal",
            "candidate_include_minimized",
            "comparison_normal",
            "comparison_include_minimized",
        )
        signatures = {tuple(row.get(field) for field in stable_fields) for row in rows}
        if len(signatures) != 1:
            blockers.add("fixture-repeat-unstable:" + ":".join(map(str, key)))

    for required in REQUIRED_OS:
        pairs = [value for (bucket, _), value in bit58_pairs.items() if bucket == required]
        if not pairs:
            blockers.add(f"bit58-pair-missing:{required}")
        for states in pairs:
            if set(states) != {"clear", "set"}:
                blockers.add(f"bit58-state-pair-incomplete:{required}")
                continue
            clear = states["clear"][0]
            set_row = states["set"][0]
            fields = ("candidate_normal", "candidate_include_minimized")
            if any(clear.get(field) != set_row.get(field) for field in fields):
                blockers.add(f"bit58-classification-changed:{required}")

    if stage_states < {"off", "on"}:
        blockers.add("stage-manager-pair-missing")
    if not has_multi_display_separate_spaces:
        blockers.add("multi-display-separate-spaces-missing")
    if len(source_revisions) != 1 or "" in source_revisions:
        blockers.add("source-revision-not-uniform")

    return {
        "schema": "yabai.window-server-classifier-gate/v1",
        "status": "pass" if not blockers else "blocked",
        "capture_count": capture_count,
        "window_count": window_count,
        "os_coverage": sorted(observed_os),
        "source_revisions": sorted(source_revisions),
        "blockers": sorted(blockers),
    }


def synthetic_capture(os_version: str, ax_mode: str, repeat: int) -> tuple[dict, list[dict]]:
    bucket = os_bucket(os_version)
    assert bucket
    header = {
        "schema": SCHEMA,
        "type": "capture",
        "scenario": "synthetic",
        "capture_set_id": f"set-{bucket.replace('.', '-')}",
        "repeat_index": repeat,
        "os_version": os_version,
        "source_revision": "synthetic-revision",
        "ax_mode": ax_mode,
        "display_count": 2,
        "separate_spaces": "yes",
        "stage_manager": "on" if repeat == 2 else "off",
        "activation_state": "diagnostic-only",
        "fallback_algorithm": "legacy-space-eligibility-v1",
        "rollback_policy": "disable-candidate-and-use-legacy",
        "tag_layout_status": "sampled",
        "counts": {
            "false_positive": 0,
            "unlabeled_disagreement": 0,
            "pending_disagreement": 0,
            "ax_matched": 1 if ax_mode == "auto" else 0,
        },
    }
    rows = []
    negative = {
        "root-dialog",
        "child-sheet",
        "floating-panel",
        "menu-extra-negative",
        "dock-negative",
        "system-overlay-negative",
        "electron-helper",
        "stage-manager-shelf",
        "native-tab-inactive",
        "native-tab-bar",
    }
    for index, role in enumerate(sorted(REQUIRED_ROLES - {"bit58-ordinary"})):
        expected = "user-facing-nonmanageable" if role in negative else MANAGEABLE
        rows.append(
            {
                "schema": SCHEMA,
                "type": "window",
                "fixture_id": f"fixture-{index}",
                "fixture_role": role,
                "expected_normal": expected,
                "expected_include_minimized": expected,
                "candidate_normal": expected,
                "candidate_include_minimized": expected,
                "legacy_normal": expected == MANAGEABLE,
                "legacy_include_minimized": expected == MANAGEABLE,
                "comparison_normal": "same-include" if expected == MANAGEABLE else "same-exclude",
                "comparison_include_minimized": "same-include" if expected == MANAGEABLE else "same-exclude",
                "adjudication_status": "confirmed",
                "adjudication_reason": "synthetic-control",
                "evidence_ref": "synthetic-evidence",
                "bit58_state": "not-applicable",
            }
        )
    for state in ("clear", "set"):
        rows.append(
            {
                "schema": SCHEMA,
                "type": "window",
                "fixture_id": f"bit58-{state}",
                "fixture_role": "bit58-ordinary",
                "expected_normal": MANAGEABLE,
                "expected_include_minimized": MANAGEABLE,
                "candidate_normal": MANAGEABLE,
                "candidate_include_minimized": MANAGEABLE,
                "legacy_normal": True,
                "legacy_include_minimized": True,
                "comparison_normal": "same-include",
                "comparison_include_minimized": "same-include",
                "adjudication_status": "confirmed",
                "adjudication_reason": "synthetic-bit58-control",
                "evidence_ref": "synthetic-evidence",
                "control_pair_id": f"pair-{bucket.replace('.', '-')}",
                "bit58_state": state,
            }
        )
    return header, rows


def self_test() -> int:
    captures = [
        synthetic_capture(version, mode, repeat)
        for version in REQUIRED_OS
        for mode in ("auto", "unavailable")
        for repeat in (1, 2)
    ]
    result = evaluate(captures)
    if result["status"] != "pass":
        print(json.dumps(result, sort_keys=True))
        return 1
    broken = [(dict(header), [dict(row) for row in rows]) for header, rows in captures]
    broken[0][0]["counts"] = dict(broken[0][0]["counts"], false_positive=1)
    result = evaluate(broken)
    if result["status"] != "blocked" or not any(
        blocker.startswith("false-positive:") for blocker in result["blockers"]
    ):
        print(json.dumps(result, sort_keys=True))
        return 1
    print("window server classifier gate self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("captures", nargs="*", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.captures:
        parser.error("at least one capture is required")
    captures = []
    errors = []
    for path in args.captures:
        try:
            captures.append(load_capture(path))
        except (OSError, ValueError, json.JSONDecodeError) as error:
            errors.append(type(error).__name__ + ":" + str(error))
    if errors:
        result = {
            "schema": "yabai.window-server-classifier-gate/v1",
            "status": "blocked",
            "capture_count": len(captures),
            "window_count": sum(len(rows) for _, rows in captures),
            "os_coverage": [],
            "source_revisions": [],
            "blockers": sorted("input-error:" + error for error in errors),
        }
    else:
        result = evaluate(captures)
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
