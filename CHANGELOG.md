# Changelog

## [0.3.1]

- Implement experimental "onlinefix" (not tested yet).

## [0.3.0]

- Fix Steam IPC startup and child-process environment filtering.
- Add `setManifestid`.
- Add `addtoken` with hot reload.
- Accept case-insensitive Lua declarations.
- Add PICS response diagnostics without logging tokens.
- Package releases as ZIP with the dylib, signatures, and install/remove scripts.

## [0.2.0]

- Add hot reload for Lua and app/depot config.
- Refresh the Library when configured games are added or removed.
- Keep the last valid config when YAML or Lua is invalid.

## [0.1.0]

- Replace the config app and launcher with install/remove scripts.
- Import Lua app/depot entries.
