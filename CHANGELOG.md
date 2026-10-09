# Changelog

## Unreleased

- **Steam IPC startup:** Install child-process environment filtering before
  Steam loads its client, so system helpers such as `launchctl` do not inherit
  the injected dylib. Filter `execv`'s inherited environment too.
- **Manifest pinning:** Apply `setManifestid(depotId, "gid" [, size])` to matching
  primary depots of configured apps after Steam builds their dependencies.
  Preserve Steam's size and shared-depot vector; hot reload additions, changes,
  and removals. The native hook has static ARM64 verification and mocked tests;
  a live installed depot matches its Lua pin. Pin logging now also records
  manifests that already match; a live GID override is still unverified.
- **Lua compatibility:** Accept case-insensitive declaration names. `addtoken`
  remains unsupported and is ignored with a warning.

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
