#!/bin/sh
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd -P)
script=$repo/tests/integration/hide_on_close.sh

# Strict prepared-runner mode must reject a missing capability, while a local
# caller may explicitly request a zero-status unsupported-capability skip.
if YABAI_INTEGRATION_FORCE_UNSUPPORTED=fixture YABAI_INTEGRATION_STRICT=1 "$script" >/dev/null 2>&1; then
    echo 'strict integration accepted missing jq' >&2
    exit 1
fi
YABAI_INTEGRATION_FORCE_UNSUPPORTED=fixture YABAI_INTEGRATION_ALLOW_SKIP=1 "$script" >/dev/null

root=$(mktemp -d "${TMPDIR:-/tmp}/yabai-hide-fixture.XXXXXX")
cleanup() { find "$root" -mindepth 1 -delete; rmdir "$root"; }
trap cleanup 0 HUP INT TERM
mock=$root/mock
state=$root/state
mkdir "$mock" "$state"
expected=$(awk '/^#define MAJOR /{a=$3} /^#define MINOR /{b=$3} /^#define PATCH /{c=$3} END{printf "yabai-v%s.%s.%s",a,b,c}' "$repo/src/yabai.c")

cat > "$mock/yabai" <<'EOF'
#!/bin/sh
set -eu
case "$*" in
  --version) printf '%s\n' "$EXPECTED_VERSION" ;;
  '-m query --spaces --space') echo '{"type":"bsp"}' ;;
  '-m query --daemon-identity') printf '{"pid":4242,"architecture":"arm64","uuid":"%s"}\n' "${DAEMON_UUID:-$BUILT_UUID}" ;;
  '-m query --windows --window') echo '{"id":55}' ;;
  '-m query --windows --window 55') echo '{"id":55}' ;;
  '-m query --windows --space') echo '[]' ;;
  '-m window --focus 55') echo focus-55 >> "$STATE/log" ;;
  *) exit 1 ;;
esac
EOF
cat > "$mock/jq" <<'EOF'
#!/bin/sh
set -eu
first=${1:-}; args="$*"; cat >/dev/null
case "$args" in
  *'.type'*) echo bsp ;;
  *'.pid'*) echo 4242 ;;
  *'.architecture'*) echo arm64 ;;
  *'.uuid'*) printf '%s\n' "${DAEMON_UUID:-$BUILT_UUID}" ;;
  *'any('*)
    id=
    for arg do case "$arg" in 101|202) id=$arg ;; esac; done
    if [ "$id" = 101 ]; then
      if [ -e "$STATE/closed" ] && [ "${LOSE_ANCHOR_AFTER_CLOSE:-0}" = 1 ]; then echo false; exit 0; else echo true; fi
    elif [ ! -e "$STATE/closed" ]; then echo true
    elif [ -e "$STATE/reshown" ]; then echo true
    elif [ "${FAIL_FALSE_PROBE:-0}" = 1 ]; then exit 9
    else
      echo false
      # Match jq -e: a successfully evaluated false/null result exits 1.
      case "$first" in -e*|-*e*) exit 1 ;; esac
    fi ;;
  *'.id'*) echo 55 ;;
  *) exit 1 ;;
esac
EOF
cat > "$mock/swiftc" <<'EOF'
#!/bin/sh
set -eu
out=
while [ $# -gt 0 ]; do [ "$1" = -o ] && { shift; out=$1; }; shift; done
cat > "$out" <<'APP'
#!/bin/sh
printf 'anchor 101\ntarget 202\n'
while [ ! -e "$STATE/closed" ]; do sleep 0.02; done
printf 'ordered out\n'
sleep 0.1
touch "$STATE/reshown"
printf 'ordered in\n'
sleep 30
APP
chmod +x "$out"
EOF
cat > "$mock/osascript" <<'EOF'
#!/bin/sh
set -eu
case "$*" in
  *'UI elements enabled'*) echo true ;;
  *'unix id of first application process'*) echo 777 ;;
  *'AXCloseButton'*) touch "$STATE/closed" ;;
  *'unix id is 777'*) [ "${OSASCRIPT_RESTORE_FAIL:-0}" != 1 ] || exit 9; echo frontmost-777 >> "$STATE/log" ;;
  *) exit 1 ;;
