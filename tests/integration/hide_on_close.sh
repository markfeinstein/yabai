#!/usr/bin/env bash
# Explicit opt-in live integration test. It opens two windows, asks System
# Events to click the target close button, and temporarily changes focus only.
# It never loads the scripting addition or starts/stops services.
set -euo pipefail
cd "$(dirname "$0")"

allow_skip=${YABAI_INTEGRATION_ALLOW_SKIP:-0}
strict=${YABAI_INTEGRATION_STRICT:-0}
fail() { echo "FAIL hide-on-close: $*" >&2; exit 1; }
unsupported() {
    if [ "$strict" != 1 ] && [ "$allow_skip" = 1 ]; then
        echo "SKIP hide-on-close: $*"; exit 0
    fi
    fail "$*"
}
if [ -n "${YABAI_INTEGRATION_FORCE_UNSUPPORTED:-}" ]; then
    unsupported "$YABAI_INTEGRATION_FORCE_UNSUPPORTED"
fi
for tool in jq swiftc osascript lsof pgrep shasum dwarfdump; do
    command -v "$tool" >/dev/null 2>&1 || unsupported "$tool is required"
done

YABAI_BIN=${YABAI_BIN:-../../bin/debug/yabai}
[ -x "$YABAI_BIN" ] || fail "build the current debug binary with 'make debug'"
YABAI_BIN=$(cd "$(dirname "$YABAI_BIN")" && pwd -P)/$(basename "$YABAI_BIN")
expected=$(awk '/^#define MAJOR /{a=$3} /^#define MINOR /{b=$3} /^#define PATCH /{c=$3} END{printf "yabai-v%s.%s.%s",a,b,c}' ../../src/yabai.c)
actual=$($YABAI_BIN --version 2>/dev/null || true)
[ "$actual" = "$expected" ] || fail "binary is not the current source version ($actual != $expected)"
$YABAI_BIN -m query --spaces --space >/dev/null 2>&1 || fail "the current yabai daemon is unreachable"
layout=$($YABAI_BIN -m query --spaces --space | jq -er '.type') || fail "could not query current layout"
[ "$layout" = bsp ] || fail "current Space must use BSP layout (found $layout)"
ui_enabled=$(osascript -e 'tell application "System Events" to return UI elements enabled' 2>/dev/null || true)
[ "$ui_enabled" = true ] || unsupported "Accessibility/UI scripting permission is required"

daemon_pid_lines=$(pgrep -x yabai || true)
daemon_count=$(printf '%s\n' "$daemon_pid_lines" | awk 'NF { count++ } END { print count+0 }')
[ "$daemon_count" -eq 1 ] || fail "expected exactly one running yabai process, found $daemon_count"
daemon_pid=$(printf '%s\n' "$daemon_pid_lines" | awk 'NF { print; exit }')
daemon_identity=$($YABAI_BIN -m query --daemon-identity) || fail "daemon identity query failed"
identity_pid=$(printf '%s\n' "$daemon_identity" | jq -er '.pid') || fail "daemon identity PID is invalid"
daemon_arch=$(printf '%s\n' "$daemon_identity" | jq -er '.architecture') || fail "daemon identity architecture is invalid"
daemon_uuid=$(printf '%s\n' "$daemon_identity" | jq -er '.uuid') || fail "daemon identity UUID is invalid"
case "$identity_pid:$daemon_arch" in "$daemon_pid":arm64|"$daemon_pid":x86_64) ;; *) fail "daemon protocol identity is malformed or does not match PID $daemon_pid" ;; esac
printf '%s\n' "$daemon_uuid" | grep -Eq '^[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}$' || fail "daemon protocol UUID is malformed"
built_uuid_lines=$(dwarfdump --uuid "$YABAI_BIN" 2>/dev/null | awk -v architecture="$daemon_arch" '$0 ~ "\\(" architecture "\\)" { print $2 }')
built_uuid_count=$(printf '%s\n' "$built_uuid_lines" | awk 'NF { count++ } END { print count+0 }')
[ "$built_uuid_count" -eq 1 ] || fail "built binary has no unique $daemon_arch UUID"
built_uuid=$(printf '%s\n' "$built_uuid_lines" | awk 'NF { print; exit }')
[ "$daemon_uuid" = "$built_uuid" ] || fail "running daemon UUID does not match the built $daemon_arch slice"

