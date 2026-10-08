#!/bin/sh
set -eu
repo=$(cd "$(dirname "$0")/.." && pwd -P)
root=$(mktemp -d "${TMPDIR:-/tmp}/yabai-man-check.XXXXXX")
cleanup() { find "$root" -mindepth 1 -delete; rmdir "$root"; }
trap cleanup 0 HUP INT TERM
mkdir -p "$root/doc" "$root/tests"
cp "$repo/makefile" "$root/makefile"
cp "$repo/doc/yabai.asciidoc" "$root/doc/yabai.asciidoc"
cp "$repo/doc/yabai.1" "$root/doc/yabai.1"
cp "$repo/tests/doc_contract.sh" "$root/tests/doc_contract.sh"
cat > "$root/asciidoctor" <<'EOF'
#!/bin/sh
set -eu
output= docdate=
while [ $# -gt 0 ]; do
    case "$1" in
        -a) shift; docdate=$1 ;;
        -o) shift; output=$1 ;;
    esac
    shift
done
[ "$docdate" = 'docdate=2026-10-07' ]
cp "$MAN_SOURCE" "$output"
touch "$MAN_MARKER"
EOF
chmod +x "$root/asciidoctor"
make -C "$root" man-contract >/dev/null
DOC_CONTRACT_SOURCE="$root/doc/yabai.asciidoc" DOC_CONTRACT_MAN="$root/doc/yabai.1" \
    TMPDIR="$root" /bin/sh -c 'mkdir "$TMPDIR/yabai-doc-contract.$$"; exec "$1" --self-test' sh "$root/tests/doc_contract.sh" >/dev/null
if make -C "$root" ASCIIDOCTOR="$root/missing-asciidoctor" man-check >/dev/null 2>&1; then
    echo 'man-check accepted a missing generator' >&2
    exit 1
fi
MAN_SOURCE="$root/doc/yabai.1" MAN_MARKER="$root/asciidoctor-ran" make -C "$root" ASCIIDOCTOR="$root/asciidoctor" man-check >/dev/null
test -f "$root/asciidoctor-ran"

printf '%s\n' ':revdate: 2026-10-07 unexpected' >> "$root/doc/yabai.asciidoc"
rm "$root/asciidoctor-ran"
if MAN_SOURCE="$root/doc/yabai.1" MAN_MARKER="$root/asciidoctor-ran" make -C "$root" ASCIIDOCTOR="$root/asciidoctor" man-check >/dev/null 2>&1; then
    echo 'man-check accepted a malformed duplicate revdate' >&2
    exit 1
fi
test ! -e "$root/asciidoctor-ran"
cp "$repo/doc/yabai.asciidoc" "$root/doc/yabai.asciidoc"

sed 's/menubar_opacity\* \['"'"'<UNIT_SEL>'"'"'\]/menubar_opacity* ['"'"'<POSITIVE_UNIT_SEL>'"'"']/' "$root/doc/yabai.asciidoc" > "$root/doc/swap"
mv "$root/doc/swap" "$root/doc/yabai.asciidoc"
if MAN_SOURCE="$root/doc/yabai.1" MAN_MARKER="$root/asciidoctor-ran" make -C "$root" ASCIIDOCTOR="$root/asciidoctor" man-check >/dev/null 2>&1; then
    echo 'man-check accepted a swapped selector with Asciidoctor available' >&2
    exit 1
fi
test ! -e "$root/asciidoctor-ran"
echo 'man-check tooling branch fixture passed'
