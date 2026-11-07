#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
baseline="$root/scripts/analyzer-baseline.txt"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/yabai-analyze.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

arch=$(uname -m)
case "$arch" in
    arm64|x86_64) ;;
    *) echo "unsupported analyzer architecture: $arch" >&2; exit 1 ;;
esac

xcrun_bin=${XCRUN:-xcrun}
if ! "$xcrun_bin" clang --analyze \
    -Xanalyzer -analyzer-opt-analyze-headers \
    -Xanalyzer -analyzer-output=plist-multi-file \
    "$root/src/manifest.m" \
    -fno-objc-arc -arch "$arch" -mmacosx-version-min=14.0 \
    -F/System/Library/PrivateFrameworks \
    -o "$tmp/report.plist" >"$tmp/analyzer.log" 2>&1; then
    cat "$tmp/analyzer.log" >&2
    exit 1
fi

"${PYTHON:-python3}" "$root/scripts/analyzer_signatures.py" \
    "$root" "$tmp/report.plist" >"$tmp/current.txt"

if [ ! -f "$baseline" ]; then
    echo "missing analyzer baseline: $baseline" >&2
    exit 1
fi

comm -23 "$tmp/current.txt" "$baseline" >"$tmp/new.txt"
comm -13 "$tmp/current.txt" "$baseline" >"$tmp/resolved.txt"
if [ -s "$tmp/new.txt" ]; then
    echo "new project static-analyzer diagnostics:" >&2
    cat "$tmp/new.txt" >&2
    echo "full analyzer output: $tmp/analyzer.log" >&2
    trap - EXIT HUP INT TERM
    exit 1
fi

current=$(wc -l <"$tmp/current.txt" | tr -d ' ')
resolved=$(wc -l <"$tmp/resolved.txt" | tr -d ' ')
echo "static analyzer passed: $current known diagnostics; $resolved baseline diagnostics resolved"
