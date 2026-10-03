#!/bin/sh

set -eu

TEST_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd "$TEST_DIR/.." && pwd)
TEST_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/yabai-installer-test.XXXXXX")

cleanup()
{
    status=$?
    trap - 0 HUP INT TERM
    case "$TEST_ROOT" in
        "${TMPDIR:-/tmp}"/yabai-installer-test.*)
            [ -n "$TEST_ROOT" ] && [ -d "$TEST_ROOT" ] && rm -R "$TEST_ROOT"
            ;;
    esac
    exit "$status"
}

fail()
{
    echo "installer test failed: $*" >&2
    exit 1
}

assert_no_owned_paths()
{
    leftovers=$(find "$BIN_DIR" "$MAN_DIR" "$TEMP_DIR" \
        \( -name '.yabai-install.*' -o -name '.yabai-backup.*' \
        -o -name 'asmvik-yabai-v*-installer.*' \) -print)
    [ -z "$leftovers" ] || fail "installer-owned paths remain: $leftovers"
}

file_mode()
{
    if mode=$(stat -f '%Lp' "$1" 2>/dev/null); then
        printf '%s\n' "$mode"
    else
        stat -c '%a' "$1"
    fi
}

run_installer()
{
    installer=${1:-$INSTALLER_FIXTURE}
    mock_version=${2:-$INSTALLER_VERSION}
    mock_expected_hash=${3:-$INSTALLER_EXPECTED_HASH}
    (
        cd "$WORK_DIR"
        PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" MOCK_VERSION="$mock_version" \
            MOCK_EXPECTED_HASH="$mock_expected_hash" \
            /bin/sh "$installer" "$BIN_DIR" "$MAN_DIR" >/dev/null
    )
}

assert_tmpdir_template()
{
    tmpdir=$1
    expected_template=$2
    rm -f "$MKTEMP_LOG"
    [ ! -d "$MKTEMP_ESCAPE" ] || rm -R "$MKTEMP_ESCAPE"
    mkdir "$MKTEMP_ESCAPE"
    printf 'cleanup escape sentinel\n' > "$MKTEMP_ESCAPE/sentinel"

    if (
        cd "$WORK_DIR"
        PATH="$TMPDIR_MOCK_BIN:$MOCK_BIN:$PATH" TMPDIR="$tmpdir" \
            MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
            MOCK_MKTEMP_LOG="$MKTEMP_LOG" MOCK_MKTEMP_RESULT="$MKTEMP_ESCAPE" \
            /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >/dev/null 2>&1
    ); then
        fail "TMPDIR '$tmpdir' template probe unexpectedly succeeded"
    fi

    [ -f "$MKTEMP_LOG" ] || fail "TMPDIR '$tmpdir' did not invoke mocked mktemp"
    [ "$(sed -n '1p' "$MKTEMP_LOG")" = 2 ] || fail "TMPDIR '$tmpdir' used the wrong mktemp argument count"
    [ "$(sed -n '2p' "$MKTEMP_LOG")" = -d ] || fail "TMPDIR '$tmpdir' omitted mktemp -d"
    [ "$(sed -n '3p' "$MKTEMP_LOG")" = "$expected_template" ] || \
        fail "TMPDIR '$tmpdir' used a non-canonical mktemp template"
    [ "$(cat "$MKTEMP_ESCAPE/sentinel")" = 'cleanup escape sentinel' ] || \
        fail "TMPDIR '$tmpdir' cleanup escaped its canonical prefix"
}

trap cleanup 0
trap 'exit 1' HUP INT TERM

MOCK_BIN="$TEST_ROOT/mock-bin"
TMPDIR_MOCK_BIN="$TEST_ROOT/tmpdir-mock-bin"
BIN_DIR="$TEST_ROOT/bin"
MAN_DIR="$TEST_ROOT/man"
WORK_DIR="$TEST_ROOT/work"
TEMP_DIR="$TEST_ROOT/tmp"
MKTEMP_LOG="$TEST_ROOT/mktemp.log"
MKTEMP_ESCAPE="$TEST_ROOT/mktemp-escape"
INSTALLER_FIXTURE="$TEST_ROOT/install-fixture.sh"
mkdir "$MOCK_BIN" "$TMPDIR_MOCK_BIN" "$BIN_DIR" "$MAN_DIR" "$WORK_DIR" "$TEMP_DIR"

