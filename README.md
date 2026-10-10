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
Releases use macSteam versions, such as `v0.3.0`. Check the release description
for the compatible Steam version. See [CHANGELOG.md](CHANGELOG.md) for changes.
Or build it yourself with `make dist`.

The ZIP contains `macsteam.dylib`, its `signatures/` database, and the
install/remove scripts. The installer creates the default config when needed.
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

Lua files accept `addtoken(appid, "decimal_uint64")` for PICS app metadata:

```lua
addappid(12345)
addtoken(12345, "1234567890123456789")
```

Tokens must be quoted decimal values in `0..18446744073709551615`;
app IDs use `1..2147483647`. A nonzero token replaces the outgoing token
only for configured apps, including apps you already own. `addtoken` alone
does not add an app; zero leaves Steam's token unchanged. The last declaration
wins in filename order. Hot reload applies changes to subsequent requests;
removing an overriding file restores the earlier declaration. Token values
are never logged. Responses for these apps log `missing_token`, `only_public`,
and `metadata_bytes`. An empty metadata buffer can be a metadata-only reply;
a response without a token error alone does not prove the token was necessary.

The PICS hook layout/signature is verified for Steam macOS ARM64 build
`1788652215`. This does not grant ownership or guarantee server acceptance.

To uninstall, quit Steam and run:

```bash
bash scripts/remove.sh
```

This restores the backed-up Steam files and keeps your Lua/config.
Keep the `installer-backup` folder until you uninstall. If something fails,
check `~/Library/Application Support/macsteam/macsteam.log`.

## License

Same license as the original project. See [LICENSE](LICENSE).
