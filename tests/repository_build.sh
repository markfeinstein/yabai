#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
MKTEMP=${REPOSITORY_MKTEMP:-/usr/bin/mktemp}
case "$MKTEMP" in /*) ;; *) exit 1 ;; esac
TMP_BASE=$(CDPATH= cd -- "${TMPDIR:-/tmp}" && pwd -P)
tmp_candidate=$("$MKTEMP" -d "$TMP_BASE/yabai-repository-build.XXXXXX")
case "$tmp_candidate" in "$TMP_BASE"/yabai-repository-build.*) ;; *) exit 1 ;; esac
[ -d "$tmp_candidate" ] && [ ! -L "$tmp_candidate" ]
TMP=$tmp_candidate
TOOL_ROOT=
cleanup()
{
    case "$TMP" in "$TMP_BASE"/yabai-repository-build.*)
        [ ! -L "$TMP" ] && [ -d "$TMP" ] && /usr/bin/find "$TMP" -depth -delete
        ;;
    esac
    if [ -n "$TOOL_ROOT" ]; then
        case "$TOOL_ROOT" in "$TOOL_BASE"/yabai-repository-tools.*)
            [ ! -L "$TOOL_ROOT" ] && [ -d "$TOOL_ROOT" ] && /usr/bin/find "$TOOL_ROOT" -depth -delete
            ;;
        esac
    fi
}
trap cleanup 0 HUP INT TERM

TOOL_BASE=$(CDPATH= cd -- /tmp && pwd -P)
tool_candidate=$("$MKTEMP" -d "$TOOL_BASE/yabai-repository-tools.XXXXXX")
case "$tool_candidate" in "$TOOL_BASE"/yabai-repository-tools.*) ;; *) exit 1 ;; esac
[ -d "$tool_candidate" ] && [ ! -L "$tool_candidate" ]
TOOL_ROOT=$tool_candidate

WORK=$TMP/work
mkdir -p "$WORK" "$TOOL_ROOT/tools" "$TOOL_ROOT/attacker"
cp "$ROOT/makefile" "$WORK/makefile"
for tree in src assets examples doc scripts; do cp -R "$ROOT/$tree" "$WORK/$tree"; done
mkdir -p "$WORK/tests" "$WORK/bin" "$WORK/release" "$WORK/deploy"
cp "$ROOT/tests/doc_contract.sh" "$WORK/tests/doc_contract.sh"
LOG=$TOOL_ROOT/tools.log
XCRUN_LOG=$TMP/xcrun.log
VERIFY_COUNT=$TMP/verify-count
: > "$LOG"; : > "$XCRUN_LOG"; printf '0\n' > "$VERIFY_COUNT"
export DEPLOY_LOG=$LOG XCRUN_LOG VERIFY_COUNT

cat > "$TOOL_ROOT/tools/xcrun" <<'TOOL'
#!/bin/sh
set -eu
printf 'xcrun\n' >> "$XCRUN_LOG"
for arg in "$@"; do printf 'xcrun-arg:%s\n' "$arg" >> "$XCRUN_LOG"; done
out=; prev=
for arg in "$@"; do
    if [ "$prev" = -o ]; then out=$arg; break; fi
    prev=$arg
done
[ -n "$out" ]; mkdir -p "$(dirname "$out")"
case "$out" in
    */yabai)
        cat > "$out" <<'BIN'
#!/bin/sh
[ -z "${DEPLOY_LOG:-}" ] || printf 'binary:%s\n' "${1:-}" >> "$DEPLOY_LOG"
case "${1:-}" in
    --version) printf '%s\n' yabai-v-test ;;
    --uninstall-sa|--load-sa) [ "${FAIL_SA:-}" != "${1#--}" ] ;;
esac
BIN
        chmod +x "$out" ;;
    *) printf 'native artifact\n' > "$out" ;;
