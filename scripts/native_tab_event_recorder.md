# Native-tab event recorder

This opt-in developer tool records observational AX, WindowServer, workspace, Space, and Mission Control evidence for one already-running application. It does not launch, activate, focus, tab, detach, move, resize, minimize, close, or replace windows. It is not connected to the yabai daemon and never calls production identity mutation, dormant aliases, replacement signals, or `window_manager_replace_window_identity`.

```sh
make native-tab-event-recorder
make native-tab-evidence-gate

bin/debug/native-tab-event-recorder \
  --pid PID \
  --scenario active-to-inactive \
  --fixture-token controlled-editor \
  --batch active-to-inactive-27.0 \
  --repeat 1 \
  --direction a-to-b \
  --expectation candidate \
  --duration 15 \
  --output /private/tmp/native-tabs-r1.jsonl
```

The output path must not exist. The recorder creates it with mode `0600`, rejects symlinks and existing paths, and removes partial output on failure. Keep traces outside repositories and delete them after analysis.

## Permission policy

The recorder checks existing Accessibility trust with `AXIsProcessTrusted()`. It never uses the prompting API. If Accessibility is not already trusted, it records `ax_available:false` and continues with reduced evidence. Do not grant permissions merely to run the tool.

## Authoritative inventories

Complete inventories define endpoint cardinality. SLS and AX callbacks are corroborating evidence and never create appeared/disappeared endpoints by themselves.

- baseline: the last complete inventory before the causal episode;
- terminal: a complete inventory followed by unchanged complete polls and the required post-action quiet interval;
- disappeared: baseline minus terminal;
- appeared: terminal minus baseline;
- incomplete inventories are recorded but never replace the last authoritative complete inventory;
- a remove/add of the same ID is `same-id-reappeared`, not an old/new identity pair.

The recorder emits the full bounded Cartesian product `disappeared × appeared`. Each vector reports redacted owner, Space, root/parent, frame, level/sublevel, order/adjacency, role/subrole, title-token, event-order, delivered-count, conflict, and lifecycle evidence. It does not greedily choose an identity.

`singleton-observational-candidate` requires one distinct disappeared/appeared pair with known compatible Space, root/parent, level and sublevel facts, complete inventories, and no exclusion, conflict, unresolved outside event, overflow, or separation violation. The label is evidence only.

## Exact action separation

Schema v2 uses:

```c
NATIVE_TAB_ACTION_SEPARATION_NS = 600000000
```

A gate-eligible episode requires:

- at least 600 ms of complete unchanged baseline before action start;
- at least 600 ms from the prior action end to a later action start;
- at least 600 ms without another causal action after the terminal delta.

Exactly 600 ms passes. 599,999,999 ns fails with `action-separation-violated`. Prefer one operator action per trace.

## SLS 815/816 accounting

Callbacks are counted on callback entry before payload validation. Event rows include:

- raw code, payload length and validity;
- per-code delivered index;
- subscription generation and whether the ID was known subscribed;
- ordered-in readback and confirmed/conflict/unknown disposition;
- duplicate status;
- target-PID corroboration when inventory facts are available.

Episode and summary rows retain delivered, invalid, confirmed, conflict, unknown, and duplicate counts. Event order is one of `816-before-815`, `815-before-816`, `816-only`, `815-only`, `neither`, or `mixed-or-duplicate-order`. Inventory cardinality remains authoritative.

## AX focus readbacks

A focused-window callback records separate redacted ordinals for:

- notification subject;
- immediate application focused-window readback;
- main-window readback;
- a stable focused-window readback emitted after 100 ms.

Conflicts remain visible; they are not guessed into identity.

## Lifecycle exclusions and ambiguity

Destroy, minimize/deminiaturize, app hide/unhide, app termination, active-Space change, and Mission Control/topology events join the current episode and set lifecycle exclusion bits. They exclude replacement interpretation whether delivered before or after 815/816.

The recorder is gate-ineligible for non-singleton cardinality, missing mandatory facts, incomplete inventory, readback conflict, unresolved outside event, lifecycle exclusion, capacity overflow, action-separation violation, or inconsistent evidence across repeats.

## Strict redaction

JSONL uses `yabai.native-tab-event-trace/v2` with `strict-v1` redaction. It never serializes raw PID, WindowServer ID, Space ID, application name, bundle ID, executable path, title text, AX descriptions, absolute frame coordinates, or wall-clock timestamps.

Per-run ordinals replace identities. Titles, roles, and subroles are equality-only tokens in memory. Frames appear only as old/new deltas quantized in 8-point units.

## Planned five-repeat gate

A batch is planned in advance with one file for each `--repeat 1` through `--repeat 5`. Validate it offline:

```sh
python3 scripts/native_tab_evidence_gate.py /private/tmp/native-tabs-*.jsonl
```

The exact tuple is OS build, architecture, fixture token, scenario, direction, expectation, batch ID, and schema version. A candidate batch passes only when all five traces contain one complete, separated, gate-eligible singleton episode with the same comparison and 815/816 delivered-count signature. Four successes are insufficient; one ambiguous or inconsistent attempt blocks the entire planned batch.

Use `--expectation negative-control` for close, minimize/restore, hide/unhide, and Space-transition batches. Those pass only when all five repetitions remain excluded and never become gate-eligible.

Even a gate pass authorizes only a later reviewed matcher proposal. Repeat the focused corpus on macOS 27.2 before proposing production identity mutation.