[ "$(sed -n '1p' "$REPO_ROOT/scripts/install.sh")" = '#!/bin/sh' ] || \
    fail 'installer does not use /bin/sh directly'
[ "$(grep -c '^PATH=/usr/bin:/bin:/usr/sbin:/sbin$' "$REPO_ROOT/scripts/install.sh")" -eq 1 ] || \
    fail 'installer does not pin one unconditional trusted production PATH'
if grep -F 'YABAI_INSTALL_TEST_PATH' "$REPO_ROOT/scripts/install.sh" >/dev/null; then
    fail 'installer retains a production test PATH bypass'
fi
grep -F 'URL="https://github.com/${AUTHOR}/${NAME}/releases/download/v${VERSION}/${ARCHIVE}"' \
    "$REPO_ROOT/scripts/install.sh" >/dev/null || fail 'installer release URL template is incorrect'
sed "s|^PATH=/usr/bin:/bin:/usr/sbin:/sbin$|PATH=$MOCK_BIN:/usr/bin:/bin:/usr/sbin:/sbin|" \
    "$REPO_ROOT/scripts/install.sh" > "$INSTALLER_FIXTURE"
chmod 0755 "$INSTALLER_FIXTURE"

INSTALLER_VERSION=$(sed -n 's/^VERSION="\([^"]*\)"$/\1/p' "$INSTALLER_FIXTURE")
INSTALLER_EXPECTED_HASH=$(sed -n 's/^EXPECTED_HASH="\([^"]*\)"$/\1/p' "$INSTALLER_FIXTURE")
case "$INSTALLER_VERSION" in
    ''|*[!A-Za-z0-9._-]*) fail 'installer fixture has a missing or unsafe VERSION' ;;
esac
case "$INSTALLER_EXPECTED_HASH" in
    *[!0-9A-Fa-f]*) fail 'installer fixture has an unsafe EXPECTED_HASH' ;;
esac
[ "${#INSTALLER_EXPECTED_HASH}" -eq 64 ] || fail 'installer fixture has a missing or invalid EXPECTED_HASH'

FAKE_SH_BIN="$TEST_ROOT/fake-sh-bin"
FAKE_SH_MARKER="$TEST_ROOT/fake-sh-used"
mkdir "$FAKE_SH_BIN"
cat > "$FAKE_SH_BIN/sh" <<EOF
#!/bin/sh
: > "$FAKE_SH_MARKER"
exit 68
EOF
chmod 0755 "$FAKE_SH_BIN/sh"
if PATH="$FAKE_SH_BIN:$PATH" "$REPO_ROOT/scripts/install.sh" relative relative >/dev/null 2>&1; then
    fail 'production installer accepted invalid local destinations'
fi
[ ! -e "$FAKE_SH_MARKER" ] || fail 'production installer shebang used sh from PATH'

cat > "$MOCK_BIN/curl" <<'EOF'
#!/bin/sh
set -eu
version=${MOCK_VERSION:?}
archive="yabai-v${version}.tar.gz"
expected="https://github.com/asmvik/yabai/releases/download/v${version}/${archive}"
[ "$#" -eq 4 ] || exit 64
[ "$1" = --fail ] || exit 64
[ "$2" = --location ] || exit 64
[ "$3" = --remote-name ] || exit 64
[ "$4" = "$expected" ] || exit 64
printf 'mock archive\n' > "$archive"
EOF

cat > "$MOCK_BIN/shasum" <<'EOF'
#!/bin/sh
set -eu
version=${MOCK_VERSION:?}
expected_hash=${MOCK_EXPECTED_HASH:?}
archive="yabai-v${version}.tar.gz"
[ "$#" -eq 3 ] || exit 64
[ "$1" = -a ] || exit 64
[ "$2" = 256 ] || exit 64
case "$3" in
    "$archive")
        [ "$(cat "$3")" = 'mock archive' ] || exit 64
        if [ "${MOCK_HASH_MISMATCH:-0}" -eq 1 ]; then
            hash=invalid-release-hash
        else
            hash=$expected_hash
        fi
        ;;
    */yabai)
        [ "$(cat "$3")" = 'new binary' ] || exit 64
        hash=mock-installed-binary-hash
        ;;
    *) exit 64 ;;
esac
printf '%s  %s\n' "$hash" "$3"
EOF

