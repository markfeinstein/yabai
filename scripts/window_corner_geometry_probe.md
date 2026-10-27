# WindowServer corner-geometry probe

This opt-in developer tool validates the read-only `SLSWindowIteratorGetResolvedCornerRadii` getter and the inert normalization contract. It never contacts the yabai daemon, creates no windows, and performs no WindowServer mutation.

```sh
make window-corner-geometry-probe
bin/debug/window-corner-geometry-probe --limit 200 --samples 24
```

Options:

- `--limit N` bounds getter calls from 1 through 10,000. The default is 200.
- `--samples N` bounds detailed redacted sample records from 0 through 100. The default is 24.

Output is one JSON object. It reports:

- OS version, build, and architecture;
- availability of the resolved/raw radii, mask-flags, and mask-copy symbols;
- the deliberately disabled production collection and proxy-rendering states;
- result CF type, array count, per-slot CF type/value, equality/asymmetry, frame dimensions, and getter latency for bounded samples;
- aggregate count/type/validity totals and p50/p95 getter latency.

Raw WindowServer IDs, owners, titles, bundle identifiers, and window contents are omitted. Getter results are consumed only while the iterator is alive and are not released because ownership is unresolved.

A valid observation currently requires exactly four finite, nonnegative numeric values. Their source order is intentionally opaque: the probe does not name corners or infer mask semantics. Zero is valid. Production capture and rendering remain disabled until getter ownership, corner ordering, mask semantics, setter ABI, and proxy-animation behavior are validated on the required OS corpus and in a disposable environment.
