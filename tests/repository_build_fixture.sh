#!/bin/sh
set -eu

PHASE=resolve-root
PARENT=
cleanup()
{
    if [ -n "$PARENT" ] && [ ! -L "$PARENT" ] && [ -d "$PARENT" ]; then
        /usr/bin/find "$PARENT" -depth -delete
    fi
}
report_exit()
{
    status=$?
    trap - 0 HUP INT TERM
    if [ "$status" -ne 0 ]; then
        printf 'repository build fixture: phase "%s" failed with status %s\n' "$PHASE" "$status" >&2
    fi
    cleanup
    exit "$status"
}
trap report_exit 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd -P)
PHASE=allocate-parent
PARENT=$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/yabai-repository-parent.XXXXXX")
PHASE=prepare-fixture
TMP_PARENT="$PARENT/temporary path with spaces"
mkdir "$TMP_PARENT"
TMP_BASE=$(CDPATH= cd -- "$TMP_PARENT" && pwd -P)
TOOL_BASE=$(CDPATH= cd -- /tmp && pwd -P)
FAKE_MKTEMP="$PARENT/failing-mktemp"
ATTEMPTS="$PARENT/mktemp-attempts"
CREATED="$PARENT/mktemp-created"
FAILURE_LOG="$PARENT/failure.log"
SUCCESS_OUTPUT="$PARENT/success.out"
SUCCESS_TRACE="$PARENT/success.trace"
: > "$ATTEMPTS"
: > "$CREATED"

cat > "$FAKE_MKTEMP" <<'FIXTURE'
#!/bin/sh
set -eu

template=
for arg do template=$arg; done
printf '%s\n' "$template" >> "$MKTEMP_ATTEMPTS"
if [ "$template" = "$MKTEMP_FAIL_TEMPLATE" ]; then
    exit 72
fi
made=$(/usr/bin/mktemp "$@")
printf '%s\n' "$made" >> "$MKTEMP_CREATED"
printf '%s\n' "$made"
FIXTURE
chmod 0755 "$FAKE_MKTEMP"

PHASE=inject-tool-allocation-failure
set +e
TMPDIR="$TMP_BASE" \
REPOSITORY_MKTEMP="$FAKE_MKTEMP" \
MKTEMP_ATTEMPTS="$ATTEMPTS" \
MKTEMP_CREATED="$CREATED" \
MKTEMP_FAIL_TEMPLATE="$TOOL_BASE/yabai-repository-tools.XXXXXX" \
/bin/sh "$ROOT/repository_build.sh" > "$FAILURE_LOG" 2>&1
failure_status=$?
set -e

if [ "$failure_status" -ne 72 ]; then
    printf 'repository build fixture: expected injected mktemp status 72, got %s\n' "$failure_status" >&2
    cat "$FAILURE_LOG" >&2
    exit 1
fi

PHASE=verify-allocation-sequence
expected_attempts=$(printf '%s\n%s\n' \
    "$TMP_BASE/yabai-repository-build.XXXXXX" \
    "$TOOL_BASE/yabai-repository-tools.XXXXXX")
actual_attempts=$(cat "$ATTEMPTS")
if [ "$actual_attempts" != "$expected_attempts" ]; then
    printf 'repository build fixture: unexpected mktemp allocation sequence\n' >&2
    printf 'expected:\n%s\nactual:\n%s\n' "$expected_attempts" "$actual_attempts" >&2
    exit 1
fi

PHASE=verify-failure-cleanup
created_count=$(awk 'END { print NR }' "$CREATED")
if [ "$created_count" -ne 1 ]; then
    printf 'repository build fixture: expected one allocated root, got %s\n' "$created_count" >&2
    exit 1
fi
allocated_root=$(cat "$CREATED")
if [ -e "$allocated_root" ]; then
    printf 'repository build fixture: leaked temporary root after mktemp failure: %s\n' "$allocated_root" >&2
    exit 1
fi

PHASE=successful-build
set +e
TMPDIR="$TMP_BASE" /bin/sh -x "$ROOT/repository_build.sh" > "$SUCCESS_OUTPUT" 2> "$SUCCESS_TRACE"
success_status=$?
set -e
if [ "$success_status" -ne 0 ]; then
    printf 'repository build fixture: successful path returned status %s\n' "$success_status" >&2
    cat "$SUCCESS_OUTPUT" >&2
    cat "$SUCCESS_TRACE" >&2
    exit 1
fi
cat "$SUCCESS_OUTPUT"

PHASE=cleanup
cleanup
trap - 0 HUP INT TERM
printf 'repository build allocation failure and success fixtures passed\n'
