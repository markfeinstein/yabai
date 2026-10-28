# macOS research environment matrix

This ledger records available environments and exact external blockers. It is not a production compatibility claim.

## Current physical host

- macOS 27.0.1, build 26A434
- arm64, Mac17,8, Apple M5 Pro, 64 GiB
- interactive WindowServer session
- selected SDK 27.0 build 26A425 from Command Line Tools
- no Xcode application and no SDK 27.2
- deployed yabai v7.1.25, arm64, macOS 14.0 floor
- scripting addition absent; core SIP remains enabled
- doctor: zero failures

## Current display topology

Two active, online, non-mirrored 2× displays are currently available:

| Ordinal | Kind | Main | Frame/mode points | Backing pixels | Refresh |
|---|---|---:|---:|---:|---:|
| 0 | external | yes | 3008×1692 | 6016×3384 | fixed 60 Hz |
| 1 | built-in | no | 1728×1117 | 3456×2234 | 120 Hz reported |

This closes a real dual-display, same-scale, mixed-60/120-Hz evidence row. It does not provide mixed 1×/2× scale or the roadmap’s exact 60/144-Hz fixture.

The deployed active highlight passed one focused existing-window check on each display. The geometry v3 corpus changed from all same-window on the single-display arrangement to six same-window and four stable different-owner disagreements after the second display was connected.

## Existing virtual environments

No suitable macOS VM bundle or virtualization application is installed. The only discovered VM is an aarch64 Linux Lima guest, which is not suitable for macOS private-API validation. No self-hosted live-integration runner is registered.

Do not create or download a VM merely to fill this matrix.

## Required version rows

| Required row | Status | Blocker |
|---|---|---|
| macOS 26.6 arm64 | BLOCKED-ENV | no existing suitable host/VM |
| macOS 27.0.x arm64 | PARTIAL-PASS | current 27.0.1 physical host; source-safe diagnostics completed |
| macOS 27.2 arm64 | BLOCKED-ENV | no existing suitable host/VM and no 27.2 SDK |
| live Intel/x86_64 | BLOCKED-ENV | cross-build only; no supported live Intel host |

## CADisplayLink/per-display animation gate

Per-display CADisplayLink work remains blocked even though mixed 60/120-Hz physical displays are now connected:

- production is still on the CV clock backend;
- no authorized real proxy-animation experiment was run;
- Reduce Motion was not disabled for an animation experiment;
- scripting-addition prerequisites were not authorized;
- no mixed-scale fixture exists;
- no macOS 26.6 or 27.2 environment exists.

Synthetic 60/120/144-Hz tests and offline macOS 14 CI remain source evidence only. They do not satisfy physical cadence, cross-display handoff, UI-stall, or proxy-cleanup acceptance.
