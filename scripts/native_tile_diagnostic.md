# Native tile membership diagnostic

`native-tile-diagnostic` performs a read-only WindowManagement bridge query. It does not create, destroy, focus, resize, or reorder Spaces or windows.

```sh
make native-tile-diagnostic
bin/debug/native-tile-diagnostic
```

Schema v2 reports managed displays/Spaces, membership, divider-direction stability, window tile-rect availability, can-create feasibility, failed queries, and ABI/capability status without IDs, application identity, titles, paths, or raw geometry. All calls are synchronous read operations with exact runtime method-encoding checks; production tile commands remain disabled.

## Current macOS 27.0.1 physical fixture

A real user-created Split View was prepared and later restored manually. The read-only diagnostic completed successfully but reported:

- 2 managed-display records;
- 10 managed Spaces queried;
- 0 tiled parent Spaces;
- 0 tile Spaces;
- 0 failed membership queries.

The zero-membership result conflicts with the visible fixture and therefore does not establish that `SLSBridgedSpaceCopyTileSpacesOperation` models the currently presented Split View. It may distinguish fullscreen Split View from newer window tiling or use another parent/child representation.

On the current restored dual-display topology, v2 additionally observed:

- divider ABI verified; 20/20 successful calls, stable across 10 repeated Space pairs, all directions zero;
- tile-rect ABI verified; 14/14 successful calls, 9 nonempty rects;
- can-create ABI verified; 10/10 successful calls, all feasibility results false;
- membership remained 0 tiled parents / 0 tile Spaces with 0 failed queries.

The results are read-only evidence, not tile ownership semantics. Do not add mutation commands or production tile ownership from them.

## Physical accessibility acceptance

The native menu passed keyboard navigation and open/close acceptance without hanging. Highlight visibility passed manual checks under Reduce Transparency, Increase Contrast, and Differentiate Without Color, with preferences restored afterward. VoiceOver labels/order were not tested and remain an external manual blocker.

## Remaining gates

- explain the visible-fixture/zero-membership disagreement;
- explain why divider/membership/feasibility remain zero while ordinary windows return nonempty tile rects;
- repeat on macOS 26.6 and 27.2;
- validate real parent/child semantics before any separate tile-command proposal.
