# Window constraints probe

This opt-in developer tool creates only probe-owned AppKit fixtures, observes both known private read-only constraint getters, attempts original unmodified resize requests through a separate AX controller process, restores every fixture, and closes all windows. It does not touch operator windows or change production policy.

```sh
make window-constraints-probe
caffeinate -d -u -t 180 &
bin/debug/window-constraints-probe > /private/tmp/window-constraints-v2.jsonl
```

For a strictly read-only run that creates only its own AppKit fixtures and skips every AX resize matrix:

```sh
bin/debug/window-constraints-probe --observe-only
```

For a bounded, redacted snapshot of up to 32 on-screen external windows, without creating fixtures or issuing AX writes:

```sh
bin/debug/window-constraints-probe --external-read-only
```

The fixture mode uses only ordinary AX reads; the external snapshot makes no AX calls at all. Neither mode requests Accessibility trust or prompts for TCC. Both omit titles, application identities, PIDs, window IDs, paths, and absolute origins, and report `ax_writes:0` / `tcc_prompt_requested:false`.

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

On build 26A434, a strict `--observe-only` run produced 383 observations across 13 controlled fixtures, with zero AX writes and no TCC prompt request. Iterator and packages/direct results were exactly equal in all 383 observations. Observed basis frequencies for the iterator were:

- minimum and maximum: 93 `both`, 261–262 `frame`, 27–28 `other`, 1 `unknown` each;
- current: one capture was 307 `frame`, 75 `other`, 1 `unknown`; a second capture was 380 `frame`, 2 `other`, 1 `unknown`.

The exact current-size basis varied between otherwise equal getter snapshots, so it remains diagnostic rather than authoritative. The minimized dynamic-limit change became visible to both getters at 250–300 ms across three observe-only runs. Delegate-only tightening/loosening, toolbar visibility, tab selection, restored state, and sheet transitions did not alter the private constraint triplet within their five-second samples.
- delegate-only tightening/loosening, toolbar visibility, and active-tab selection did not change the private triplet within 5 seconds;
- 114 AX resize trials were attempted, but all were unscored: the separate controller reported existing AX trust yet could not resolve the probe-host AX windows by WindowServer ID, so no write was issued;
- no prompt was triggered, no request was modified, and all pre-existing window IDs and frames were restored.

This is an exact fixture-host/controller identity blocker, not evidence that AX writes are unsupported. Do not grant new Accessibility permission merely to complete the matrix. A future run needs a dedicated signed fixture application whose AX windows are resolvable from an already-trusted controller, or an existing suitable VM/account. Electron framed/frameless hosts are likewise unavailable as controlled pinned fixtures; existing user Electron applications must not be mutated.

## Read-only external snapshot

Schema v3 `--external-read-only` samples at most 32 on-screen CG windows owned by other processes. It correlates the two private getter results, immediate repeat stability, and public CG bounds size. It does not call AX. Rows contain only an ordinal, zero/nonzero window-layer class, private numeric constraints, and equality booleans; they never emit the selected process/window identity or absolute frame origin. Sampling is observational only and does not change target windows.

Four earlier AX-read-only snapshots sampled 6–8 external windows; the iterator succeeded and repeated stably for every sampled window, while the packages getter returned the same non-success status (`268435459`) for every sample. Four or five AX windows were resolvable per run, with zero controller-launch errors. These AX observations are ephemeral and were not retained in the new external-only mode. The packages result is not comparable to the successful AppKit-host result; external-window access restrictions or an API-specific owner/connection basis remain plausible, not proven. A non-success status is never treated as valid constraints.

The `--observe-only` mode runs the controlled AppKit lifecycle/freshness suite, including delegate tightening/loosening, toolbar visibility, tab selection, minimize/restore, and AX read-only inspection, while skipping every `resize_trial`. This lets those private-getter basis/freshness measurements run without issuing any AX size writes.

## Decision policy

Constraints remain diagnostic-only. The v2 output may support nullable advisory fields only after cross-version evidence establishes stable basis, finite ordered limits, bounded freshness, and useful prediction for settled AX results. Delegate clamps and resize increments are expected sources of false negatives when absent from the private triplet.

Do not implement layout clamping, auto-floating, BSP correction, rejection, or creation-time-cache authority from this probe. AX write/readback plus bounded correction remains authoritative.
