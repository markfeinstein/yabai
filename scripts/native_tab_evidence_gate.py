#!/usr/bin/env python3
"""Validate planned five-repeat native-tab evidence batches offline."""

from __future__ import annotations

import argparse
import json
import stat
from collections import defaultdict
from pathlib import Path

SCHEMA = "yabai.native-tab-event-trace/v2"


def load_trace(path: Path):
    st = path.lstat()
    if stat.S_ISLNK(st.st_mode) or not stat.S_ISREG(st.st_mode):
        raise ValueError("input-not-regular")
    if st.st_mode & 0o077:
        raise ValueError("input-permissions-not-private")
    if st.st_size <= 0 or st.st_size > 50 * 1024 * 1024:
        raise ValueError("input-size-invalid")
    rows = []
    with path.open("r", encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            if not line.endswith("\n"):
                raise ValueError("unterminated-line")
            row = json.loads(line)
            if not isinstance(row, dict) or row.get("schema") != SCHEMA:
                raise ValueError("schema")
            rows.append(row)
    sessions = [row for row in rows if row.get("type") == "session"]
    summaries = [row for row in rows if row.get("type") == "summary"]
    if len(sessions) != 1 or len(summaries) != 1:
        raise ValueError("session-summary-cardinality")
    return sessions[0], [row for row in rows if row.get("type") == "resolution"], summaries[0]


def pair_signature(resolution):
    comparisons = resolution.get("comparisons") or []
    if len(comparisons) != 1:
        return None
    pair = comparisons[0]
    fields = (
        "same_owner",
        "shared_space",
        "root_compatible",
        "frame_delta_8pt",
        "level_delta",
        "sublevel_delta",
        "order_delta",
        "order_adjacent",
        "role_compatible",
        "same_title_token",
        "event_order",
        "sls_815_delivered",
        "sls_816_delivered",
        "lifecycle_exclusions",
        "readback_conflict",
    )
    return tuple(json.dumps(pair.get(field), sort_keys=True) for field in fields)


def evaluate(traces):
    blockers = set()
    batches = defaultdict(list)
    for session, resolutions, summary in traces:
        key = (
            session.get("os_build"),
            session.get("architecture"),
            session.get("fixture_token"),
            session.get("scenario"),
            session.get("direction"),
            session.get("expectation"),
            session.get("batch_id"),
            session.get("schema"),
        )
        batches[key].append((session, resolutions, summary))
        if session.get("production_identity_mutation") is not False or \
           summary.get("production_identity_mutation") is not False:
            blockers.add("production-mutation-present")

    for key, attempts in batches.items():
        label = ":".join(str(value) for value in key[:-1])
        repeats = [attempt[0].get("planned_repeat") for attempt in attempts]
        if len(attempts) != 5 or set(repeats) != {1, 2, 3, 4, 5} or len(repeats) != len(set(repeats)):
            blockers.add("planned-five-incomplete:" + label)
            continue
        expectation = key[5]
        signatures = set()
        for session, resolutions, summary in attempts:
            if summary.get("inventory_incomplete") != 0 or summary.get("dropped_records") != 0:
                blockers.add("trace-incomplete-or-dropped:" + label)
            if expectation == "candidate":
                eligible = [row for row in resolutions if row.get("gate_eligible") is True]
                if len(eligible) != 1 or len(resolutions) != 1:
                    blockers.add("candidate-resolution-cardinality:" + label)
                    continue
                resolution = eligible[0]
                if resolution.get("outcome") != "singleton-observational-candidate":
                    blockers.add("candidate-outcome:" + label)
                signature = pair_signature(resolution)
                if signature is None:
                    blockers.add("candidate-pair-cardinality:" + label)
                else:
                    signatures.add(signature)
                if resolution.get("action_separation_violated") or \
                   resolution.get("lifecycle_exclusions") or \
                   resolution.get("readback_conflict") or \
                   resolution.get("unresolved_outside"):
                    blockers.add("candidate-ambiguity:" + label)
            elif expectation == "negative-control":
                if summary.get("control_markers", 0) < 1:
                    blockers.add("negative-control-marker-missing:" + label)
                marker_requirements = {
                    "close": ("control_close",),
                    "minimize": ("control_minimize", "control_restore"),
                    "hide": ("control_hide", "control_unhide"),
                    "space-change": ("control_space_change",),
                }
                for field in marker_requirements.get(session.get("direction"), ()):
                    if summary.get(field, 0) < 1:
                        blockers.add("negative-control-marker-missing-" + field + ":" + label)
                if any(row.get("gate_eligible") is True for row in resolutions):
                    blockers.add("negative-control-eligible:" + label)
                if any(row.get("action_separation_violated") is True for row in resolutions):
                    blockers.add("negative-control-separation:" + label)
                if not resolutions or any(
                    row.get("outcome") not in {
                        "lifecycle-excluded",
                        "same-id-reappeared",
                        "action-separation-violated",
                    }
                    for row in resolutions
                ):
                    blockers.add("negative-control-outcome:" + label)
                signatures.add(
                    tuple(
                        (row.get("outcome"), row.get("lifecycle_exclusions"))
                        for row in resolutions
                    )
                )
            else:
                blockers.add("invalid-expectation:" + label)
        if len(signatures) != 1:
            blockers.add("repeat-signature-inconsistent:" + label)

    return {
        "schema": "yabai.native-tab-evidence-gate/v1",
        "status": "pass" if not blockers else "blocked",
        "batch_count": len(batches),
        "trace_count": len(traces),
        "blockers": sorted(blockers),
    }


def synthetic_trace(repeat, expectation="candidate", inconsistent=False):
    session = {
        "schema": SCHEMA,
        "type": "session",
        "os_build": "26A434",
        "architecture": "arm64",
        "fixture_token": "fixture",
        "scenario": "active-to-inactive",
        "direction": "a-to-b",
        "expectation": expectation,
        "batch_id": "batch",
        "planned_repeat": repeat,
        "production_identity_mutation": False,
    }
    if expectation == "candidate":
        pair = {
            "same_owner": True,
            "shared_space": True,
            "root_compatible": True,
            "frame_delta_8pt": [1 if inconsistent else 0, 0, 0, 0],
            "level_delta": 0,
            "sublevel_delta": 0,
            "order_delta": 1,
            "order_adjacent": True,
            "role_compatible": True,
            "same_title_token": True,
            "event_order": "816-before-815",
            "sls_815_delivered": 1,
            "sls_816_delivered": 1,
            "lifecycle_exclusions": 0,
            "readback_conflict": False,
        }
        resolutions = [{
            "schema": SCHEMA,
            "type": "resolution",
            "gate_eligible": True,
            "outcome": "singleton-observational-candidate",
            "action_separation_violated": False,
            "lifecycle_exclusions": 0,
            "readback_conflict": False,
            "unresolved_outside": False,
            "comparisons": [pair],
        }]
    else:
        resolutions = [{
            "schema": SCHEMA,
            "type": "resolution",
            "gate_eligible": False,
            "outcome": "lifecycle-excluded",
            "lifecycle_exclusions": 4,
        }]
    summary = {
        "schema": SCHEMA,
        "type": "summary",
        "inventory_incomplete": 0,
        "dropped_records": 0,
        "control_markers": 1 if expectation == "negative-control" else 0,
        "production_identity_mutation": False,
    }
    return session, resolutions, summary


def self_test():
    good = [synthetic_trace(repeat) for repeat in range(1, 6)]
    if evaluate(good)["status"] != "pass":
        return 1
    four = good[:4]
    if not any(blocker.startswith("planned-five-incomplete:")
               for blocker in evaluate(four)["blockers"]):
        return 1
    inconsistent = good[:4] + [synthetic_trace(5, inconsistent=True)]
    if not any(blocker.startswith("repeat-signature-inconsistent:")
               for blocker in evaluate(inconsistent)["blockers"]):
        return 1
    negative = [synthetic_trace(repeat, "negative-control") for repeat in range(1, 6)]
    if evaluate(negative)["status"] != "pass":
        return 1
    print("native tab evidence gate self-test passed")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("traces", nargs="*", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.traces:
        parser.error("at least one trace is required")
    traces = []
    errors = []
    for path in args.traces:
        try:
            traces.append(load_trace(path))
        except (OSError, ValueError, json.JSONDecodeError) as error:
            errors.append(type(error).__name__ + ":" + str(error))
    result = evaluate(traces)
    if errors:
        result["status"] = "blocked"
        result["blockers"] = sorted(
            result["blockers"] + ["input-error:" + error for error in errors]
        )
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
