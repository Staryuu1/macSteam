# macsteam

This is an unofficial fork of [Selectively11/macSteam](https://github.com/Selectively11/macSteam).
All credit for the original project goes to its author and contributors.

This project is intended only for personal experiment.

This fork removes the config app and launcher. You install the dylib with shell
scripts and put Lua files in a folder. The original README is in [oldreadme.md](oldreadme.md).

## Install and use

Apple Silicon only, macOS 15+. Install Steam and launch it once first.
If you used the old config app, start with a clean Steam installation.

Download `macsteam-standalone.zip` from [Releases](https://github.com/Staryuu1/macSteam/releases).
Releases use macSteam versions, such as `v0.3.1`. Check the release description
for the compatible Steam version. See [CHANGELOG.md](CHANGELOG.md) for changes.
Or build it yourself with `make dist`.

Extract the ZIP, quit Steam, and run this from the extracted folder without sudo:

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

## Experimental online fix

Experimental; multiplayer has not been tested yet. Uses Spacewar (AppID 480).
Based on the onlinefix implementation in [BetterSteamTools](https://github.com/madoiscool/BetterSteamTools).

1. Install the latest macsteam build and restart Steam.
2. Open the game's **Properties → General → Launch Options**.
3. Add `-onlinefix`, then launch the game.

If the game has startup issues, try `-onlinefix -realappid`.
Remove these options and relaunch to disable onlinefix.

Both players need compatible game versions and matchmaking through AppID 480.
Normal game lobbies are separate. Run only one game at a time; compatibility
with individual games and CrossOver/Wine is unverified.

## Uninstall

To uninstall, quit Steam and run:

```bash
bash scripts/remove.sh
```

This restores the backed-up Steam files and keeps your Lua/config.
Keep the `installer-backup` folder until you uninstall. If something fails,
check `~/Library/Application Support/macsteam/macsteam.log`.

## License

Same license as the original project. See [LICENSE](LICENSE).
