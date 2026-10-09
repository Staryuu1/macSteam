#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d /tmp/macsteam-install.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
export HOME="$WORK/home"
export STEAM_APP="$WORK/Steam Test.app"
PACKAGE="$WORK/package"
mkdir -p "$HOME" "$STEAM_APP/Contents/MacOS" "$PACKAGE/scripts" "$WORK/bin"
cp "$ROOT"/scripts/{install,remove,install-common}.sh "$PACKAGE/scripts/"
cp -R "$ROOT/examples" "$ROOT/signatures" "$PACKAGE/"
# Avoid registering test bundles or checking real Steam processes.
printf '\nregister_steam() { :; }\n' >> "$PACKAGE/scripts/install-common.sh"
printf '#!/bin/bash\nexit 1\n' > "$WORK/bin/pgrep"
cat > "$WORK/bin/codesign" <<'SH'
#!/bin/bash
if [[ ${FAIL_SIGN:-0} == 1 && $* == *'-f -s -'* && ${!#} == "$STEAM_APP/Contents/MacOS/steam_osx" ]]; then exit 1; fi
exec /usr/bin/codesign "$@"
SH
chmod +x "$WORK/bin/pgrep" "$WORK/bin/codesign"
export PATH="$WORK/bin:$PATH"
cat > "$STEAM_APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>local.macsteam.install-check</string>
<key>CFBundleExecutable</key><string>steam_osx</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>LSEnvironment</key><dict><key>DYLD_INSERT_LIBRARIES</key><string>/tmp/other.dylib</string></dict>
</dict></plist>
PLIST
printf 'int main(void) { return 0; }\n' > "$WORK/stub.c"
clang -arch arm64 "$WORK/stub.c" -o "$STEAM_APP/Contents/MacOS/steam_osx"
codesign -f -s - "$STEAM_APP"
clang -arch arm64 -dynamiclib "$WORK/stub.c" -o "$WORK/arm64.dylib"
clang -arch x86_64 -dynamiclib "$WORK/stub.c" -o "$WORK/x86_64.dylib"
lipo -create "$WORK/arm64.dylib" "$WORK/x86_64.dylib" -output "$PACKAGE/macsteam.dylib"
codesign -f -s - "$PACKAGE/macsteam.dylib"
cp -Rp "$STEAM_APP" "$WORK/original.app"
STEAM_ROOT="$HOME/Library/Application Support/Steam"
mkdir -p "$STEAM_ROOT"
printf 'UnrelatedSetting=1\nBootStrapperInhibitUpdateOnLaunch=disable\n' > "$STEAM_ROOT/steam.cfg"
cp "$STEAM_ROOT/steam.cfg" "$WORK/original.cfg"
if FAIL_SIGN=1 bash "$PACKAGE/scripts/install.sh"; then echo 'Expected sign failure' >&2; exit 1; fi
diff -qr "$WORK/original.app" "$STEAM_APP"
cmp "$WORK/original.cfg" "$STEAM_ROOT/steam.cfg"
[[ ! -d $HOME/Library/Application\ Support/macsteam/installer-backup ]]
bash "$PACKAGE/scripts/install.sh"
insert=$(/usr/libexec/PlistBuddy -c 'Print :LSEnvironment:DYLD_INSERT_LIBRARIES' "$STEAM_APP/Contents/Info.plist")
[[ $insert == "$STEAM_APP/Contents/MacOS/macsteam.dylib:/tmp/other.dylib" ]]
grep -q '^UnrelatedSetting=1$' "$STEAM_ROOT/steam.cfg"
grep -q '^BootStrapperInhibitUpdateOnLaunch=enable$' "$STEAM_ROOT/steam.cfg"
STATE="$HOME/Library/Application Support/macsteam"
printf 'addappid(42)\n' > "$STATE/lua/user.lua"
printf '\n# user config\n' >> "$STATE/config.yaml"
cp "$STATE/config.yaml" "$WORK/user.yaml"
bash "$PACKAGE/scripts/install.sh"
cp "$STEAM_APP/Contents/MacOS/macsteam.dylib" "$WORK/installed.dylib"
cp "$STATE/installer-backup/installed.sha256" "$WORK/installed.sha256"
rm "$STEAM_APP/Contents/MacOS/macsteam.dylib"
for action in install remove; do
    if bash "$PACKAGE/scripts/$action.sh" > "$WORK/missing.log" 2>&1; then
        echo "Expected missing-payload refusal: $action" >&2; exit 1
    fi
    grep -q 'Previous installation backup exists.*is missing' "$WORK/missing.log"
    if grep -q 'shasum:' "$WORK/missing.log"; then echo 'Unexpected raw checksum error' >&2; exit 1; fi
done
cmp "$WORK/installed.sha256" "$STATE/installer-backup/installed.sha256"
cp "$WORK/installed.dylib" "$STEAM_APP/Contents/MacOS/macsteam.dylib"
cp "$STEAM_APP/Contents/Info.plist" "$WORK/installed.plist"
printf '\n' >> "$STEAM_APP/Contents/Info.plist"
if bash "$PACKAGE/scripts/remove.sh"; then echo 'Expected changed-bundle refusal' >&2; exit 1; fi
cp "$WORK/installed.plist" "$STEAM_APP/Contents/Info.plist"
bash "$PACKAGE/scripts/remove.sh"
diff -qr "$WORK/original.app" "$STEAM_APP"
cmp "$WORK/original.cfg" "$STEAM_ROOT/steam.cfg"
[[ ! -e $STEAM_ROOT/Steam.AppBundle/Steam/Contents/MacOS/steam.cfg ]]
[[ -f $STATE/lua/user.lua ]]
cmp "$WORK/user.yaml" "$STATE/config.yaml"
codesign --verify "$STEAM_APP"
echo 'Installer rollback, reinstall, removal, and preservation checks passed'
