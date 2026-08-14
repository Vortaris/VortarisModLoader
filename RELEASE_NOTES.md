# Release Notes

## 0.1.0

First public release of VortarisModLoader.

### Core

- Namespaced id registry (`namespace:path`) with implicit path-to-id convention:
  drop a file into `assets/<ns>/` or `data/<ns>/` and it is addressable by id.
- Override arbitration: later-loaded mods win; explicit `register()` beats
  implicit at equal priority; deterministic tie-breaks.
- Thunderstore-compatible `manifest.json` (namespace/name/version_number/
  dependencies with semver constraints/load_before/incompatibilities).
- Dependency-sorted load order with cycle detection and clean error surfacing.

### Unified content database

- `database_mode` (`data` default / `all` / `off`): preload data ids into an
  in-memory repository for O(1) lookups.
- Mutable repository: `set_data`/`delete_data` rewrite live entries and emit
  `database_entry_changed`.

### Declarative hooks

- Hook points are namespaced ids; three semantics: `invoke_hook` (pipeline
  rewrite), `emit_hook` (broadcast), `check_hook` (predicate).
- `add_hook` from a `mod_main._init` is attributed to that mod automatically.

### Mod lifecycle

- Discovery from `res://mods-unpacked/` (dev) and `user://vml/mods/` (installed).
- Runtime `enable/disable/load/unload` of whole mods, with dependency protection.
- Transactional zip install (`install_mod_from_zip`) and uninstall.
- `user://vml/profile.json` persists enable state.

### Assets & hot reload

- Raw asset loading for `user://` paths (PNG/WAV/MP3/TTF/OTF) with no import cache.
- Native `vml://` ResourceFormatLoader (works in exports).
- Dev hot reload: mtime polling + `reload_resources()` + `database_entry_changed`.

### Editor

- EditorPlugin dock: mod list/state, enable-disable toggle, zip install, rescan.
- One-click mod skeleton wizard (`res://mods-unpacked/<id>/`).

### Demo & tests

- "Vortaria" data-driven demo (units/items/recipes/camp scene, hooks).
- Headless regression suite T0–T30 (66 assertions) covering every layer.
