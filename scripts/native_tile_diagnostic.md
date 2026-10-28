# Native tile membership diagnostic

`native-tile-diagnostic` performs a read-only WindowManagement bridge query. It does not create, destroy, focus, resize, or reorder Spaces or windows.

```sh
make native-tile-diagnostic
bin/debug/native-tile-diagnostic
```

The aggregate-only output reports managed displays/Spaces, parent Spaces with tile membership, tile Space count, failed queries, and ABI/capability status without IDs, application identity, titles, paths, or geometry.

## Current macOS 27.0.1 physical fixture

A real user-created Split View was prepared and later restored manually. The read-only diagnostic completed successfully but reported:

- 2 managed-display records;
- 10 managed Spaces queried;
- 0 tiled parent Spaces;
- 0 tile Spaces;
- 0 failed membership queries.

The zero-membership result conflicts with the visible fixture and therefore does not establish that `SLSBridgedSpaceCopyTileSpacesOperation` models the currently presented Split View. It may distinguish fullscreen Split View from newer window tiling, use another parent/child representation, or require additional read-only operations.

Static inspection on the same build found read-only operation classes and result types for:

- tile-space divider directions;
- window tile rect;
- can-create-tile feasibility;
- tile-space membership.

Their runtime semantics and exact ABI remain research-gated. Do not add mutation commands or production tile ownership from the current result.

## Physical accessibility acceptance

The native menu passed keyboard navigation and open/close acceptance without hanging. Highlight visibility passed manual checks under Reduce Transparency, Increase Contrast, and Differentiate Without Color, with preferences restored afterward. VoiceOver labels/order were not tested and remain an external manual blocker.

## Remaining gates

- explain the visible-fixture/zero-membership disagreement;
- add safe runtime metadata and bounded read-only calls for divider directions, tile rect, and feasibility;
- repeat on macOS 26.6 and 27.2;
- validate real parent/child semantics before any separate tile-command proposal.