esac
TOOL
cat > "$TOOL_ROOT/tools/xxd" <<'TOOL'
#!/bin/sh
set -eu
printf 'xxd:%s\n' "$*" >> "$XCRUN_LOG"
printf 'unsigned char fixture[] = { 0 };\n'
TOOL
cat > "$TOOL_ROOT/tools/codesign" <<'TOOL'
#!/bin/sh
set -eu
printf 'codesign:%s\n' "$*" >> "$DEPLOY_LOG"
case " $* " in
    *' -fs '*) printf '\n# fixture-signature:AAAA\n' >> "$3" ;;
    *' --verify '*)
        count=$(cat "$VERIFY_COUNT"); count=$((count + 1)); printf '%s\n' "$count" > "$VERIFY_COUNT"
        target=
        for target do :; done
        if [ "${MUTATE_BEFORE_LOCAL:-0}" = 1 ] && [ "$count" -eq 1 ]; then
            printf '#!/bin/sh\nexit 99\n' > "$target"; chmod +x "$target"
        fi
        requirement=; previous=
        for arg in "$@"; do
            if [ "$previous" = -R ]; then requirement=$arg; break; fi
            previous=$arg
        done
        [ "$requirement" = "${TRUSTED_REQUIREMENT:-certificate leaf = H\"AAAA\"}" ]
        grep -q '^# fixture-signature:AAAA$' "$target"
        [ "$count" != "${FAIL_VERIFY_AT:-0}" ]
        ;;
esac
TOOL
cat > "$TOOL_ROOT/tools/sudo" <<'TOOL'
#!/bin/sh
set -eu
printf 'sudo\n' >> "$DEPLOY_LOG"
# The privileged invocation intentionally receives no user-controlled source path.
if [ "${MUTATE_SOURCE:-0}" = 1 ]; then
    [ -n "${SOURCE_TO_MUTATE:-}" ]
    printf '#!/bin/sh\nexit 98\n' > "$SOURCE_TO_MUTATE"
    chmod +x "$SOURCE_TO_MUTATE"
fi
if [ -n "${ATTACKER_PATH:-}" ]; then PATH="$ATTACKER_PATH:$PATH"; export PATH; fi
exec "$@"
TOOL
cat > "$TOOL_ROOT/tools/security" <<'TOOL'
#!/bin/sh
set -eu
printf '%s\n' \
    '  1) AAAA "Apple Development: Alice (TEAM1)"' \
    '  2) BBBB "Developer ID Application: Alice (TEAM1)"' \
    '  3) CCCC "Alice\tBob"' \
    '     3 valid identities found'
TOOL
cat > "$TOOL_ROOT/tools/stat" <<'TOOL'
#!/bin/sh
set -eu
case "$2" in
    %u) if [ -n "${STAT_FOREIGN:-}" ] && [ "$3" = "$STAT_FOREIGN" ]; then printf '99999\n'; else /usr/bin/stat -f %u "$3"; fi ;;
    %OLp) /usr/bin/stat -f %OLp "$3" ;;
    %z) /usr/bin/stat -f %z "$3" ;;
    *) exit 2 ;;
esac
TOOL
cat > "$TOOL_ROOT/tools/acl-list" <<'TOOL'
#!/bin/sh
set -eu
path=$2
printf '%s\n' "$path"
if [ -n "${ACL_PATH:-}" ] && [ "$path" = "$ACL_PATH" ]; then
    printf ' 0: group:attacker allow add_file,delete_child\n'
fi
TOOL
cat > "$TOOL_ROOT/tools/acl-list-fail" <<'TOOL'
#!/bin/sh
exit 71
TOOL
cat > "$TOOL_ROOT/tools/asciidoctor" <<'TOOL'
#!/bin/sh
set -eu
out=; previous=
for arg in "$@"; do [ "$previous" = -o ] && out=$arg; previous=$arg; done
cp doc/yabai.1 "$out"
TOOL
cat > "$TOOL_ROOT/tools/asciidoctor-bad" <<'TOOL'
#!/bin/sh
set -eu
out=; previous=
for arg in "$@"; do [ "$previous" = -o ] && out=$arg; previous=$arg; done
printf 'noncanonical manual\n' > "$out"
TOOL
cat > "$TOOL_ROOT/tools/python" <<'TOOL'
#!/bin/sh
set -eu
printf 'python:%s\n' "$*" >> "$DEPLOY_LOG"
TOOL
cat > "$TOOL_ROOT/tools/tar-fail" <<'TOOL'
#!/bin/sh
set -eu
case "$1" in
    -czf) printf 'partial archive\n' > "$2"; exit 1 ;;
    *) exec /usr/bin/tar "$@" ;;
