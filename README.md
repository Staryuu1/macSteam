# macsteam

## Unofficial fork

This is an unofficial fork of [Selectively11/macSteam](https://github.com/Selectively11/macSteam).
All credit for the original project goes to its author and contributors.

This fork removes the config app and launcher. You install the dylib with shell
scripts and put Lua files in a folder. The original README is in [oldreadme.md](oldreadme.md).

## Install and use

Apple Silicon only, macOS 15+. Install Steam and launch it once first.
If you used the old config app, start with a clean Steam installation.

Get the bundle from **Actions → Build standalone dylib → Run workflow**, then
download the artifact after it finishes. Or build it yourself with `make dist`.

Extract the bundle, quit Steam, and run this from the extracted folder without sudo:

```bash
bash scripts/install.sh
```

Put your `.lua` files here:

```text
~/Library/Application Support/macsteam/lua/
```

Open Steam normally. Restart Steam whenever you add, edit, or remove Lua files.
You can also edit `~/Library/Application Support/macsteam/config.yaml`.

Lua supports `addappid` app/depot entries. `setManifestid` and `addtoken` are
ignored with a warning; full Lua scripts and hot reload are not supported.

To uninstall, quit Steam and run:

```bash
bash scripts/remove.sh
```

This restores the backed-up Steam files and keeps your Lua/config.
Keep the `installer-backup` folder until you uninstall. If something fails,
check `~/Library/Application Support/macsteam/macsteam.log`.

## License

Same license as the original project. See [LICENSE](LICENSE).
