# WindowServer corner-geometry probe

This opt-in developer tool validates the read-only `SLSWindowIteratorGetResolvedCornerRadii` getter and the inert normalization contract. It never contacts the yabai daemon and performs no WindowServer mutation.

```sh
make window-corner-geometry-probe
bin/debug/window-corner-geometry-probe --limit 200 --samples 24
bin/debug/window-corner-geometry-probe --fixtures
```

Options:

- `--limit N` bounds ambient getter calls from 1 through 10,000. The default is 200.
- `--samples N` bounds detailed redacted ambient sample records from 0 through 100. The default is 24.
- `--fixtures` creates eight temporary probe-owned AppKit windows, observes them, and closes them. It does not activate the probe or alter operator windows.

Fixture mode includes a standard titled window, an opaque borderless control, a transparent uniform-radius layer, four one-corner `CACornerMask` variants, and a small window whose public layer radius exceeds half its minimum dimension. Public layer transactions are flushed before querying exact probe-owned window numbers.

Schema v2 reports:

- OS version, build, architecture, and ambient/fixture mode;
- availability of the resolved/raw radii, mask-flags, and mask-copy symbols;
- the deliberately disabled production collection and proxy-rendering states;
- result CF type, array count, source-order numeric slots, uniform/asymmetric classification with 0.01 tolerance, frame dimensions, and getter latency;
- fixture labels plus whether an immediate repeated getter call returns the same pointer and equivalent normalized values;
- aggregate count/type/validity totals and p50/p95 getter latency.

Raw WindowServer IDs, owners, titles, bundle identifiers, absolute positions, and window contents are omitted. Repeated pointer identity is an observation, not an ownership contract.

## macOS 27.0.1 ABI ledger

Read-only arm64 disassembly on build 26A434 establishes:

- resolved and raw iterator getters take the iterator as their only argument, create a four-element CFArray, and return it at +1 ownership;
- mask-flags takes the iterator and returns a 32-bit value;
- `SLSCopyWindowCornerMask` takes connection, window ID, and a 32-byte geometry output pointer, but its returned object/mask representation remains insufficiently identified for use;
- singular and system-radius transaction setters take transaction, window ID, and one double;
- masked-corners takes transaction, window ID, and an integer mask;
- the macOS-27 plural radii setter takes transaction, window ID, and four doubles in scalar floating-point argument registers—not a CFArray;
- the corner-mask transaction setter has additional object, flag, and four-double inputs and remains disabled.

No setter is called by this probe.

A valid observation requires exactly four finite, nonnegative numeric values. Source order remains intentionally opaque until an independently correlated mask/getter ABI proves physical corner names.

Fixture mode on build 26A434 produced eight valid, uniform observations. The standard titled window reported four 16-point values; the opaque borderless and all transparent public-layer fixtures reported four zeros. One-corner `CACornerMask` variants and a 60-point layer radius on an 80-point window did not change WindowServer metadata. Immediate repeated calls returned equivalent values but distinct array pointers. Public content-layer masks are therefore not ground truth for WindowServer slot ordering.

## Policy

Production collection and proxy rendering remain disabled. If physical ordering and mask semantics remain unresolved, the safe recommendation is uniform-only: accept four valid values, use them only when all four agree within tolerance, clamp the cosmetic proxy radius to half its minimum dimension, and otherwise preserve the rectangular fallback. Never mutate application windows. Setter calls require a separate explicitly authorized experiment; symbol availability or static ABI analysis is insufficient.