esac
TOOL
for tool in mktemp cp chmod mv rm cut stat codesign shasum; do
    cat > "$TOOL_ROOT/attacker/$tool" <<TOOL
#!/bin/sh
printf 'attacker:$tool\\n' >> '$LOG'
exit 99
TOOL
done
chmod +x "$TOOL_ROOT/tools/"* "$TOOL_ROOT/attacker/"*

run_make()
{
    make -f "$WORK/makefile" \
        "XCRUN=$TOOL_ROOT/tools/xcrun" \
        "XXD=$TOOL_ROOT/tools/xxd" \
        "CODESIGN=$TOOL_ROOT/tools/codesign" \
        "SUDO=$TOOL_ROOT/tools/sudo" \
        "STAT=$TOOL_ROOT/tools/stat" \
        "ACL_LIST=$TOOL_ROOT/tools/acl-list" \
        "ASCIIDOCTOR=$TOOL_ROOT/tools/asciidoctor" \
        "PYTHON=$TOOL_ROOT/tools/python" \
        "$@"
}
run_deploy()
{
    run_make \
        "DEPLOY_DIR=$WORK/deploy" \
        "DEPLOY_TRUST_ROOT=$TMP" \
        "DEPLOY_OWNER_UID=$(id -u)" \
        "$@"
}
prepare_signed()
{
    rm -f "$WORK/bin/release/yabai"
    run_make sign >/dev/null
}
cd "$WORK"

# Certificate names are matched literally, including regex metacharacters and backslashes.
: > "$LOG"
PATH="$TOOL_ROOT/tools:/usr/bin:/bin" TERM=dumb "$WORK/scripts/codesign" /bin/sh 'Apple Development: Alice (TEAM1)'
PATH="$TOOL_ROOT/tools:/usr/bin:/bin" TERM=dumb "$WORK/scripts/codesign" /bin/sh 'Alice\tBob'
grep -Fq 'codesign:--deep --force --verbose --sign AAAA /bin/sh' "$LOG"
grep -Fq 'codesign:--deep --force --verbose --sign CCCC /bin/sh' "$LOG"

# Configuration products coexist; generated.c is the sole published OSAX input.
run_make debug release asan tsan >/dev/null
for config in debug release asan tsan; do
    test -x "$WORK/bin/$config/yabai"
    test -f "$WORK/bin/$config/osax/generated.c"
    test ! -e "$WORK/bin/$config/osax/payload"
    test ! -e "$WORK/bin/$config/osax/loader"
done
# The embedded Info.plist is a daemon link input, never an object-compile flag.
test "$(grep -F -c 'xcrun-arg:-sectcreate' "$XCRUN_LOG")" -eq 4
test "$(grep -F -c 'xcrun-arg:./assets/Info.plist' "$XCRUN_LOG")" -eq 4

# An unchanged configuration is a true no-op, while an OSAX source change
# regenerates the embedded artifacts and rebuilds the dependent binary once.
: > "$XCRUN_LOG"
run_make release >/dev/null
test ! -s "$XCRUN_LOG"
touch src/osax/payload.m
run_make release >/dev/null
grep -F 'xcrun-arg:./src/osax/payload.m' "$XCRUN_LOG" >/dev/null
grep -F 'xcrun-arg:./src/manifest.m' "$XCRUN_LOG" >/dev/null
: > "$XCRUN_LOG"
run_make release >/dev/null
test ! -s "$XCRUN_LOG"

printf 'archive\n' > "$WORK/release/keep.tar.gz"
run_make clean-debug
test ! -e "$WORK/bin/debug"
test -x "$WORK/bin/release/yabai"
test -f "$WORK/release/keep.tar.gz"