esac
EOF
cat > "$mock/pgrep" <<'EOF'
#!/bin/sh
echo 4242
EOF
cat > "$mock/lsof" <<'EOF'
#!/bin/sh
printf 'p4242\nftxt\nn%s\n' "${LSOF_TEXT_PATH:-$YABAI_BIN}"
EOF
cat > "$mock/dwarfdump" <<'EOF'
#!/bin/sh
printf 'UUID: %s (arm64) %s\n' "$BUILT_UUID" "$2"
EOF
chmod +x "$mock"/*

export STATE="$state" EXPECTED_VERSION="$expected" YABAI_BIN="$mock/yabai"
export BUILT_UUID=11111111-2222-3333-4444-555555555555

# Real `jq -r` exits zero for a literal false result. Prove this fixture would
# defeat a status-only assertion before relying on the integration assertion.
touch "$state/closed"
anchor_probe=$(printf '[]\n' | PATH="$mock:/usr/bin:/bin:/usr/sbin" LOSE_ANCHOR_AFTER_CLOSE=1 \
    "$mock/jq" -r --argjson id 101 'any(.[]; .id == $id)')
[ "$anchor_probe" = false ] || { echo 'false anchor probe did not succeed literally' >&2; exit 1; }
rm -f "$state/closed"

PATH="$mock:/usr/bin:/bin:/usr/sbin" YABAI_INTEGRATION_STRICT=1 "$script" >/dev/null
[ "$(grep -c '^focus-55$' "$state/log")" -eq 1 ]
[ "$(grep -c '^frontmost-777$' "$state/log")" -eq 1 ]
[ ! -e "$repo/tests/integration/bin/hide-on-close" ]
[ ! -d "$repo/tests/integration/bin" ]

rm -f "$state/closed" "$state/reshown"
if PATH="$mock:/usr/bin:/bin:/usr/sbin" LOSE_ANCHOR_AFTER_CLOSE=1 YABAI_INTEGRATION_STRICT=1 "$script" >/dev/null 2>&1; then
    echo 'integration accepted anchor membership loss after target close' >&2
    exit 1
fi

rm -f "$state/closed" "$state/reshown"
if PATH="$mock:/usr/bin:/bin:/usr/sbin" FAIL_FALSE_PROBE=1 YABAI_INTEGRATION_STRICT=1 "$script" >/dev/null 2>&1; then
    echo 'integration treated expected-false query errors as absence' >&2
    exit 1
fi

rm -f "$state/closed" "$state/reshown"
if PATH="$mock:/usr/bin:/bin:/usr/sbin" OSASCRIPT_RESTORE_FAIL=1 YABAI_INTEGRATION_STRICT=1 "$script" >/dev/null 2>&1; then
    echo 'integration ignored cleanup restoration failure' >&2
    exit 1
fi

rm -f "$state/closed" "$state/reshown"
if PATH="$mock:/usr/bin:/bin:/usr/sbin" DAEMON_UUID=AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE YABAI_INTEGRATION_STRICT=1 "$script" >/dev/null 2>&1; then
    echo 'integration accepted a stale daemon UUID with matching path digest' >&2
    exit 1
fi

cp "$mock/yabai" "$root/stale-yabai"
printf '\n# stale\n' >> "$root/stale-yabai"
if PATH="$mock:/usr/bin:/bin:/usr/sbin" LSOF_TEXT_PATH="$root/stale-yabai" YABAI_INTEGRATION_STRICT=1 "$script" >/dev/null 2>&1; then
    echo 'integration accepted a stale daemon executable' >&2
    exit 1
fi

echo 'hide-on-close shell fixture tests passed'
