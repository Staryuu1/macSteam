# macsteam — standalone dylib

**Unofficial fork** of [Selectively11/macSteam](https://github.com/Selectively11/macSteam),
maintained separately in [Staryuu1/macSteam](https://github.com/Staryuu1/macSteam).
This fork is not an official upstream release. Credit for the original macsteam
project and its dylib implementation belongs to Selectively11 and the original
contributors. The original license is retained in [LICENSE](LICENSE).

macsteam runs inside Steam on Apple Silicon macOS 15 or newer. The standalone
package does not require macSteam Config.app. The x86_64 slice is an inert helper
stub; Intel Macs are not supported.

## Changes in this fork

Compared with the original app-based workflow, this fork makes these changes:

- **Standalone distribution:** ships `macsteam.dylib` with shell installation
  tools, signature profiles, examples, and documentation. No companion app is
  needed to install or configure it.
- **Shell installation and removal:** adds `scripts/install.sh`,
  `scripts/remove.sh`, and a shared helper. Installation follows the original
  app's injection and signing procedure. Removal restores backed-up Steam files
  and update settings while retaining user Lua/configuration.
- **Recovery checks:** backs up the original plist, executable, code signature,
  and update settings; rolls back failed installation attempts; and refuses to
  overwrite a Steam installation changed externally. Missing installed dylibs
  now produce an actionable error instead of a raw `shasum` failure.
- **Direct Lua loading:** adds a C parser to the dylib that merges `lua/*.lua`
  with the existing YAML configuration at startup. Supports app/depot declarations,
  duplicate app merging, and generated files whose first app entry includes a
  key. Invalid files are rejected without applying partial entries.
- **Clear parser diagnostics:** logs rejected files and line numbers, ignored
  metadata, and final merged app/package/depot-group counts. `setManifestid` and
  `addtoken` declarations are accepted but ignored with warnings.
- **Source cleanup:** removes `macsteam-app/`, `launcher/`, `Macsteam.xcodeproj/`,
  `project.yml`, `appcast.xml`, and obsolete app build/test references.
- **Build, checks, and packaging:** adds standalone parser/installer checks,
  example configuration, `make dist`, and a manual-only GitHub Actions workflow
  that builds and uploads `macsteam-standalone.tar.gz`.

The existing hook implementations and signature profile are inherited from the
original project. This fork changes configuration loading and distribution; it
does not add a new unlock engine, Intel support, live Lua reload, manifest pinning,
or access-token handling. ZIP import, the GUI updater, and Steam
download/downgrade/repair tools from the removed app are no longer included.
It is not a feature-equivalent port of BetterSteamTools.

## Install

Extract the standalone archive, quit Steam, and run from the extracted folder:

```bash
bash scripts/install.sh
```

Install Steam and launch it once before installing macsteam. Run the script as
your normal user, without sudo. The default target is `/Applications/Steam.app`;
for another writable installation use:

```bash
STEAM_APP="$HOME/Applications/Steam.app" bash scripts/install.sh
```

The script follows the original companion app's installation procedure:

1. Copy `macsteam.dylib` into `Steam.app/Contents/MacOS`.
2. Deploy signature profiles to the macsteam support directory.
3. Add the dylib to `LSEnvironment:DYLD_INSERT_LIBRARIES` in Steam's Info.plist,
   preserving other injected libraries.
4. Ad-hoc sign the dylib, `steam_osx`, and Steam.app, in that order.
5. Set `BootStrapperInhibitUpdateOnLaunch=enable` in both Steam `steam.cfg` files,
   preserving unrelated settings.
6. Register Steam.app with LaunchServices.

Original executable, plist, code signature, and Steam update settings are backed
up under `~/Library/Application Support/macsteam/installer-backup`. Failed
installation attempts restore Steam automatically. Do not delete the backup.
An existing installation made by the companion app has no script backup: restore
a clean Steam bundle before migrating, keeping your macsteam config directory.

Launch Steam normally afterward. No companion app needs to run.

## Lua and configuration

Put your files here:

```text
~/Library/Application Support/macsteam/
├── config.yaml
├── lua/
│   ├── game-one.lua
│   └── game-two.lua
├── signatures/macos.arm64/
└── macsteam.log
```

The installer creates the folders and a default `config.yaml` only if none exists.
The dylib merges `lua/*.lua` with the YAML configuration on Steam startup, in
filename order. Restart Steam after adding, editing, or removing files. Deleting a
Lua file removes its entries on the next launch, unless they also exist in YAML
or another Lua file.

Supported Lua format is declarative `addappid` calls, one per line:

```lua
addappid(12345) -- app
addappid(12346, 0, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
addappid(12347) -- another app or DLC
```

IDs must be positive signed 32-bit integers; keys must contain exactly 64 hex
characters. The first `addappid` is the main app, including when it carries a key.
Later keyed entries belong to the most recent unkeyed app, or that first app.
Both single and double quotes, whitespace, trailing semicolons, and
`--` line comments are accepted. Duplicate app IDs are merged; later files win
for the same app/depot key.

This is not a full Lua interpreter or feature-equivalent BetterSteamTools port.
Valid `setManifestid` and `addtoken` declarations are accepted as metadata and
ignored with a warning; manifest pinning and access tokens are not implemented.
Loops, conditionals, multiline calls, and ticket directives are unsupported.
A file containing an invalid or unsupported line is
rejected entirely and logged, while other valid files still load. Place `.manifest`
files directly in `~/Library/Application Support/Steam/depotcache` if needed;
there is no ZIP importer in the standalone package.

The existing YAML settings (`Apps`, `PackageIds`, `DepotKeys`, `HideWhatsNew`)
remain available. See `examples/config.yaml` and `examples/example.lua`.
Retain package `20200` in `PackageIds` for the existing license injection path.
If `MACSTEAM_CONFIG` is set, it overrides the YAML path; Lua still loads from the
support directory shown above.

## Remove

Quit Steam and run:

```bash
bash scripts/remove.sh
```

Use the same `STEAM_APP` override if you installed into a custom location. Removal
restores the original files and signatures, including the prior Steam update
settings. Your Lua, YAML, profiles, and logs are preserved.

Re-running installation updates the dylib without replacing the original backup.
If Steam's executable, plist, code signature, injected dylib, or update config
changed externally, installation/removal refuses to overwrite it with an older
backup. Keep the backup for recovery and restore a clean Steam installation.
The installer does not download or downgrade Steam. Runtime hooks still require
a compatible Steam build and the supplied signature profile.

## Build and bundle

Install Xcode Command Line Tools and CMake, then run:

```bash
make test-standalone
make dist
```

The build fetches the pinned Dobby source and produces `out/macsteam.dylib`.
`out/macsteam-standalone.tar.gz` contains the dylib, `scripts/install.sh`,
`scripts/remove.sh`, their shared helper, signature profiles, examples, and docs.
Keep the extracted package together; the scripts use these relative paths.
The repository contains the dylib source and standalone tooling; the companion
app, launcher, and their Xcode projects have been removed.

The installer checks use a temporary signed app bundle and stub dylib. They verify
rollback, reinstall, preservation of other injected libraries/settings, refusal
after external changes, and exact restoration on removal. They do not launch
Steam; LaunchServices registration and process detection are stubbed in the test.

## GitHub Actions

The **Build standalone dylib** workflow has only `workflow_dispatch`; pushes and
pull requests do not trigger it. In GitHub, open **Actions → Build standalone
dylib → Run workflow**. It tests, builds, and uploads the standalone archive as the
`macsteam-standalone` artifact. It does not build an app or publish a release.

## License

See [LICENSE](LICENSE).
