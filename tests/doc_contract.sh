#!/bin/sh
set -eu

source_doc=${DOC_CONTRACT_SOURCE:-doc/yabai.asciidoc}
generated_doc=${DOC_CONTRACT_MAN:-doc/yabai.1}
[ -f "$source_doc" ] || { source_doc=../doc/yabai.asciidoc; generated_doc=../doc/yabai.1; }

require_source() { grep -Fqx "$1" "$source_doc"; }
require_man() { grep -Fqx "$1" "$generated_doc"; }
check_contract() {
    require_source 'UNIT_SEL             := 0.0 <= <value> <= 1.0'
    require_source 'POSITIVE_UNIT_SEL    := 0.0 < <value> <= 1.0'
    require_source 'SPLIT_RATIO_SEL      := 0.1 <= <value> <= 0.9'
    require_source 'DURATION_SEL         := finite <value> >= 0.0'
    require_source 'OPACITY_DURATION_SEL := finite 0.0 <= <value> <= INT_MAX / 1000 seconds'

    require_source "*menubar_opacity* ['<UNIT_SEL>']::"
    require_source "*window_opacity_duration* ['<OPACITY_DURATION_SEL>']::"
    require_source "*active_window_opacity* ['<POSITIVE_UNIT_SEL>']::"
    require_source "*normal_window_opacity* ['<POSITIVE_UNIT_SEL>']::"
    require_source "*window_animation_duration* ['<DURATION_SEL>']::"
    require_source "*split_ratio* ['<SPLIT_RATIO_SEL>']::"
    require_source "*--opacity* '<UNIT_SEL>'::"
    require_source "*opacity='<UNIT_SEL>'*::"
    require_source '*--daemon-identity*::'
    require_source '    Retrieve diagnostic identity information for the running daemon. This command accepts no properties or trailing arguments. +'
    grep -Fq 'The JSON object contains `pid`' "$source_doc"
    grep -Fq '`architecture` (`arm64` or `x86_64`)' "$source_doc"
    grep -Fq '`uuid` (the Mach-O UUID' "$source_doc"

    require_man 'UNIT_SEL\&             := 0.0 <= <value> <= 1.0'
    require_man 'POSITIVE_UNIT_SEL\&    := 0.0 < <value> <= 1.0'
    require_man 'SPLIT_RATIO_SEL\&      := 0.1 <= <value> <= 0.9'
    require_man 'DURATION_SEL\&         := finite <value> >= 0.0'
    require_man 'OPACITY_DURATION_SEL\& := finite 0.0 <= <value> <= INT_MAX / 1000 seconds'
    require_man '\fBmenubar_opacity\fP [\fI<UNIT_SEL>\fP]'
    require_man '\fBwindow_opacity_duration\fP [\fI<OPACITY_DURATION_SEL>\fP]'
    require_man '\fBactive_window_opacity\fP [\fI<POSITIVE_UNIT_SEL>\fP]'
    require_man '\fBnormal_window_opacity\fP [\fI<POSITIVE_UNIT_SEL>\fP]'
    require_man '\fBwindow_animation_duration\fP [\fI<DURATION_SEL>\fP]'
    require_man '\fBsplit_ratio\fP [\fI<SPLIT_RATIO_SEL>\fP]'
    require_man '\fB\-\-opacity\fP \fI<UNIT_SEL>\fP'
    require_man '\fBopacity=\fI<UNIT_SEL>\fP\fP'
    require_man '\fB\-\-daemon\-identity\fP'
    grep -Fq 'This command accepts no properties or trailing arguments' "$generated_doc"
    grep -Fq '\f(CWpid\fP' "$generated_doc"
    grep -Fq '\f(CWarchitecture\fP' "$generated_doc"
    grep -Fq '\f(CWuuid\fP' "$generated_doc"

    ! grep -Fq 'FLOAT_SEL' "$source_doc"
    ! grep -Fq 'FLOAT_SEL' "$generated_doc"
}

check_contract

if [ "${1:-}" = --self-test ]; then
    temp=${TMPDIR:-/tmp}/yabai-doc-contract.$$
    mkdir "$temp"
    trap 'rm -f "$temp/source" "$temp/man"; rmdir "$temp"' 0 HUP INT TERM
    cp "$source_doc" "$temp/source"
    cp "$generated_doc" "$temp/man"
    sed 's/menubar_opacity\* \['"'"'<UNIT_SEL>'"'"'\]/menubar_opacity* ['"'"'<POSITIVE_UNIT_SEL>'"'"']/' "$temp/source" > "$temp/source.swap"
    mv "$temp/source.swap" "$temp/source"
    ! cmp -s "$source_doc" "$temp/source" || { echo 'selector-swap fixture did not modify source' >&2; exit 1; }
    if DOC_CONTRACT_SOURCE="$temp/source" DOC_CONTRACT_MAN="$temp/man" "$0" >/dev/null 2>&1; then
        echo 'documentation contract accepted a deliberate source selector swap' >&2
        exit 1
    fi
    cp "$source_doc" "$temp/source"
    sed 's/menubar_opacity\\fP \[\\fI<UNIT_SEL>/menubar_opacity\\fP [\\fI<POSITIVE_UNIT_SEL>/' "$temp/man" > "$temp/man.swap"
    mv "$temp/man.swap" "$temp/man"
    ! cmp -s "$generated_doc" "$temp/man" || { echo 'manpage selector-swap fixture did not modify output' >&2; exit 1; }
    if DOC_CONTRACT_SOURCE="$temp/source" DOC_CONTRACT_MAN="$temp/man" "$0" >/dev/null 2>&1; then
        echo 'documentation contract accepted a deliberate manpage selector swap' >&2
        exit 1
    fi
fi

echo 'documentation numeric-domain contract passed'