# Newer legacy blobs in the isolated source copy cannot affect output or inputs.
printf 'poison payload\n' > src/osax/payload_bin.c
printf 'poison loader\n' > src/osax/loader_bin.c
before=$(/usr/bin/shasum -a 256 "$WORK/bin/release/osax/generated.c")
: > "$XCRUN_LOG"
rm -f "$WORK/bin/release/yabai"
run_make release >/dev/null
after=$(/usr/bin/shasum -a 256 "$WORK/bin/release/osax/generated.c")
test "$before" = "$after"
! grep -E 'xcrun-arg:.*src/osax/(payload|loader)_bin\.c' "$XCRUN_LOG"
grep -F 'xcrun-arg:./src/manifest.m' "$XCRUN_LOG" >/dev/null
grep -F 'xcrun-arg:./bin/release/osax/generated.c' "$XCRUN_LOG" >/dev/null

# Real tar layout is installer-compatible; canonical man parity is required and failed creation preserves the old final.
if run_make "ASCIIDOCTOR=$TOOL_ROOT/tools/asciidoctor-bad" "TAR=/usr/bin/tar" archive >/dev/null 2>&1; then exit 1; fi
run_make "TAR=/usr/bin/tar" archive >/dev/null
ARCHIVE=$WORK/release/yabai-v-test.tar.gz
/usr/bin/tar -tzf "$ARCHIVE" | grep -qx 'archive/bin/yabai'
/usr/bin/tar -tzf "$ARCHIVE" | grep -qx 'archive/doc/yabai.1'
! /usr/bin/tar -tzf "$ARCHIVE" | grep -q '^staging/'
archive_hash=$(/usr/bin/shasum -a 256 "$ARCHIVE")
if run_make "TAR=$TOOL_ROOT/tools/tar-fail" archive >/dev/null 2>&1; then exit 1; fi
test "$archive_hash" = "$(/usr/bin/shasum -a 256 "$ARCHIVE")"
test -z "$(find "$WORK/release" \( -name '.archive.*' -o -name '.archive-stage.*' \) -print)"

uid=$(id -u)
TRUSTED='certificate leaf = H"AAAA"'
reset_verify() { printf '0\n' > "$VERIFY_COUNT"; : > "$LOG"; }

# Sign is explicit; deploy only verifies an already signed artifact.
prepare_signed

# Dry-run deploy has no build, generation, icon, or signing commands.
dry_run=$(run_make -n "DEPLOY_REQUIREMENT=$TRUSTED" deploy)
! printf '%s\n' "$dry_run" | grep -E '(^|[ /])(xcrun|xxd)([ /]|$)|clang|seticon\.py|codesign[^;]* -fs '

# Missing or untrusted cryptographic requirements fail closed.
reset_verify
if run_make "DEPLOY_DIR=$WORK/deploy" "DEPLOY_TRUST_ROOT=$TMP" "DEPLOY_OWNER_UID=$uid" deploy >/dev/null 2>&1; then exit 1; fi
! grep -q '^sudo$' "$LOG"
reset_verify
if run_make "DEPLOY_DIR=$WORK/deploy" "DEPLOY_TRUST_ROOT=$TMP" "DEPLOY_OWNER_UID=$uid" 'DEPLOY_REQUIREMENT=certificate leaf = H"BBBB"' deploy >/dev/null 2>&1; then exit 1; fi
! grep -q '^sudo$' "$LOG"
test ! -e "$WORK/deploy/yabai"

# Missing release artifacts and unsigned rebuilds fail before privilege.
rm -f "$WORK/bin/release/yabai"; reset_verify; : > "$XCRUN_LOG"
if DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null 2>&1; then exit 1; fi
! grep -q '^sudo$' "$LOG"
test ! -s "$XCRUN_LOG"
run_make release >/dev/null
reset_verify; : > "$XCRUN_LOG"
if DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null 2>&1; then exit 1; fi
! grep -q '^sudo$' "$LOG"
! grep -q 'codesign:-fs' "$LOG"
test ! -s "$XCRUN_LOG"
prepare_signed