cat > "$MOCK_BIN/tar" <<'EOF'
#!/bin/sh
set -eu
version=${MOCK_VERSION:?}
archive="yabai-v${version}.tar.gz"
[ "$#" -eq 2 ] || exit 64
[ "$1" = -xzf ] || exit 64
[ "$2" = "$archive" ] || exit 64
[ -f "$2" ] || exit 64
[ "$(cat "$2")" = 'mock archive' ] || exit 64
[ -z "${MOCK_TAR_MARKER:-}" ] || : > "$MOCK_TAR_MARKER"
mkdir -p archive/bin archive/doc
printf 'new binary\n' > archive/bin/yabai
chmod 0700 archive/bin/yabai
if [ "${MOCK_TAR_MODE:-complete}" != missing-man ]; then
    printf 'new manual\n' > archive/doc/yabai.1
    chmod 0600 archive/doc/yabai.1
fi
EOF

cat > "$MOCK_BIN/id" <<'EOF'
#!/bin/sh
set -eu
case "$#:$1" in
    1:-u) printf '%s\n' "${MOCK_EUID:-501}" ;;
    1:-un) printf '%s\n' "${MOCK_CURRENT_USER:-tester}" ;;
    2:-u)
        [ "$2" = "${MOCK_SUDO_USER:-alice}" ] || exit 1
        printf '%s\n' "${MOCK_SUDO_UID:-502}"
        ;;
    2:-un)
        [ "$2" = "${MOCK_SUDO_UID:-502}" ] || exit 1
        printf '%s\n' "${MOCK_SUDO_USER:-alice}"
        ;;
    *) exit 64 ;;
esac
EOF

cat > "$MOCK_BIN/mv" <<'EOF'
#!/bin/sh
set -eu
if [ "${MOCK_MV_FAIL_SECOND_FINAL:-0}" -eq 1 ]; then
    [ "$#" -eq 3 ] || exit 64
    [ "$1" = -f ] || exit 64
    case "$2" in
        */.yabai-install.*/yabai|*/.yabai-install.*/yabai.1)
            count=0
            [ ! -f "$MOCK_MV_COUNT" ] || count=$(cat "$MOCK_MV_COUNT")
            count=$((count + 1))
            printf '%s\n' "$count" > "$MOCK_MV_COUNT"
            [ "$count" -ne 2 ] || exit 65
            ;;
    esac
fi

mode=${MOCK_MV_FAIL_AFTER:-none}
operation=none
case "$2:$3" in
    */bin/yabai:*/bin/.yabai-backup.*/yabai) operation=bin-backup ;;
    */man/yabai.1:*/man/.yabai-backup.*/yabai.1) operation=man-backup ;;
    */bin/.yabai-install.*/yabai:*/bin/yabai) operation=bin-final ;;
    */man/.yabai-install.*/yabai.1:*/man/yabai.1) operation=man-final ;;
esac
if [ "$mode" != none ] && [ "$mode" = "$operation" ]; then
    /bin/mv "$@"
    exit 65
fi
exec /bin/mv "$@"
EOF

cat > "$MOCK_BIN/rm" <<'EOF'
#!/bin/sh
set -eu
mode=${MOCK_RM_MODE:-none}
marker=${MOCK_RM_MARKER:-}
backup=0
for argument do
    case "$argument" in */.yabai-backup.*) backup=1 ;; esac
done
if [ "$backup" -eq 1 ] && [ "$mode" != none ] && [ ! -e "$marker" ]; then
    : > "$marker"
    if [ "$mode" = fail-after ]; then
        /bin/rm "$@"
    fi
    exit 66
fi
exec /bin/rm "$@"
EOF

cat > "$MOCK_BIN/mktemp" <<'EOF'
#!/bin/sh
set -eu
if [ -z "${MOCK_MKTEMP_LOG:-}" ]; then
    exec /usr/bin/mktemp "$@"
fi
[ ! -e "$MOCK_MKTEMP_LOG" ] || exit 69
printf '%s\n' "$#" "$1" "$2" > "$MOCK_MKTEMP_LOG"
[ "$#" -eq 2 ]
[ "$1" = -d ]
printf '%s\n' "$MOCK_MKTEMP_RESULT"
EOF

cat > "$TMPDIR_MOCK_BIN/curl" <<'EOF'
#!/bin/sh
exit 67
EOF
chmod +x "$MOCK_BIN/curl" "$MOCK_BIN/shasum" "$MOCK_BIN/tar" "$MOCK_BIN/id" "$MOCK_BIN/mv" "$MOCK_BIN/rm" \
    "$MOCK_BIN/mktemp" "$TMPDIR_MOCK_BIN/curl"

