# Changelog

## [0.2.0]

- **Hot reload:** Apply Lua and app/depot config changes without restarting Steam.
- **Live Library updates:** Added games appear immediately; removing their Lua
  removes them unless another config or Steam license still supplies them.
- **Config validation:** Invalid YAML or Lua keeps the last working config.
- **Release versioning:** Use macSteam versions, with the target Steam version
  and changelog in each release description.

## [0.1.0]

- **Standalone distribution:** Reset macSteam versioning to `0.1.0` and replace
  the config app and launcher with shell-based installation and removal.
- **Lua import:** Read `addappid` app/depot entries from the Lua folder.
- **Release bundle:** Package the dylib, installer, examples, and signatures in
  `macsteam-standalone.tar.gz`.