# Replacement immediately before local verification fails without privilege.
reset_verify
if MUTATE_BEFORE_LOCAL=1 DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null 2>&1; then exit 1; fi
unset MUTATE_BEFORE_LOCAL
! grep -q '^sudo$' "$LOG"
! grep -q 'codesign:-fs' "$LOG"
test ! -e "$WORK/deploy/yabai"
prepare_signed

# Successful deploy preserves the signed source, invokes no mutation tools,
# ignores attacker PATH, and orders verification before SA/restart.
source_hash=$(/usr/bin/shasum -a 256 "$WORK/bin/release/yabai")
source_mtime=$(/usr/bin/stat -f %m "$WORK/bin/release/yabai")
reset_verify; : > "$XCRUN_LOG"
ATTACKER_PATH="$TOOL_ROOT/attacker" DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null
unset ATTACKER_PATH
! grep -q '^attacker:' "$LOG"
! grep -q 'codesign:-fs' "$LOG"
! grep -q '^python:' "$LOG"
test ! -s "$XCRUN_LOG"
test "$source_hash" = "$(/usr/bin/shasum -a 256 "$WORK/bin/release/yabai")"
test "$source_mtime" = "$(/usr/bin/stat -f %m "$WORK/bin/release/yabai")"
test "$(grep -F -c 'codesign:--verify --strict -R' "$LOG")" -eq 3
final_verify=$(grep -F -n 'codesign:--verify --strict -R' "$LOG" | tail -1 | cut -d: -f1)
uninstall=$(grep -n 'binary:--uninstall-sa' "$LOG" | cut -d: -f1)
restart=$(grep -n 'binary:--restart-service' "$LOG" | cut -d: -f1)
test "$final_verify" -lt "$uninstall" && test "$uninstall" -lt "$restart"
test -z "$(find "$WORK/deploy" \( -name '.yabai.deploy.*' -o -name '.yabai.backup.*' \) -print)"

# The privileged phase receives only staged bytes and no source pathname.
reset_verify
staged_hash=$(/usr/bin/shasum -a 256 "$WORK/bin/release/yabai" | cut -d ' ' -f 1)
MUTATE_SOURCE=1 SOURCE_TO_MUTATE="$WORK/bin/release/yabai" DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null
unset MUTATE_SOURCE SOURCE_TO_MUTATE
test "$staged_hash" != "$(/usr/bin/shasum -a 256 "$WORK/bin/release/yabai" | cut -d ' ' -f 1)"
test "$staged_hash" = "$(/usr/bin/shasum -a 256 "$WORK/deploy/yabai" | cut -d ' ' -f 1)"
grep -q 'binary:--uninstall-sa' "$LOG"
prepare_signed

# A second verification failure restores a prior destination.
printf 'known destination\n' > "$WORK/deploy/yabai"; chmod +x "$WORK/deploy/yabai"
reset_verify
if FAIL_VERIFY_AT=3 DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null 2>&1; then exit 1; fi
unset FAIL_VERIFY_AT
grep -qx 'known destination' "$WORK/deploy/yabai"
! grep -q 'binary:--uninstall-sa' "$LOG"
test -z "$(find "$WORK/deploy" \( -name '.yabai.deploy.*' -o -name '.yabai.backup.*' \) -print)"

# The same post-rename failure leaves no destination when there was no prior one.
rm -f "$WORK/deploy/yabai"; reset_verify
if FAIL_VERIFY_AT=3 DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null 2>&1; then exit 1; fi
unset FAIL_VERIFY_AT
test ! -e "$WORK/deploy/yabai"
test -z "$(find "$WORK/deploy" \( -name '.yabai.deploy.*' -o -name '.yabai.backup.*' \) -print)"

# SA failure occurs after commit: it returns failure but retains the verified binary.
reset_verify
if FAIL_SA=load-sa DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null 2>&1; then exit 1; fi
unset FAIL_SA
test -x "$WORK/deploy/yabai"
! grep -q 'binary:--restart-service' "$LOG"