installer_template="asmvik-yabai-v${INSTALLER_VERSION}-installer.XXXXXX"
assert_tmpdir_template / "/$installer_template"
assert_tmpdir_template /tmp/ "/tmp/$installer_template"
assert_tmpdir_template /tmp/// "/tmp/$installer_template"
assert_tmpdir_template /// "/$installer_template"

rm -f "$MKTEMP_LOG"
relative_output="$TEST_ROOT/relative-output"
if (
    cd "$WORK_DIR"
    PATH="$TMPDIR_MOCK_BIN:$MOCK_BIN:$PATH" TMPDIR=relative/tmp \
        MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
        MOCK_MKTEMP_LOG="$MKTEMP_LOG" MOCK_MKTEMP_RESULT="$MKTEMP_ESCAPE" \
        /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >"$relative_output" 2>&1
); then
    fail 'relative TMPDIR unexpectedly succeeded'
fi
[ ! -e "$MKTEMP_LOG" ] || fail 'relative TMPDIR invoked mktemp'
grep -F "Temporary directory base 'relative/tmp' is not an absolutepath." "$relative_output" >/dev/null || \
    fail 'relative TMPDIR did not report absolute-path rejection'
[ "$(cat "$MKTEMP_ESCAPE/sentinel")" = 'cleanup escape sentinel' ] || \
    fail 'relative TMPDIR cleanup escaped before rejection'
rm -R "$MKTEMP_ESCAPE"

legacy_installer_dir="$WORK_DIR/asmvik-yabai-v${INSTALLER_VERSION}-installer"
mkdir "$legacy_installer_dir"
printf 'legacy directory sentinel\n' > "$legacy_installer_dir/sentinel"
printf 'old binary\n' > "$BIN_DIR/yabai"
printf 'old manual\n' > "$MAN_DIR/yabai.1"

run_installer
[ "$(cat "$BIN_DIR/yabai")" = 'new binary' ] || fail 'successful installation did not replace the binary'
[ "$(cat "$MAN_DIR/yabai.1")" = 'new manual' ] || fail 'successful installation did not replace the man page'
[ "$(cat "$legacy_installer_dir/sentinel")" = 'legacy directory sentinel' ] || fail 'legacy predictable directory was modified'
assert_no_owned_paths

PUBLISHED_INSTALLER="$TEST_ROOT/install-published.sh"
PUBLISHED_EXPECTED_HASH=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
sed -e 's/^VERSION=.*/VERSION="9.9.9"/' \
    -e "s/^EXPECTED_HASH=.*/EXPECTED_HASH=\"$PUBLISHED_EXPECTED_HASH\"/" \
    "$INSTALLER_FIXTURE" > "$PUBLISHED_INSTALLER"
printf 'old binary before published-version install\n' > "$BIN_DIR/yabai"
printf 'old manual before published-version install\n' > "$MAN_DIR/yabai.1"
run_installer "$PUBLISHED_INSTALLER" 9.9.9 "$PUBLISHED_EXPECTED_HASH"
[ "$(cat "$BIN_DIR/yabai")" = 'new binary' ] || fail 'published-version installation did not replace the binary'
[ "$(cat "$MAN_DIR/yabai.1")" = 'new manual' ] || fail 'published-version installation did not replace the man page'
published_leftovers=$(find "$TEMP_DIR" -name 'asmvik-yabai-v9.9.9-installer.*' -print)
[ -z "$published_leftovers" ] || fail "published-version temporary directory remains: $published_leftovers"
assert_no_owned_paths

printf 'old binary before hash mismatch\n' > "$BIN_DIR/yabai"
printf 'old manual before hash mismatch\n' > "$MAN_DIR/yabai.1"
TAR_MARKER="$TEST_ROOT/tar-invoked"
if (
    cd "$WORK_DIR"
    PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
        MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
        MOCK_HASH_MISMATCH=1 MOCK_TAR_MARKER="$TAR_MARKER" \
        /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >/dev/null 2>&1
); then
    fail 'checksum mismatch unexpectedly succeeded'
fi
[ "$(cat "$BIN_DIR/yabai")" = 'old binary before hash mismatch' ] || fail 'checksum rejection replaced the binary'
[ "$(cat "$MAN_DIR/yabai.1")" = 'old manual before hash mismatch' ] || fail 'checksum rejection replaced the man page'
[ ! -e "$TAR_MARKER" ] || fail 'checksum rejection extracted the archive'
assert_no_owned_paths

