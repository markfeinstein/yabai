# macOS research environment matrix

This ledger records available environments and exact external blockers. It is not a production compatibility claim.

## Earlier physical-host research snapshot

The following 27.0.1/SDK 27.0 inventory is historical, not the current OS/SDK inventory. See the dated native smoke evidence below for the newer host run.

- macOS 27.0.1, build 26A434
- arm64, Mac17,8, Apple M5 Pro, 64 GiB
- interactive WindowServer session
- selected SDK 27.0 build 26A425 from Command Line Tools
- no Xcode application and no SDK 27.2
- deployed yabai v7.1.25, arm64, macOS 14.0 floor
- scripting addition absent; core SIP remains enabled
- doctor: zero failures

## Earlier display-topology research snapshot

Two active, online, non-mirrored 2× displays are currently available:

| Ordinal | Kind | Main | Frame/mode points | Backing pixels | Refresh |
|---|---|---:|---:|---:|---:|
| 0 | external | yes | 3008×1692 | 6016×3384 | fixed 60 Hz |
| 1 | built-in | no | 1728×1117 | 3456×2234 | 120 Hz reported |

This closes a real dual-display, same-scale, mixed-60/120-Hz evidence row. It does not provide mixed 1×/2× scale or the roadmap’s exact 60/144-Hz fixture.

The deployed active highlight passed one focused existing-window check on each display. The geometry v3 corpus changed from all same-window on the single-display arrangement to six same-window and four stable different-owner disagreements after the second display was connected.

## Earlier virtual-environment inventory

No suitable macOS VM bundle or virtualization application is installed. The only discovered VM is an aarch64 Linux Lima guest, which is not suitable for macOS private-API validation. No self-hosted live-integration runner is registered.

Do not create or download a VM merely to fill this matrix.

## Required version rows

| Required row | Status | Blocker |
|---|---|---|
| macOS 26.6 arm64 | BLOCKED-ENV | no existing suitable host/VM |
| macOS 27.0.x arm64 | PARTIAL-PASS | current 27.0.1 physical host; source-safe diagnostics completed |
| macOS 27.2 arm64 | PARTIAL-PASS | 2026-11-04 native query smoke passed on 27.2 build 26B123; UI/animation acceptance not run |

## Native private-API smoke evidence (2026-11-04)

The new read-only smoke probe passed locally on macOS **27.2.0**, build **26B123**, loaded **arm64**, not translated: two managed displays, two current Spaces, and 15 queried/sampled windows. All eleven checks passed, including real managed-display/Space queries, the production snapshot iterator path, bounds, and ordered-state queries. Initial native execution exposed a JSON boolean encoding mismatch; after correcting it, the strict driver passed. No running daemon, permissions, windows, services, or scripting addition were changed.

The initial run was local working-tree evidence for the new probe. A repeat on the clean source revision `008cf1fac0bd86e4a2a6f42aa45f1d0d13fc2fbc` passed all eleven checks on the same native host with the same counts. Neither run was a dispatched self-hosted CI job. This does not supersede earlier feature-specific evidence or fill any UI/animation acceptance row.

Prepared-host availability was checked again on 2026-11-04: the read-only GitHub repository runner API for `markfeinstein/yabai` returned `total_count: 0`. The configured Mac SSH alias (`m5`) names this current 27.2 arm64 host, not an additional environment. No additional prepared Mac was identified in existing configuration or supplied for this task. The other native rows therefore remain NOT-RUN; no workflow was queued without a matching runner, and no machine was provisioned, downloaded, or reconfigured.

| Native smoke row | Evidence |
|---|---|
| macOS 26 arm64 | NOT-RUN; prepared native host required |
| macOS 27 arm64 | PASS on 27.2.0 / 26B123; local query smoke only |

Use the [manual native smoke workflow and setup instructions](../doc/testing.md) to add attributable per-host results. No suitable additional hosts or runner registration are implied by the workflow definition.

## CADisplayLink/per-display animation gate

Per-display CADisplayLink work remains blocked even though mixed 60/120-Hz physical displays are now connected:

- production is still on the CV clock backend;
- no authorized real proxy-animation experiment was run;
- Reduce Motion was not disabled for an animation experiment;
- scripting-addition prerequisites were not authorized;
- no mixed-scale fixture exists;
- no macOS 26.6 environment has been validated; the available 27.2 host has only query-smoke evidence, not animation acceptance.

Synthetic 60/120/144-Hz tests and offline CI remain source evidence only. They do not satisfy physical cadence, cross-display handoff, UI-stall, or proxy-cleanup acceptance.