daemon_text=$(lsof -a -p "$daemon_pid" -d txt -Fn 2>/dev/null | sed -n 's/^n//p' | head -1)
[ -n "$daemon_text" ] || fail "could not resolve daemon executable text path"
daemon_text=$(cd "$(dirname "$daemon_text")" 2>/dev/null && pwd -P)/$(basename "$daemon_text") || fail "could not canonicalize daemon executable"
[ -f "$daemon_text" ] || fail "daemon executable is not a regular file"
built_hash=$(shasum -a 256 "$YABAI_BIN" | awk '{print $1}')
daemon_hash=$(shasum -a 256 "$daemon_text" | awk '{print $1}')
[ "$daemon_hash" = "$built_hash" ] || fail "running daemon is not the just-built debug artifact"

original_frontmost=$(osascript -e 'tell application "System Events" to return unix id of first application process whose frontmost is true' 2>/dev/null) || fail "could not capture frontmost application"
original_focus=$($YABAI_BIN -m query --windows --window | jq -er '.id' 2>/dev/null || true)

fixture_parent=${TMPDIR:-/tmp}
fixture_parent=$(cd "$fixture_parent" && pwd -P) || fail "could not canonicalize temporary directory"
fixture_dir=$(mktemp -d "$fixture_parent/yabai-hide-on-close.XXXXXX") || fail "could not create private fixture directory"
out=$fixture_dir/output
fixture_binary=$fixture_dir/hide-on-close
pid=
cleanup() {
    local original_status=$? cleanup_status=0
    trap - EXIT HUP INT TERM
    set +e
    if [ -n "$pid" ]; then
        if kill -0 "$pid" 2>/dev/null; then kill "$pid" 2>/dev/null || cleanup_status=1; fi
        wait "$pid" 2>/dev/null
        wait_status=$?
        [ "$wait_status" -ne 127 ] || cleanup_status=1
    fi
    if [ -n "$original_focus" ] && $YABAI_BIN -m query --windows --window "$original_focus" >/dev/null 2>&1; then
        $YABAI_BIN -m window --focus "$original_focus" >/dev/null 2>&1 || cleanup_status=1
    fi
    osascript -e 'tell application "System Events" to set frontmost of first application process whose unix id is '"$original_frontmost"' to true' >/dev/null 2>&1 || cleanup_status=1
    case "$fixture_dir" in
        "$fixture_parent"/yabai-hide-on-close.*)
            if [ -d "$fixture_dir" ] && [ ! -L "$fixture_dir" ]; then
                find "$fixture_dir" -mindepth 1 -delete || cleanup_status=1
                rmdir "$fixture_dir" 2>/dev/null || cleanup_status=1
            else
                cleanup_status=1
            fi
            ;;
        *) cleanup_status=1 ;;
    esac
    if [ "$original_status" -ne 0 ]; then exit "$original_status"; fi
    exit "$cleanup_status"
}
trap cleanup EXIT HUP INT TERM
swiftc -O hide_on_close.swift -o "$fixture_binary"

"$fixture_binary" --reshow-after 4 --quit-after 30 > "$out" &
pid=$!
for _ in $(seq 80); do
    grep -q '^anchor ' "$out" && grep -q '^target ' "$out" && break
    sleep 0.1
done
anchor=$(awk '/^anchor /{print $2; exit}' "$out")
target=$(awk '/^target /{print $2; exit}' "$out")
[ -n "$anchor" ] && [ -n "$target" ] || fail "window IDs were not reported"

managed() {
    local id=$1
    $YABAI_BIN -m query --windows --space | jq -r --argjson id "$id" 'any(.[]; .id == $id and (."is-floating" | not))'
}
wait_managed() {
    local id=$1 want=$2 observed
    for _ in $(seq 50); do
        if observed=$(managed "$id" 2>/dev/null); then
            case "$observed" in
                true|false) [ "$observed" = "$want" ] && return 0 ;;
            esac
        fi
        sleep 0.1
    done
    return 1
}

wait_managed "$anchor" true || fail "persistent anchor was not managed"
wait_managed "$target" true || fail "target was not managed"
echo "yabai $actual, daemon $daemon_pid, anchor $anchor, target $target"

osascript -e 'tell application "System Events" to tell (first process whose unix id is '"$pid"')' \
          -e 'click (first button of window "hide-on-close-target" whose subrole is "AXCloseButton")' -e 'end tell' >/dev/null
wait_managed "$target" false || fail "ordered-out target remained managed"
wait_managed "$anchor" true || fail "anchor stopped being managed"
for _ in $(seq 80); do grep -q '^ordered in' "$out" && break; sleep 0.1; done
wait_managed "$target" true || fail "ordered-in target did not return to managed membership"
echo "PASS hide-on-close managed membership lifecycle"