printf 'old binary after checksum rejection\n' > "$BIN_DIR/yabai"
printf 'old manual after checksum rejection\n' > "$MAN_DIR/yabai.1"
if (
    cd "$WORK_DIR"
    PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
        MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
        MOCK_TAR_MODE=missing-man \
        /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >/dev/null 2>&1
); then
    fail 'malformed release preparation unexpectedly succeeded'
fi
[ "$(cat "$BIN_DIR/yabai")" = 'old binary after checksum rejection' ] || fail 'failed preparation replaced the binary'
[ "$(cat "$MAN_DIR/yabai.1")" = 'old manual after checksum rejection' ] || fail 'failed preparation replaced the man page'
assert_no_owned_paths

printf 'old linked binary\n' > "$TEST_ROOT/old-linked-binary"
rm -f "$BIN_DIR/yabai"
ln -s "$TEST_ROOT/old-linked-binary" "$BIN_DIR/yabai"
printf 'old manual before commit failure\n' > "$MAN_DIR/yabai.1"
MV_COUNT="$TEST_ROOT/mv-count"
if (
    cd "$WORK_DIR"
    PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
        MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
        MOCK_MV_FAIL_SECOND_FINAL=1 MOCK_MV_COUNT="$MV_COUNT" \
        /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >/dev/null 2>&1
); then
    fail 'second commit-time mv failure unexpectedly succeeded'
fi
[ "$(cat "$MV_COUNT")" -eq 2 ] || fail 'mv mock did not fail on the second final rename'
[ -L "$BIN_DIR/yabai" ] || fail 'binary symlink was not restored after commit failure'
[ "$(readlink "$BIN_DIR/yabai")" = "$TEST_ROOT/old-linked-binary" ] || fail 'wrong binary symlink was restored'
[ "$(cat "$MAN_DIR/yabai.1")" = 'old manual before commit failure' ] || fail 'man page was not restored after commit failure'
assert_no_owned_paths

rm -f "$BIN_DIR/yabai" "$MAN_DIR/yabai.1"
mkdir "$BIN_DIR/yabai"
printf 'binary directory sentinel\n' > "$BIN_DIR/yabai/sentinel"
printf 'manual beside binary directory\n' > "$MAN_DIR/yabai.1"
if run_installer 2>/dev/null; then
    fail 'binary directory destination unexpectedly succeeded'
fi
[ "$(cat "$BIN_DIR/yabai/sentinel")" = 'binary directory sentinel' ] || fail 'binary directory sentinel was modified'
[ "$(cat "$MAN_DIR/yabai.1")" = 'manual beside binary directory' ] || fail 'man page changed when binary destination was a directory'
assert_no_owned_paths

rm -R "$BIN_DIR/yabai"
printf 'binary beside manual directory\n' > "$BIN_DIR/yabai"
rm -f "$MAN_DIR/yabai.1"
mkdir "$MAN_DIR/yabai.1"
printf 'manual directory sentinel\n' > "$MAN_DIR/yabai.1/sentinel"
if run_installer 2>/dev/null; then
    fail 'man-page directory destination unexpectedly succeeded'
fi
[ "$(cat "$BIN_DIR/yabai")" = 'binary beside manual directory' ] || fail 'binary changed when man destination was a directory'
[ "$(cat "$MAN_DIR/yabai.1/sentinel")" = 'manual directory sentinel' ] || fail 'man directory sentinel was modified'
assert_no_owned_paths

rm -f "$BIN_DIR/yabai"
rm -R "$MAN_DIR/yabai.1"
(
    umask 077
    run_installer
)
[ "$(file_mode "$BIN_DIR/yabai")" = 755 ] || fail 'installed binary mode is not 0755 under umask 077'
[ "$(file_mode "$MAN_DIR/yabai.1")" = 644 ] || fail 'installed man-page mode is not 0644 under umask 077'
assert_no_owned_paths

