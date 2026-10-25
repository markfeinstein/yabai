#!/bin/sh

#
# This script will install the latest pre-built yabai release from GitHub.
# Depends on curl, shasum, tar, cp, cut, id, mktemp, mv.
#
# ARG1:   Directory in which to store the yabai binary; must be an absolutepath.
#         Fallback: /usr/local/bin
#
# ARG2:   Directory in which to store the yabai man-page; must be an absolutepath.
#         Fallback: /usr/local/share/man/man1
#
# Author: Åsmund Vikane
#   Date: 2024-02-13
#

set -e

PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH

BIN_DIR="$1"
MAN_DIR="$2"

if [ -z "$BIN_DIR" ]; then
    BIN_DIR="/usr/local/bin"
fi

if [ -z "$MAN_DIR" ]; then
    MAN_DIR="/usr/local/share/man/man1"
fi

if [ "${BIN_DIR%%/*}" ]; then
    echo "Error: Binary target directory '${BIN_DIR}' is not an absolutepath."
    exit 1
fi

if [ ! -d "$BIN_DIR" ]; then
    echo "Error: Binary target directory '${BIN_DIR}' does not exist."
    exit 1
fi

if [ ! -w "$BIN_DIR" ]; then
    echo "Error: User does not have write permission for binary target directory '${BIN_DIR}'."
    exit 1
fi

if [ "${MAN_DIR%%/*}" ]; then
    echo "Error: Man-page target directory '${MAN_DIR}' is not an absolutepath."
    exit 1
fi

if [ ! -d "$MAN_DIR" ]; then
    echo "Error: Man-page target directory '${MAN_DIR}' does not exist."
    exit 1
fi

if [ ! -w "$MAN_DIR" ]; then
    echo "Error: User does not have write permission for man-page target directory '${MAN_DIR}'."
    exit 1
fi

AUTHOR="asmvik"
NAME="yabai"
VERSION="7.1.25"
EXPECTED_HASH="76f383841570bfe1e3fd24cedd9b1a4b804b43b0f39c86a951c97d187fc6b1b4"
TMP_BASE=${TMPDIR:-/tmp}
while [ "$TMP_BASE" != / ] && [ "${TMP_BASE%/}" != "$TMP_BASE" ]; do
    TMP_BASE=${TMP_BASE%/}
done
TMP_DIR=
BIN_STAGE_DIR=
MAN_STAGE_DIR=
BIN_BACKUP=
MAN_BACKUP=
BIN_BACKUP_INTENT=0
MAN_BACKUP_INTENT=0
BIN_FINAL_INTENT=0
MAN_FINAL_INTENT=0
COMMIT_STARTED=0
COMMIT_FINISHED=0

