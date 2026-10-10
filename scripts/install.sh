#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
source "$ROOT/scripts/install-common.sh"

SOURCE="$ROOT/macsteam.dylib"
if [[ ! -f $SOURCE ]]; then SOURCE="$ROOT/out/macsteam.dylib"; fi
[[ -f $SOURCE ]] || fail "macsteam.dylib is missing. Run make first, or use the standalone release."
lipo "$SOURCE" -verify_arch arm64 && lipo "$SOURCE" -verify_arch x86_64 || fail "Payload must contain arm64 and the x86_64 helper stub."
codesign --verify "$SOURCE" || fail "Payload signature is invalid. Rebuild it."
[[ -d $ROOT/signatures/macos.arm64 ]] || fail "Signature profiles are missing."
profiles=("$ROOT"/signatures/macos.arm64/*.json)
[[ -f ${profiles[0]} ]] || fail "No signature profiles found."

if [[ -d $BACKUP ]]; then
    check_backup
else
    [[ ! -e $PAYLOAD ]] || fail "An older macsteam installation exists without a script backup. Restore clean Steam before migrating."
fi
begin_transaction
if [[ ! -d $BACKUP ]]; then
    NEW_BACKUP=1
    cp -Rp "$TRANSACTION" "$BACKUP"
fi

mkdir -p "$STATE/lua" "$STATE/signatures/macos.arm64"
cp -p "${profiles[@]}" "$STATE/signatures/macos.arm64/"
if [[ ! -e $STATE/config.yaml ]]; then
    cat > "$STATE/config.yaml" <<'YAML'
Apps:
PackageIds:
  - 20200
DepotKeys:
HideWhatsNew: false
YAML
fi
cp "$SOURCE" "$PAYLOAD"
codesign -f -s - "$PAYLOAD"

insert=$(/usr/libexec/PlistBuddy -c 'Print :LSEnvironment:DYLD_INSERT_LIBRARIES' "$PLIST" 2>/dev/null || true)
case ":$insert:" in
    *":$PAYLOAD:"*) ;;
    *) insert="$PAYLOAD${insert:+:$insert}" ;;
esac
if ! /usr/libexec/PlistBuddy -c 'Print :LSEnvironment' "$PLIST" >/dev/null 2>&1; then
    /usr/libexec/PlistBuddy -c 'Add :LSEnvironment dict' "$PLIST"
fi
plutil -replace LSEnvironment.DYLD_INSERT_LIBRARIES -string "$insert" "$PLIST"
codesign -f -s - "$EXE"
codesign -f -s - "$STEAM_APP"
codesign --verify "$STEAM_APP"
for cfg in "$CFG_ROOT" "$CFG_INNER"; do
    mkdir -p "$(dirname "$cfg")"
    # Preserve unrelated settings, replacing only the update-block setting.
    tmp=$(mktemp "$(dirname "$cfg")/.steam.cfg.XXXXXX")
    if [[ -f $cfg ]]; then sed '/^[[:space:]]*BootStrapperInhibitUpdateOnLaunch[[:space:]]*=/d' "$cfg" > "$tmp"; fi
    printf '\nBootStrapperInhibitUpdateOnLaunch=enable\n' >> "$tmp"
    if [[ -f $cfg ]]; then cat "$tmp" > "$cfg"; rm "$tmp"; else mv "$tmp" "$cfg"; fi
done
register_steam
fingerprint > "$TRANSACTION/installed.sha256"
mv "$TRANSACTION/installed.sha256" "$BACKUP/installed.sha256"
ROLLBACK=0
echo "Installed. Launch Steam normally; no macSteam Config.app is required."
echo "Put .lua files in: $STATE/lua"
echo "Edit config in: $STATE/config.yaml"
echo "Lua/app changes hot reload automatically; PackageIds changes require a Steam restart."