# A rename can take effect even when mv reports failure. Backup failures must
# restore every original, while final failures without originals must remove
# every destination installed by the aborted transaction.
for mode in bin-backup man-backup; do
    printf 'old binary for %s\n' "$mode" > "$BIN_DIR/yabai"
    printf 'old manual for %s\n' "$mode" > "$MAN_DIR/yabai.1"
    if (
        cd "$WORK_DIR"
        PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
            MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
            MOCK_MV_FAIL_AFTER="$mode" \
            /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >/dev/null 2>&1
    ); then
        fail "$mode post-effect failure unexpectedly succeeded"
    fi
    [ "$(cat "$BIN_DIR/yabai")" = "old binary for $mode" ] || fail "$mode did not restore the binary"
    [ "$(cat "$MAN_DIR/yabai.1")" = "old manual for $mode" ] || fail "$mode did not restore the man page"
    assert_no_owned_paths
done

for mode in bin-final man-final; do
    rm -f "$BIN_DIR/yabai" "$MAN_DIR/yabai.1"
    if (
        cd "$WORK_DIR"
        PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
            MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
            MOCK_MV_FAIL_AFTER="$mode" \
            /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >/dev/null 2>&1
    ); then
        fail "$mode post-effect failure unexpectedly succeeded"
    fi
    [ ! -e "$BIN_DIR/yabai" ] && [ ! -L "$BIN_DIR/yabai" ] || fail "$mode stranded a new binary"
    [ ! -e "$MAN_DIR/yabai.1" ] && [ ! -L "$MAN_DIR/yabai.1" ] || fail "$mode stranded a new man page"
    assert_no_owned_paths
done

# Once both final renames commit, neither an interrupted removal nor a removal
# that reports failure after taking effect may roll the installation back.
for mode in fail-before fail-after; do
    printf 'old binary before %s cleanup\n' "$mode" > "$BIN_DIR/yabai"
    printf 'old manual before %s cleanup\n' "$mode" > "$MAN_DIR/yabai.1"
    marker="$TEST_ROOT/rm-$mode"
    if (
        cd "$WORK_DIR"
        PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
            MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
            MOCK_RM_MODE="$mode" MOCK_RM_MARKER="$marker" \
            /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >/dev/null 2>&1
    ); then
        fail "$mode backup-removal failure unexpectedly succeeded"
    fi
    [ -f "$marker" ] || fail "$mode backup-removal failure was not injected"
    [ "$(cat "$BIN_DIR/yabai")" = 'new binary' ] || fail "$mode cleanup rolled back the committed binary"
    [ "$(cat "$MAN_DIR/yabai.1")" = 'new manual' ] || fail "$mode cleanup rolled back the committed man page"
    assert_no_owned_paths
done

identity_output="$TEST_ROOT/identity-output"
(
    cd "$WORK_DIR"
    PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
        MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
        MOCK_EUID=501 MOCK_CURRENT_USER=developer SUDO_USER=root \
        /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >"$identity_output"
)
grep -F "  developer ALL=(root) NOPASSWD:" "$identity_output" >/dev/null || \
    fail 'normal invocation did not use the current user in the sudoers row'

SAFE_BIN_DIR=$BIN_DIR
UNSAFE_BIN_DIR="$TEST_ROOT/bin unsafe,#"
mkdir "$UNSAFE_BIN_DIR"
BIN_DIR=$UNSAFE_BIN_DIR
(
    cd "$WORK_DIR"
    PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
        MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
        MOCK_EUID=501 MOCK_CURRENT_USER=developer \
        MOCK_MV_FAIL_SECOND_FINAL=0 MOCK_MV_FAIL_AFTER=none MOCK_RM_MODE=none \
        /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >"$identity_output"
)
[ "$(cat "$BIN_DIR/yabai")" = 'new binary' ] || fail 'unsafe sudoers path installation did not install the binary'
[ "$(cat "$MAN_DIR/yabai.1")" = 'new manual' ] || fail 'unsafe sudoers path installation did not install the man page'
grep -F 'Unable to determine a non-root invoking user; no sudoers row was generated.' "$identity_output" >/dev/null || \
    fail 'unsafe sudoers path did not report that no row was generated'
if grep -F "ALL=(root) NOPASSWD:" "$identity_output" >/dev/null; then
    fail 'unsafe binary path emitted a sudoers row'
fi
assert_no_owned_paths
BIN_DIR=$SAFE_BIN_DIR

(
    cd "$WORK_DIR"
    PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
        MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
        MOCK_EUID=0 MOCK_SUDO_USER=alice MOCK_SUDO_UID=502 SUDO_USER=alice \
        /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >"$identity_output"
)
grep -F "  alice ALL=(root) NOPASSWD:" "$identity_output" >/dev/null || \
    fail 'sudo invocation did not use the validated invoking user'
