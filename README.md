# macsteam

This is an unofficial fork of [Selectively11/macSteam](https://github.com/Selectively11/macSteam).
All credit for the original project goes to its author and contributors.

This project is intended only for personal experiment.

This fork removes the config app and launcher. You install the dylib with shell
scripts and put Lua files in a folder. The original README is in [oldreadme.md](oldreadme.md).

## Install and use

Apple Silicon only, macOS 15+. Install Steam and launch it once first.
If you used the old config app, start with a clean Steam installation.

Download `macsteam-standalone.tar.gz` from [Releases](https://github.com/Staryuu1/macSteam/releases).
Releases use macSteam versions, such as `v0.2.0`. Check the release description
for the compatible Steam version. See [CHANGELOG.md](CHANGELOG.md) for changes.
Or build it yourself with `make dist`.

Extract the bundle, quit Steam, and run this from the extracted folder without sudo:

```bash
bash scripts/install.sh
```

Put your `.lua` files here:

```text
~/Library/Application Support/macsteam/lua/
```

Open Steam normally. Additional settings are in
`~/Library/Application Support/macsteam/config.yaml`; see
[examples/config.yaml](examples/config.yaml) for the supported format.

Lua files support one declarative call per line; function names are case-insensitive.
To pin a depot's manifest for a configured app, add:

```lua
addappid(12345)
setManifestid(12346, "1234567890123456789")
```

Replace the example app ID, depot ID, and manifest GID before use. GIDs must be
quoted positive uint64 decimals. An optional third size argument is accepted and
validated, but Steam's original size is preserved. Only matching depots in the
primary dependency vector are changed; the separate shared-depot vector is preserved.
Duplicate pins use the last declaration in filename sort order. Lua changes hot
reload; removing a pin restores normal selection on the next dependency build,
or the remaining declaration if another file pins the same depot. Existing
downloads are not explicitly restarted.

Manifest pinning requires the `BuildDepotDependency` hook to resolve and install;
check the log for `BuildDepotDependency: hooked` and the pinned manifest message.
The pin message also appears when Steam already selected the configured GID.
The signature was checked against the locally available ARM64 Steam binary;
a live installed depot matches its Lua pin, but a live GID override remains
unverified. `addtoken` declarations are still
ignored with a warning because the macOS PICS consumer is not yet verified.

To uninstall, quit Steam and run:

```bash
bash scripts/remove.sh
```

This restores the backed-up Steam files and keeps your Lua/config.
Keep the `installer-backup` folder until you uninstall. If something fails,
check `~/Library/Application Support/macsteam/macsteam.log`.

## License

Same license as the original project. See [LICENSE](LICENSE).
