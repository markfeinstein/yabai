# Window corner geometry probe

This standalone probe is the only code path that calls private corner getters. Production collection and proxy rendering remain hard-disabled.

```sh
make window-corner-geometry-probe
revision=$(git rev-parse --short=12 HEAD)
bin/debug/window-corner-geometry-probe \
  --source-revision "$revision" \
  --fixture-revision controlled-v3
```

The JSONL schema is `yabai.window-corner-geometry-probe/v3`. Output contains fixture labels, phases, cycles, dimensions, uninterpreted slots/flags, and compatibility metadata, but no raw window IDs, PIDs, titles, application identity, paths, Space IDs, or display IDs.

## ABI guard

On macOS 27.0.1 build 26A434, arm64 disassembly validated these one-argument iterator getters:

- `SLSWindowIteratorGetCornerRadii` → newly created `CFArrayRef`;
- `SLSWindowIteratorGetResolvedCornerRadii` → newly created `CFArrayRef`;
- `SLSWindowIteratorGetCornerMaskFlags` → `uint32_t`.

The v3 probe calls raw, resolved, and mask-flags only on that exact build. On any other build it reports symbol presence and `abi: unverified` without invoking them. Every observation records the iterator window before/after the call sequence, performs repeated calls, and releases returned arrays under the verified +1 ownership contract.

`SLSCopyWindowCornerMask` remains `abi_state: partial` and `called:false`. No private setter symbol is resolved or called.

## Fixtures and repetitions

The controlled set includes:

- standard titled window;
- opaque borderless square control;
- five public content-layer mask/corner-radius negative controls;
- clamped small content-layer control;
- utility panel;
- attached sheet.

Content-layer fixtures are explicitly labeled `expected_windowserver_effect: none`; public `CACornerMask` does not establish WindowServer slot order.

The probe runs:

- five steady batches for every fixture;
- five resize cycles over baseline, half-width, half-height, small size, and restored size for titled and borderless controls;
- back-to-back raw/resolved/mask calls per observation.

Fullscreen and native tile transitions are not automated on the live host because they require a disposable VM/dedicated-account workflow and previously disturbed the active Space. Their counters remain zero.

## Current macOS 27.0.1 result

The controlled v3 corpus produced 100 observations:

- raw: 100/100 four-number finite nonnegative arrays;
- resolved: 100/100 four-number finite nonnegative arrays;
- raw/resolved repeated calls returned different pointers with stable values;
- iterator identity remained stable for all 100 call sequences;
- mask flags were stably `0x0000000f` for all 100 observations;
- no resolved observation was asymmetric;
- physical slot order therefore remains `unknown`.

This strengthens the ABI and ownership evidence, but does not establish mask-bit semantics or physical slot ordering.

## Compatibility and policy

Run records include OS/build, process/host architecture, Rosetta state, pointer/CGFloat width, compiler, SDK max-allowed macro, deployment target, symbol presence, source/fixture revisions, and per-symbol ABI state.

Actual runtime/ABI rows are still missing for macOS 26.6 and 27.2. The installed SDK set also lacks 27.2. Do not infer those rows from 26.5/27.0 exports.

The following remain unchanged:

- `window_corner_geometry_production_collection_enabled = false`;
- `window_corner_geometry_production_rendering_enabled = false`;
- no private corner setter;
- no production proxy behavior change.

Any later cosmetic rendering policy must still require independently proven physical slot order across asymmetric fixtures on 26.6 and 27.2, default to uniform-only, clamp to half the proxy dimension, affect only yabai-owned proxies, and fall back to rectangular rendering.
