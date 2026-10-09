#!/bin/bash
# Shared by install.sh and remove.sh.
set -euo pipefail

fail() { echo "macsteam: $*" >&2; exit 1; }

[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || fail "Requires Apple Silicon macOS."
[[ $(sw_vers -productVersion | cut -d. -f1) -ge 15 ]] || fail "Requires macOS 15 or newer."
[[ $EUID != 0 ]] || fail "Run as your normal user, without sudo."
STEAM_APP=${STEAM_APP:-/Applications/Steam.app}
[[ $STEAM_APP == /* && $STEAM_APP != *:* && $STEAM_APP != *$'\n'* ]] || fail "STEAM_APP must be an absolute path without colon/newline."
STATE="$HOME/Library/Application Support/macsteam"
PLIST="$STEAM_APP/Contents/Info.plist"
EXE="$STEAM_APP/Contents/MacOS/steam_osx"
PAYLOAD="$STEAM_APP/Contents/MacOS/macsteam.dylib"
SEAL="$STEAM_APP/Contents/_CodeSignature"
BACKUP="$STATE/installer-backup"
STEAM_ROOT="$HOME/Library/Application Support/Steam"
CFG_ROOT="$STEAM_ROOT/steam.cfg"
CFG_INNER="$STEAM_ROOT/Steam.AppBundle/Steam/Contents/MacOS/steam.cfg"
[[ -f $PLIST && -f $EXE ]] || fail "Steam not found at $STEAM_APP. Install and launch Steam once first."
[[ -w $PLIST && -w $EXE && -w $(dirname "$EXE") ]] || fail "No write access to Steam. Use a writable Steam installation (STEAM_APP=/path/Steam.app)."
if pgrep -u "$(id -u)" -x steam_osx >/dev/null; then
    fail "Quit Steam before continuing."
fi
mkdir -p "$STATE"
LOCK="$STATE/.installer-lock"
mkdir "$LOCK" 2>/dev/null || fail "Another installer is running (lock: $LOCK)."
TRANSACTION=""
ROLLBACK=0
NEW_BACKUP=0

snapshot() {
    mkdir -p "$1"
    cp -p "$PLIST" "$1/Info.plist"
    cp -p "$EXE" "$1/steam_osx"
    if [[ -d $SEAL ]]; then cp -Rp "$SEAL" "$1/_CodeSignature"; fi
    if [[ -f $PAYLOAD ]]; then cp -p "$PAYLOAD" "$1/macsteam.dylib"; fi
    if [[ -f $CFG_ROOT ]]; then cp -p "$CFG_ROOT" "$1/root.steam.cfg"; fi
    if [[ -f $CFG_INNER ]]; then cp -p "$CFG_INNER" "$1/inner.steam.cfg"; fi
    printf '%s\n' "$STEAM_APP" > "$1/app.path"
}

restore() {
    cp -p "$1/Info.plist" "$PLIST" || return
    cp -p "$1/steam_osx" "$EXE" || return
    rm -rf "$SEAL" || return
    if [[ -d $1/_CodeSignature ]]; then cp -Rp "$1/_CodeSignature" "$SEAL" || return; fi
    if [[ -f $1/macsteam.dylib ]]; then
        cp -p "$1/macsteam.dylib" "$PAYLOAD" || return
    else
        rm -f "$PAYLOAD" || return
    fi
    restore_cfg "$1/root.steam.cfg" "$CFG_ROOT" || return
    restore_cfg "$1/inner.steam.cfg" "$CFG_INNER" || return
}

restore_cfg() {
    if [[ -f $1 ]]; then
        mkdir -p "$(dirname "$2")" || return
        cp -p "$1" "$2" || return
    else
        rm -f "$2" || return
    fi
}

finish() {
    local status=$?
    trap - EXIT
    if [[ $ROLLBACK == 1 ]]; then
        echo "macsteam: restoring Steam after failure..." >&2
        if ! restore "$TRANSACTION"; then
            echo "macsteam: restore failed; keep recovery files in $TRANSACTION and $BACKUP." >&2
            rmdir "$LOCK"
            exit 1
        fi
        if [[ $NEW_BACKUP == 1 ]]; then rm -rf "$BACKUP"; fi
    fi
    if [[ -n $TRANSACTION ]]; then rm -rf "$TRANSACTION"; fi
    rmdir "$LOCK"
    exit "$status"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

fingerprint() {
    shasum -a 256 "$PLIST" "$EXE" "$PAYLOAD" || return
    if [[ -f $SEAL/CodeResources ]]; then shasum -a 256 "$SEAL/CodeResources" || return; fi
    for cfg in "$CFG_ROOT" "$CFG_INNER"; do
        if [[ -f $cfg ]]; then shasum -a 256 "$cfg" || return; else printf 'missing: %s\n' "$cfg"; fi
    done
}

check_backup() {
    [[ -f $BACKUP/app.path && -f $BACKUP/Info.plist && -f $BACKUP/steam_osx && -f $BACKUP/installed.sha256 ]] || fail "Installer backup is incomplete: $BACKUP"
    [[ $(cat "$BACKUP/app.path") == "$STEAM_APP" ]] || fail "Backup belongs to a different Steam installation."
    [[ -f $PAYLOAD ]] || fail "Previous installation backup exists, but $PAYLOAD is missing. Steam was modified or replaced after installation. Restore a clean Steam bundle, verify its signature, then move $BACKUP aside before installing again. Keep the backup for recovery; do not delete your config or Lua."
    if ! fingerprint > "$STATE/.installed-check"; then
        rm -f "$STATE/.installed-check"
        fail "Cannot read installed Steam files. Keep $BACKUP for recovery."
    fi
    if ! cmp -s "$STATE/.installed-check" "$BACKUP/installed.sha256"; then
        rm -f "$STATE/.installed-check"
        fail "Steam changed since installation. Refusing to overwrite it with an older backup. Restore a clean Steam installation before installing again; keep $BACKUP for recovery."
    fi
    rm -f "$STATE/.installed-check"
    codesign --verify "$STEAM_APP" || fail "Steam bundle resources changed; refusing to restore an older backup."
}

begin_transaction() {
    TRANSACTION=$(mktemp -d "$STATE/.transaction.XXXXXX")
    snapshot "$TRANSACTION"
    ROLLBACK=1
}

register_steam() {
    /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$STEAM_APP"
}