if grep -F "  root ALL=(root) NOPASSWD:" "$identity_output" >/dev/null; then
    fail 'sudo invocation emitted a root sudoers row'
fi

(
    cd "$WORK_DIR"
    unset SUDO_USER
    PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
        MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" MOCK_EUID=0 \
        /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >"$identity_output"
)
grep -F 'Unable to determine a non-root invoking user; no sudoers row was generated.' "$identity_output" >/dev/null || \
    fail 'root invocation without SUDO_USER did not omit the sudoers row'
if grep -F "ALL=(root) NOPASSWD:" "$identity_output" >/dev/null; then
    fail 'root invocation without SUDO_USER emitted a sudoers row'
fi

(
    cd "$WORK_DIR"
    PATH="$MOCK_BIN:$PATH" TMPDIR="$TEMP_DIR" \
        MOCK_VERSION="$INSTALLER_VERSION" MOCK_EXPECTED_HASH="$INSTALLER_EXPECTED_HASH" \
        MOCK_EUID=0 SUDO_USER='invalid;name' \
        /bin/sh "$INSTALLER_FIXTURE" "$BIN_DIR" "$MAN_DIR" >"$identity_output"
)
grep -F 'Unable to determine a non-root invoking user; no sudoers row was generated.' "$identity_output" >/dev/null || \
    fail 'invalid SUDO_USER did not omit the sudoers row'
if grep -F "ALL=(root) NOPASSWD:" "$identity_output" >/dev/null; then
    fail 'invalid SUDO_USER emitted a sudoers row'
fi
assert_no_owned_paths

PUBLISH_ROOT="$TEST_ROOT/publish"
PUBLISH_BUILD="$PUBLISH_ROOT/build"
PUBLISH_SCRIPT="$PUBLISH_ROOT/scripts"
PUBLISH_BASELINE="$PUBLISH_ROOT/install.baseline.sh"
mkdir -p "$PUBLISH_BUILD" "$PUBLISH_SCRIPT"
cp "$REPO_ROOT/scripts/install.sh" "$PUBLISH_SCRIPT/install.sh"
cp "$PUBLISH_SCRIPT/install.sh" "$PUBLISH_BASELINE"

if make -f "$REPO_ROOT/makefile" publish BUILD_PATH="$PUBLISH_BUILD" SCRIPT_PATH="$PUBLISH_SCRIPT" >/dev/null 2>&1; then
    fail 'publish without a binary unexpectedly succeeded'
fi
cmp -s "$PUBLISH_SCRIPT/install.sh" "$PUBLISH_BASELINE" || fail 'missing-binary publish changed installer'

cat > "$PUBLISH_BUILD/yabai" <<'EOF'
#!/bin/sh
printf '%s\n' 'yabai-v9.8.7'
EOF
chmod 0755 "$PUBLISH_BUILD/yabai"
if make -f "$REPO_ROOT/makefile" publish BUILD_PATH="$PUBLISH_BUILD" SCRIPT_PATH="$PUBLISH_SCRIPT" >/dev/null 2>&1; then
    fail 'publish without an archive unexpectedly succeeded'
fi
cmp -s "$PUBLISH_SCRIPT/install.sh" "$PUBLISH_BASELINE" || fail 'missing-archive publish changed installer'

printf 'local fake archive\n' > "$PUBLISH_BUILD/yabai-v9.8.7.tar.gz"
PUBLISH_HASH=$(shasum -a 256 "$PUBLISH_BUILD/yabai-v9.8.7.tar.gz" | cut -d ' ' -f 1)
make -f "$REPO_ROOT/makefile" publish BUILD_PATH="$PUBLISH_BUILD" SCRIPT_PATH="$PUBLISH_SCRIPT" >/dev/null
grep -F 'VERSION="9.8.7"' "$PUBLISH_SCRIPT/install.sh" >/dev/null || \
    fail 'successful publish wrote the wrong version'
grep -F "EXPECTED_HASH=\"$PUBLISH_HASH\"" "$PUBLISH_SCRIPT/install.sh" >/dev/null || \
    fail 'successful publish wrote the wrong archive hash'
[ "$(file_mode "$PUBLISH_SCRIPT/install.sh")" = 755 ] || fail 'publish did not retain executable mode 0755'

printf 'installer regression tests passed (28 cases)\n'
