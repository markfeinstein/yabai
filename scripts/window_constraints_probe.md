# Window constraints probe

This opt-in developer tool creates only probe-owned AppKit fixtures, observes both known private read-only constraint getters, attempts original unmodified resize requests through a separate AX controller process, restores every fixture, and closes all windows. It does not touch operator windows or change production policy.

```sh
make window-constraints-probe
caffeinate -d -u -t 180 &
bin/debug/window-constraints-probe > /private/tmp/window-constraints-v2.jsonl
```

Keeping the display awake avoids a macOS state where `CGGetActiveDisplayList` temporarily reports zero active displays while the built-in display remains online. The assertion is temporary and is not part of the evidence.

## Schema v2

`yabai.window-constraints-probe/v2` emits:

- `run`: platform, symbol, trust, schedule, and diagnostic-only metadata;
- `fixture`: declared limits, increments, fixture kind, and delegate policy;
- `observation`: AppKit state, AX readback, iterator/packages raw values, validity, getter order/skew, basis, deltas, and agreement class;
- `resize_trial`: the original AX request, settled readback, separate iterator/packages predictions and confusion cells, delegate input/output, restore result, and authority block;
- `transition_summary`: 46 samples from 0 through 5000 ms with first-change latency;
- `summary`: agreement, AX write/restore outcomes, and per-source confusion counts.

Every resize trial contains:

```json
"authority": {
  "decision_source": "ax-readback",
  "private_constraints_modified_request": false,
  "production_policy_enabled": false
}
```

Private constraints never clamp, suppress, or rewrite the requested AX size.

## Controlled fixtures

The AppKit host covers:

- frame and content limit controls;
- fixed and unbounded controls;
- delegate-only effective clamping, tighten, and loosen transitions;
- frame and content resize increments with off-grid requests;
- toolbar hidden/visible transitions;
- utility panel;
- attached, detached, and reattached sheet;
- native tabs with different per-tab limits and A→B→A selection;
- minimized limit mutation and restored lifecycle state.

Resizable fixtures receive the full below/within/above 3×3 request matrix, exact minimum and maximum, and two off-grid requests for increment fixtures. AX settling requires three equal reads spanning at least 100 ms and times out after 2 seconds. Every trial restores and verifies the baseline.

The output uses fixture IDs but no raw PID, WindowServer ID, title, application name, bundle ID, path, absolute position, Space ID, or display ID.

## Current macOS 27.0.1 evidence

On build 26A434:

- 13 controlled fixtures produced 383 observations;
- iterator and packages/direct results were available and exactly equal in all 383 observations;
- the minimized dynamic-limit change became visible to both getters by 250 ms;
- delegate-only tightening/loosening, toolbar visibility, and active-tab selection did not change the private triplet within 5 seconds;
- 114 AX resize trials were attempted, but all were unscored: the separate controller reported existing AX trust yet could not resolve the probe-host AX windows by WindowServer ID, so no write was issued;
- no prompt was triggered, no request was modified, and all pre-existing window IDs and frames were restored.

This is an exact fixture-host/controller identity blocker, not evidence that AX writes are unsupported. Do not grant new Accessibility permission merely to complete the matrix. A future run needs a dedicated signed fixture application whose AX windows are resolvable from an already-trusted controller, or an existing suitable VM/account. Electron framed/frameless hosts are likewise unavailable as controlled pinned fixtures; existing user Electron applications must not be mutated.

## Decision policy

Constraints remain diagnostic-only. The v2 output may support nullable advisory fields only after cross-version evidence establishes stable basis, finite ordered limits, bounded freshness, and useful prediction for settled AX results. Delegate clamps and resize increments are expected sources of false negatives when absent from the private triplet.

Do not implement layout clamping, auto-floating, BSP correction, rejection, or creation-time-cache authority from this probe. AX write/readback plus bounded correction remains authoritative.