case "$TMP_BASE" in
    /*) ;;
    *)
        echo "Error: Temporary directory base '${TMP_BASE}' is not an absolutepath."
        exit 1
        ;;
esac
TMP_DIR_PREFIX="${TMP_BASE%/}/${AUTHOR}-${NAME}-v${VERSION}-installer."

cleanup()
{
    status=$?
    trap - 0 HUP INT TERM
    set +e

    if [ "$COMMIT_STARTED" -eq 1 ] && [ "$COMMIT_FINISHED" -eq 0 ]; then
        # Infer completed renames from filesystem state: mv may rename and still
        # report failure. A backup payload always takes precedence over a final
        # staged payload when rolling the transaction back.
        if [ "$BIN_BACKUP_INTENT" -eq 1 ] && { [ -e "$BIN_BACKUP/$NAME" ] || [ -L "$BIN_BACKUP/$NAME" ]; }; then
            if [ ! -d "$BIN_DIR/$NAME" ]; then
                rm -f "$BIN_DIR/$NAME"
                mv -f "$BIN_BACKUP/$NAME" "$BIN_DIR/$NAME"
            fi
        elif [ "$BIN_FINAL_INTENT" -eq 1 ] && [ ! -e "$BIN_STAGE_DIR/$NAME" ] && [ ! -L "$BIN_STAGE_DIR/$NAME" ] && [ ! -d "$BIN_DIR/$NAME" ]; then
            rm -f "$BIN_DIR/$NAME"
        fi
        if [ "$MAN_BACKUP_INTENT" -eq 1 ] && { [ -e "$MAN_BACKUP/$NAME.1" ] || [ -L "$MAN_BACKUP/$NAME.1" ]; }; then
            if [ ! -d "$MAN_DIR/$NAME.1" ]; then
                rm -f "$MAN_DIR/$NAME.1"
                mv -f "$MAN_BACKUP/$NAME.1" "$MAN_DIR/$NAME.1"
            fi
        elif [ "$MAN_FINAL_INTENT" -eq 1 ] && [ ! -e "$MAN_STAGE_DIR/$NAME.1" ] && [ ! -L "$MAN_STAGE_DIR/$NAME.1" ] && [ ! -d "$MAN_DIR/$NAME.1" ]; then
            rm -f "$MAN_DIR/$NAME.1"
        fi
    fi

    case "$BIN_STAGE_DIR" in
        "$BIN_DIR"/.yabai-install.*)
            [ -n "$BIN_STAGE_DIR" ] && [ -d "$BIN_STAGE_DIR" ] && rm -R "$BIN_STAGE_DIR"
            ;;
    esac
    case "$MAN_STAGE_DIR" in
        "$MAN_DIR"/.yabai-install.*)
            [ -n "$MAN_STAGE_DIR" ] && [ -d "$MAN_STAGE_DIR" ] && rm -R "$MAN_STAGE_DIR"
            ;;
    esac
    case "$BIN_BACKUP" in
        "$BIN_DIR"/.yabai-backup.*)
            if [ "$COMMIT_FINISHED" -eq 1 ] || { [ ! -e "$BIN_BACKUP/$NAME" ] && [ ! -L "$BIN_BACKUP/$NAME" ]; }; then
                rm -R "$BIN_BACKUP"
            fi
            ;;
    esac
    case "$MAN_BACKUP" in
        "$MAN_DIR"/.yabai-backup.*)
            if [ "$COMMIT_FINISHED" -eq 1 ] || { [ ! -e "$MAN_BACKUP/$NAME.1" ] && [ ! -L "$MAN_BACKUP/$NAME.1" ]; }; then
                rm -R "$MAN_BACKUP"
            fi
            ;;
    esac
    case "$TMP_DIR" in
        "$TMP_DIR_PREFIX"*) [ -n "$TMP_DIR" ] && [ -d "$TMP_DIR" ] && rm -R "$TMP_DIR" ;;
    esac

    exit "$status"
}

validate_destinations()
{
    if [ -d "$BIN_DIR/$NAME" ]; then
        echo "Error: Binary destination '$BIN_DIR/$NAME' is a directory."
        return 1
    fi
    if [ -d "$MAN_DIR/$NAME.1" ]; then
        echo "Error: Man-page destination '$MAN_DIR/$NAME.1' is a directory."
        return 1
    fi
}

trap cleanup 0
trap 'exit 1' HUP INT TERM

TMP_DIR=$(mktemp -d "${TMP_DIR_PREFIX}XXXXXX")
ARCHIVE="${NAME}-v${VERSION}.tar.gz"
URL="https://github.com/${AUTHOR}/${NAME}/releases/download/v${VERSION}/${ARCHIVE}"

cd "$TMP_DIR"
curl --fail --location --remote-name "$URL"
FILE_HASH=$(shasum -a 256 "$ARCHIVE" | cut -d " " -f 1)

if [ "$FILE_HASH" != "$EXPECTED_HASH" ]; then
    echo "Hash does not match the expected value.. abort."
    echo "Expected hash: $EXPECTED_HASH"
    echo "  Actual hash: $FILE_HASH"
    exit 1
fi

echo "Hash verified. Preparing files.."
tar -xzf "$ARCHIVE"

if [ ! -f "archive/bin/$NAME" ] || [ ! -f "archive/doc/$NAME.1" ]; then
    echo "Error: Release archive does not contain the expected files."
    exit 1
fi

validate_destinations

BIN_STAGE_DIR=$(mktemp -d "$BIN_DIR/.yabai-install.XXXXXX")
MAN_STAGE_DIR=$(mktemp -d "$MAN_DIR/.yabai-install.XXXXXX")
BIN_BACKUP=$(mktemp -d "$BIN_DIR/.yabai-backup.XXXXXX")
MAN_BACKUP=$(mktemp -d "$MAN_DIR/.yabai-backup.XXXXXX")
cp "archive/bin/$NAME" "$BIN_STAGE_DIR/$NAME"
cp "archive/doc/$NAME.1" "$MAN_STAGE_DIR/$NAME.1"
chmod 0755 "$BIN_STAGE_DIR/$NAME"
chmod 0644 "$MAN_STAGE_DIR/$NAME.1"

# Validate again immediately before any destination is moved.
validate_destinations
COMMIT_STARTED=1
if [ -e "$BIN_DIR/$NAME" ] || [ -L "$BIN_DIR/$NAME" ]; then
    BIN_BACKUP_INTENT=1
    mv -f "$BIN_DIR/$NAME" "$BIN_BACKUP/$NAME"
fi
if [ -e "$MAN_DIR/$NAME.1" ] || [ -L "$MAN_DIR/$NAME.1" ]; then
    MAN_BACKUP_INTENT=1
    mv -f "$MAN_DIR/$NAME.1" "$MAN_BACKUP/$NAME.1"
fi

# A final check prevents mv from treating an exact destination as a directory.
validate_destinations
BIN_FINAL_INTENT=1
mv -f "$BIN_STAGE_DIR/$NAME" "$BIN_DIR/$NAME"
validate_destinations
MAN_FINAL_INTENT=1
mv -f "$MAN_STAGE_DIR/$NAME.1" "$MAN_DIR/$NAME.1"
COMMIT_FINISHED=1

rm -R "$BIN_BACKUP" "$MAN_BACKUP"
BIN_BACKUP=
MAN_BACKUP=
rmdir "$BIN_STAGE_DIR" "$MAN_STAGE_DIR"
BIN_STAGE_DIR=
MAN_STAGE_DIR=

echo "Finished copying files.."
echo ""
echo "If you want yabai to be managed by launchd (start automatically upon login):"
echo "  yabai --start-service"
echo ""
echo "When running as a launchd service logs will be found in:"
echo "  /tmp/yabai_<user>.[out|err].log"
echo ""
echo "If you are using the scripting-addition; remember to update your sudoers file:"
echo "  sudo visudo -f /private/etc/sudoers.d/yabai"
echo ""
echo "Sudoers file configuration row:"
SUDOERS_USER=
EFFECTIVE_UID=$(id -u)
if [ "$EFFECTIVE_UID" = 0 ]; then
    case ${SUDO_USER:-} in
        ""|*[!A-Za-z0-9_.-]*|[0-9.-]*) ;;
        *)
            SUDOERS_UID=$(id -u "$SUDO_USER" 2>/dev/null) || SUDOERS_UID=
            case $SUDOERS_UID in
                ""|0|*[!0-9]*) ;;
                *)
                    RESOLVED_SUDO_USER=$(id -un "$SUDOERS_UID" 2>/dev/null) || RESOLVED_SUDO_USER=
                    [ "$RESOLVED_SUDO_USER" = "$SUDO_USER" ] && SUDOERS_USER=$RESOLVED_SUDO_USER
                    ;;
            esac
            ;;
    esac
else
    SUDOERS_USER=$(id -un 2>/dev/null) || SUDOERS_USER=
fi

SUDOERS_BINARY=
case "$BIN_DIR/yabai" in
    /*)
        case "$BIN_DIR/yabai" in
            *[!A-Za-z0-9_./-]*) ;;
            *) SUDOERS_BINARY="$BIN_DIR/yabai" ;;
        esac
        ;;
esac

if [ -n "$SUDOERS_USER" ] && [ -n "$SUDOERS_BINARY" ]; then
    echo "  $SUDOERS_USER ALL=(root) NOPASSWD: sha256:$(shasum -a 256 "$SUDOERS_BINARY" | cut -d " " -f 1) $SUDOERS_BINARY --load-sa"
else
    echo "  Unable to determine a non-root invoking user; no sudoers row was generated."
fi
echo ""
echo "README: https://github.com/asmvik/yabai/wiki/Installing-yabai-%28latest-release%29#configure-scripting-addition"