# ACL inspection failure is fail-closed before temporary-file creation.
printf 'ACL inspection sentinel\n' > "$WORK/deploy/yabai"; chmod 0755 "$WORK/deploy"; reset_verify
if DEPLOY_REQUIREMENT="$TRUSTED" run_deploy "ACL_LIST=$TOOL_ROOT/tools/acl-list-fail" deploy >/dev/null 2>&1; then exit 1; fi
grep -qx 'ACL inspection sentinel' "$WORK/deploy/yabai"
test -z "$(find "$WORK/deploy" -name '.yabai.deploy.*' -print)"

# A mutation ACL is rejected before temporary-file creation despite mode 0755.
rm -f "$WORK/deploy/yabai"; reset_verify
if ACL_PATH="$WORK/deploy" DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null 2>&1; then exit 1; fi
unset ACL_PATH
test ! -e "$WORK/deploy/yabai"
test -z "$(find "$WORK/deploy" -name '.yabai.deploy.*' -print)"

# Reject writable, symlinked, and noncanonical deployment directory components.
chmod 0777 "$WORK/deploy"
if DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null 2>&1; then exit 1; fi
chmod 0700 "$WORK/deploy"
ln -s "$WORK/deploy" "$WORK/deploy-link"
if run_make "DEPLOY_DIR=$WORK/deploy-link" "DEPLOY_TRUST_ROOT=$TMP" "DEPLOY_OWNER_UID=$uid" "DEPLOY_REQUIREMENT=$TRUSTED" deploy >/dev/null 2>&1; then exit 1; fi
mkdir -p "$TMP/chain"
ln -s "$TMP" "$TMP/chain/link"
if run_make "DEPLOY_DIR=$TMP/chain/link/deploy" "DEPLOY_TRUST_ROOT=$TMP" "DEPLOY_OWNER_UID=$uid" "DEPLOY_REQUIREMENT=$TRUSTED" deploy >/dev/null 2>&1; then exit 1; fi
if run_make "DEPLOY_DIR=$TMP/./deploy" "DEPLOY_TRUST_ROOT=$TMP" "DEPLOY_OWNER_UID=$uid" "DEPLOY_REQUIREMENT=$TRUSTED" deploy >/dev/null 2>&1; then exit 1; fi
if STAT_FOREIGN="$WORK/deploy" DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null 2>&1; then exit 1; fi
unset STAT_FOREIGN
printf 'foreign destination\n' > "$WORK/deploy/yabai"
if STAT_FOREIGN="$WORK/deploy/yabai" DEPLOY_REQUIREMENT="$TRUSTED" run_deploy deploy >/dev/null 2>&1; then exit 1; fi
unset STAT_FOREIGN
grep -qx 'foreign destination' "$WORK/deploy/yabai"

# Cleanup refuses every override and unsafe root without touching sentinels.
mkdir -p "$TMP/cleanup-outside"
printf 'outside sentinel\n' > "$TMP/cleanup-outside/sentinel"
printf 'source sentinel\n' > "$WORK/src/cleanup-sentinel"
for override in '' . / .. "$TMP/cleanup-outside" ./src; do
    if run_make "BUILD_PATH=$override" clean-debug >/dev/null 2>&1; then exit 1; fi
    test -f "$TMP/cleanup-outside/sentinel"
    test -f "$WORK/src/cleanup-sentinel"
done
mv "$WORK/bin" "$WORK/bin.real"
ln -s "$TMP/cleanup-outside" "$WORK/bin"
if run_make clean-debug >/dev/null 2>&1; then exit 1; fi
test -f "$TMP/cleanup-outside/sentinel"
rm "$WORK/bin"; mv "$WORK/bin.real" "$WORK/bin"

for override in '' . / .. "$TMP/cleanup-outside" ./src; do
    if run_make "RELEASE_PATH=$override" distclean >/dev/null 2>&1; then exit 1; fi
    test -f "$TMP/cleanup-outside/sentinel"
    test -f "$WORK/src/cleanup-sentinel"
done
mv "$WORK/release" "$WORK/release.real"
ln -s "$TMP/cleanup-outside" "$WORK/release"
if run_make distclean >/dev/null 2>&1; then exit 1; fi
test -f "$TMP/cleanup-outside/sentinel"
rm "$WORK/release"; mv "$WORK/release.real" "$WORK/release"

printf 'repository build/deploy and clean-refusal tests passed\n'
