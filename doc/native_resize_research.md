# Native BSP live-resize investigation

This note records the controlled evidence used to choose the native BSP resize architecture. All live experiments used disposable helper windows. Logs contained only case labels, counts, timings, error codes, and geometry deltas; they omitted application names, titles, window identifiers, and filesystem paths.

## Safety and scope

- Full SIP remained enabled and the scripting addition remained unloaded.
- Accessibility and ScreenCaptureKit preflights were already authorized. No TCC request API was called and no prompt appeared.
- Private WindowServer mutations were separately approved for disposable fixtures only.
- Every private mutation read the original state first, restored it, verified restoration, and closed the helper.
- No production service was restarted or activated.

`make native-resize-pipeline-probe` builds the opt-in AppKit fixture used for raw-event, Accessibility-notification, and WindowServer-observation timing. It creates floating helper windows, generates a bounded synthetic native edge drag, restores the pointer, and exits. It is never part of `make test` or service startup.

## Event pipeline

A 120-event, one-second native right-edge drag on the AppKit fixture produced:

| Stage | Observed updates | Median interval | 95th percentile |
| --- | ---: | ---: | ---: |
| Posted drag events | 120 | 8.333 ms target | — |
| AppKit raw drag events | 120 | 8.333 ms | 8.347 ms |
| AX resized notifications | 119 | 8.321 ms | 8.518 ms |
| WindowServer bounds changes | 120 | 8.310 ms | 9.373 ms |
| Event post to AppKit receipt | 120 | 4.708 ms | 5.754 ms |
| AppKit receipt to next WS change | 120 | 7.335 ms | 7.844 ms |

A disposable Electron fixture showed the same order of magnitude: 118 AX resize notifications and 119 WindowServer changes, with 8.286/9.484 ms AX and 8.216/9.923 ms WindowServer median/95th-percentile intervals. The native input and observation pipeline can therefore carry approximately 120 Hz on this host.

## Accessibility write path

For responsive disposable windows, the AX call itself was usually not the long pole:

| Fixture and operation | AX call median | AX call p95 | WS target median | WS target p95 |
| --- | ---: | ---: | ---: | ---: |
| AppKit position | 0.131 ms | 0.253 ms | 8.328 ms | 9.772 ms |
| AppKit size | 0.533 ms | 0.775 ms | 8.284 ms | 9.195 ms |
| AppKit position then size | 0.620 ms | 0.845 ms | 8.222 ms | 9.539 ms |
| Electron size then position | 0.735 ms | 0.843 ms | 8.512 ms | 10.253 ms |
| Electron position then size | 0.809 ms | 3.880 ms | 10.021 ms | 16.302 ms |

The robust size-position-size sequence had higher tail cost: 1.187 ms median / 4.334 ms p95 for the calls and 8.486 ms median / 14.982 ms p95 to WindowServer on AppKit.

ScreenCaptureKit showed the harder visible limit. In the best controlled Electron runs, AX-driven boundary changes produced 87–107 visible changes for 120 requested targets, with visible-change p95 intervals around 17–19 ms. This matches the live result in which raising the production cap from 60 Hz to 120 Hz did not improve perceived sibling motion.

`AXFrame` (`"AXFrame"`) was not settable (`kAXErrorAttributeUnsupported`) on either AppKit or Electron fixtures. Neither a public nor private multi-attribute AX setter symbol was exported. The public multiple-attribute API is read-only.

### Messaging timeout semantics

Lowering an element's AX messaging timeout did not improve responsive-fixture latency. A deliberately blocked disposable AppKit fixture demonstrated why aggressive timeouts are unsafe: with a 1 ms timeout, the setter returned `kAXErrorCannotComplete` after 1.271 ms, but the requested size still committed 11.862 ms later. A timeout is therefore an unknown outcome, not proof that no mutation occurred; automatically retrying it can apply stale geometry after a newer target.

## Concurrency and coalescing

Independent fixture processes were driven with position-plus-size targets at 120 Hz.

