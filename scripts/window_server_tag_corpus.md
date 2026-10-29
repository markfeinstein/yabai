# WindowServer tag corpus and classifier comparison

This developer-only tooling records strict-redacted WindowServer metadata and evaluates the evidence gate offline. It never changes daemon behavior, and production inventory continues to use `legacy-space-eligibility-v1`.

## Build

```sh
make window-server-tag-corpus
make classifier-corpus-gate
```

The gate target runs a synthetic pass/fail self-test. It does not validate live evidence.

## Capture

Arrange one controlled state, allow it to become quiescent, and capture it at least twice with distinct repeat indexes:

```sh
revision=$(git rev-parse --short=12 HEAD)
bin/debug/window-server-tag-corpus \
  --scenario appkit-standard \
  --capture-set appkit-standard-27.0 \
  --repeat-index 1 \
  --source-revision "$revision" \
  --ax-mode auto \
  --separate-spaces yes \
  --stage-manager off \
  --output /private/tmp/appkit-standard-r1.jsonl \
  --compare-classifier \
  --labels /private/tmp/appkit-standard-r1.labels.json
```

Use `--ax-mode unavailable` to force the same-state conservative control without changing TCC. `auto` uses Accessibility only when it is already trusted. Never trigger a permission prompt for a corpus.

`--separate-spaces` accepts `yes`, `no`, or `unknown`; `--stage-manager` accepts `on`, `off`, or `unknown`. Display count is observed automatically. These fields are provenance, not selectors.

The recorder:

- uses one public CG window list and one immutable WindowServer snapshot;
- does not contact the yabai daemon;
- never requests Accessibility or Screen Recording permission;
- distinguishes forced/unavailable AX, inventory errors, role errors, no-match, and matched records;
- does not move, focus, resize, order, minimize, fullscreen, create, or destroy windows;
- creates output with `O_EXCL|O_NOFOLLOW` and mode `0600`;
- emits no raw window IDs, PIDs, titles, application names, bundle IDs, paths, Space/display identities, or absolute geometry.

Keep generated corpora and label files outside repositories and delete them after analysis. Do not commit them without an explicit privacy review.

## Labels and adjudication

Label files are local, mode `0600`, non-symlink JSON files. Raw IDs exist only in this private input and never appear in strict-redacted output. To create a private starter file without titles or application identity, use `--label-template /private/tmp/state.labels.json` instead of `--labels`; the command fails rather than overwriting an existing path. Edit the placeholders, then capture again with `--labels`.

```json
{
  "schema": "yabai.window-server-tag-labels/v2",
  "windows": [
    {
      "window_id": 123,
      "fixture_id": "document-1",
      "fixture_role": "primary-document",
      "expected_normal": "manageable",
      "expected_include_minimized": "manageable",
      "adjudication_status": "confirmed",
      "adjudication_reason": "controlled-standard-document",
      "evidence_ref": "operator-row-1",
      "bit58_state": "not-applicable"
    }
  ]
}
```

`fixture_id`, reasons, evidence references, and control-pair IDs are opaque safe tokens containing only letters, digits, `.`, `_`, or `-`. They must not encode application identity, titles, paths, raw IDs, or other sensitive values.

Adjudication status is:

- `pending`: reason and evidence reference must be absent;
- `confirmed`: the controlled fixture explains the expected result or disagreement;
- `rejected`: the row is proven not to represent the claimed fixture.

A labeled disagreement remains a gate blocker while pending. Merely assigning a fixture role does not adjudicate it.

Expected classes are `manageable`, `user-facing-nonmanageable`, `system-overlay`, or `uncertain`.

Allowed fixture roles are:

- `primary-document`
- `root-dialog`
- `child-sheet`
- `floating-panel`
- `menu-negative`
- `overlay-negative`
- `menu-extra-negative`
- `dock-negative`
- `system-overlay-negative`
- `electron-main`
- `electron-helper`
- `fullscreen`
- `split-view`
- `minimized`
- `restored`
- `stage-manager-off`
- `stage-manager-on`
- `stage-manager-shelf`
- `native-tab-active`
- `native-tab-inactive`
- `native-tab-detached`
- `native-tab-bar`
- `bit58-ordinary`

### Bit-58 controls

`bit58-ordinary` labels additionally require the same safe `control_pair_id` in paired captures and a `bit58_state` of `clear` or `set`. The recorder rejects a state that disagrees with the captured tag. The offline gate requires both states and identical candidate classes; bit 58 remains diagnostic and is never validity evidence.

All other roles require `bit58_state: "not-applicable"` and no control-pair ID.

## Disagreement and AX profiles

Comparison headers retain aggregate class, comparison, false-positive/negative, AX, and disagreement-profile counts. Each window row contains:

- source-safe fixture and adjudication metadata when labeled;
- legacy and candidate results for both modes;
- controlled reason lists;
- AX classifier status and separate observation status;
- redacted owner, parent, title, frame, level, tag, and attribute categories.

AX inventory and role read failures are classified conservatively as unavailable for the candidate rather than being conflated with a successful no-match. Forced-unavailable evidence on 27.0.1 showed that root dialogs and inactive native tabs can be indistinguishable from ordinary documents using the current WindowServer facts. The candidate therefore keeps all untracked AX-unavailable documents `uncertain`; only already-tracked windows retain manageable status. `no_ax_document` remains a diagnostic phenotype and is accompanied by `ax_unavailable_conservative`.

## Offline evidence gate

Run the validator only on reviewed strict-redacted v2 comparison files:

```sh
python3 scripts/window_server_classifier_gate.py /private/tmp/corpus/*.jsonl
```

It exits nonzero and emits a source-safe blocker list until all of the following are true:

- macOS 26.6, 27.0.x, and 27.2 are present;
- every required role is covered on every required OS;
- every capture set and fixture has at least two stable repeats;
- both `auto` and forced `unavailable` AX modes exist per OS, with matched AX evidence in `auto`;
- labeled candidate-manageable false positives are zero;
- every disagreement is labeled, explained, and non-pending;
- required positives are confirmed and found;
- bit-58 clear/set pairs are classification-invariant;
- Stage Manager off/on and multi-display Separate Spaces evidence exist;
- tag-layout status is sampled for each required build;
- all files use one reviewed source revision;
- activation is still diagnostic-only with legacy fallback and explicit rollback metadata.

A gate pass is evidence for a later review; it does not enable the classifier. Activation still requires a separate, version-gated production PR with explicit fallback and rollback behavior.

## Current external blockers

The current host is macOS 27.0.1; the external display used for the earlier dual-display rows is not currently connected. No suitable macOS 26.6 or 27.2 VM is installed, and no self-hosted live-integration runner is registered. Do not create or download a VM merely to satisfy this matrix.
