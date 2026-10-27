# Native-tab event recorder

This opt-in developer tool records read-only AX, WindowServer, workspace, Space, and Mission Control evidence for one already-running application. It does not launch, activate, focus, tab, detach, move, resize, minimize, or close windows. It is not connected to the yabai daemon and does not alter production window lifecycle handling.

```sh
make native-tab-event-recorder
bin/debug/native-tab-event-recorder \
  --pid PID \
  --scenario active-to-inactive \
  --duration 15 \
  --output /private/tmp/native-tabs.jsonl
```

The output path must not exist. The recorder creates it with mode `0600`, rejects symlinks and existing paths, and removes partial output on failure.

## Permission policy

The recorder checks existing Accessibility trust with `AXIsProcessTrusted()`. It never uses the prompting API. If Accessibility is not already trusted, it records `ax_available:false` and continues with reduced WindowServer/workspace evidence. Do not grant permissions merely to run the tool.

WindowServer notifications observed by the tool include visible (`815`), invisible (`816`), destroyed (`804`), and Mission Control enter (`1204`). Every inventory and notification subscription is bounded to 256 target-process windows.

## Timing and interpretation

Version 1 uses fixed diagnostic timing:

- inventory polling: 100 ms;
- normal boundary: two consecutive complete, unchanged inventories (200 ms quiescence);
- hard terminal readback: 1000 ms after the first event when quiescence is not established;
- appeared/disappeared capacity: 8 IDs per episode.

A single unchanged poll does not close an episode, so a split remove/add transition remains together. Once two unchanged polls close it, a later inventory delta starts a new episode instead of contaminating the earlier cardinality. Incomplete inventories neither establish quiescence nor replace the authoritative complete inventory.

The recorder tracks redacted in-run presence history. `known_id_reappeared:true` means an ordinal observed in an earlier complete inventory was absent and later appeared again; it is evidence about WindowServer ID presence only, not a dormant-tab alias or proof of logical identity. A remove/add of the same ID inside one episode retains the `same-id-reappeared` outcome.

An event exactly at the hard deadline still participates. Events after it begin a new episode. Outcomes such as `unexplained-one-to-one`, `appearance-only`, and `same-id-reappeared` describe event cardinality only. They are not identity matches and never change daemon state.

## Strict redaction

JSONL uses schema `yabai.native-tab-event-trace/v1` and `strict-v1` redaction. It never serializes raw PID, WindowServer ID, Space ID, application name, bundle ID, executable path, title text, AX descriptions, absolute frame coordinates, or wall-clock timestamps.

Per-run ordinals replace identities. Titles become equality-only in-memory tokens and are serialized only as `same_title_token`. Frames appear only as old/new deltas quantized in 8-point units.

## Suggested manual scenarios

Run a separate trace for each controlled fixture:

- selected tab A to selected tab B;
- B back to A;
- inactive tab detached into its own window;
- active tab detached;
- minimized and restored;
- ordinary close;
- app hide/unhide;
- native tabs with focus changes.

The operator or a dedicated fixture application performs the actions. The recorder itself remains read-only. Preserve traces outside the repository, inspect only redacted JSONL, and delete them after analysis.

A later PR may propose transactional identity replacement only after the recorded event order and lifecycle explanations are adjudicated. This tool does not add `window_manager_replace_window_identity`, dormant-tab aliases, or a `window_replaced` signal.