For eight windows, sequential writes consumed 5.620 ms median, 8.345 ms p95, and 56.366 ms maximum, exceeding the 8.333 ms frame budget seven times. A dedicated latest-target worker per window reduced producer enqueue cost to 0.012 ms median / 0.028 ms p95 / 0.066 ms maximum. The workers applied 956 of 960 generated targets, coalesced 20 stale targets, and verified the exact final integer frames after a 10.758 ms release barrier.

This design keeps the event loop responsive and bounds stale work, but it does not change the per-window compositor/application visible-update limit measured above.

## Active-window highlight

The installed highlight was measured from WindowServer bounds while a focused disposable Electron window was resized through AX.

- Size-only: 118 real-window changes versus 70 highlight changes.
- Position-plus-size: 137 real-window changes versus 68 highlight changes.
- Highlight median intervals were 16.6–16.8 ms, with roughly 21 ms p95.
- Only 58–69 real-window frames acquired an exactly matching highlight frame in the sampled runs.

The border must therefore be part of the live presentation transaction. Refreshing the existing shape/drawing path after AX notifications cannot keep it synchronized at 120 Hz.

## Private API experiments

### Cross-process move

`SLSMoveWindow` succeeded for both AppKit and Electron fixtures, and the original position was restored and verified. However, the AX position remained unchanged. Combining a WindowServer move with an AX size write caused the WindowServer origin to return to the application's AX origin, regardless of call order. It is not a usable frame primitive and would produce pointer fighting or snap-back.

### Cross-process transform

The prior disposable experiment found that `SLSSetWindowTransform` returned success but immediate readback remained unchanged for a foreign-owned window. Transaction transforms did not produce the requested target either. Core Yabai cannot depend on foreign-window transforms under full SIP.

### Locked bounds

The exact `SLSTransactionSetWindowLockedBounds(transaction, wid, CGRect)` ABI was corroborated by open-source target-Mac fixtures before invocation. It worked and restored correctly for a same-process disposable window. The same transaction against foreign AppKit and Electron fixtures left their public bounds unchanged while preserving the native anchor. Bounds locking is ownership-restricted in the configuration relevant to core Yabai.

### Yabai-owned transforms

Direct transforms on a disposable process-owned overlay returned in 0.003 ms and read back exactly. A run-loop-serviced overlay driven through a command pipe produced 85 detected boundary changes out of approximately 90 observable two-pixel steps, with 9.959 ms median visible-step spacing. Transaction transforms are therefore suitable for Yabai-owned presentation surfaces even though they are not available for foreign application windows.

## Accurate proxy feasibility

`SLSHWCaptureWindowList` returned exact 2x images for both disposable AppKit and Electron windows. Repeated single-window capture cost was:

| Fixture | Median | p95 |
| --- | ---: | ---: |
| AppKit | 7.771 ms | 10.206 ms |
| Electron | 5.922 ms | 8.086 ms |

Capture can occur once, in parallel, when the gesture is claimed. Existing Yabai animation code already proves the image-to-overlay path and transaction-transform machinery. This established technical feasibility for a captured-content proxy experiment, but not product acceptability as genuine live window motion.

## Architecture decision

Genuinely smooth foreign-window frame mutation is not available under the required full-SIP/no-scripting-addition constraints:

1. AX is the only validated size writer.
2. Its visible tail cadence misses the 120 Hz target even when call latency is low.
3. Worker concurrency prevents event-loop stalls but cannot improve that visible limit.
4. Foreign WindowServer transforms and locked bounds are ignored.
5. WindowServer-only position changes diverge from application AX state and are undone by size changes.
6. Aggressive AX timeouts have ambiguous late-commit semantics.

An accurate captured-content proxy was implemented with explicit approval and exercised through the real daemon. The live result was rejected: it was visibly a substitute rather than a live resize, the focused border lagged, and a glitchy edge indicator remained visible during the gesture. The earlier divider preview was already rejected for the same product-level reason.

The final stable architecture is therefore release-only for ordinary native edge and corner drags. macOS owns the grabbed window during the gesture; Yabai observes and validates the final geometry, updates BSP ratios once after mouse-up, and normalizes the real windows exactly. `mouse_bsp_live_resize` remains available only for Yabai's modifier-assisted resize gesture. The measured API limits and rejected proxy evidence are retained here so future work does not repeat these approaches without a genuinely new primitive.
