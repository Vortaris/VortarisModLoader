# Release Notes

## 0.2.1

UX + reliability pass.

### Editor
- **VML Mods** panel lives in the left-bottom dock next to Import; **VML IDs**
  (Registry + Loaded tabs, New/Edit popup with type dropdown) in the right dock
  next to the Inspector. Resizable columns; mod config editor; console + status
  feedback on every action.
- Dependency confirmation dialogs: disabling a mod whose dependents would cascade
  off asks first; enabling one whose deps cascade on asks too; missing-dependency
  warnings refuse to start.

### Reliability (from a focused review)
- Cascade enable/disable with cycle guards; enable idempotence fixed so fresh zip
  installs actually scan their content.
- Overlay priority is derived from the dependency load order — a re-enabled mod
  always keeps its override rank.
- `rescan` rebuilds mod_main (no more "enabled but not loaded"); the hot reloader
  watches newly added files; registry edits refresh the DB cache.
- Reinstalling/upgrading a zip mod no longer deletes its own files; pure-data mods
  without an entry point no longer show spurious errors.

### Tests
115 assertions green, repeatable.

## 0.2.0

Data/content authoring layer. One addon zip now contains **all platforms**.

### ID content registry (persisted)

- Saveable `id → resource` route table (`user://vml/registry.json`; `res://registry.json`
  for built-in defaults). Auto-loaded at `finish_startup`.
- API: `set_registry_entry` / `get_registry_entry` / `get_registry` /
  `remove_registry_entry` / `save_registry` / `load_registry`.
- A mod shipping the same id overrides the registry route (override arbitration).

### Runtime reroute

- `reroute(id, path)` / `clear_reroute(id)` — highest-priority, non-persisted
  hot-swap of an id's target (theme/day-night switches, A/B testing).

### Per-mod config

- `config_schema` in `manifest.json` (`extra.godot.config_schema`);
  `get_config` / `set_config` / `get_config_schema` with `user://vml/configs/<id>.json`.

### Data-driven scenes & validation

- `build_node(id)` builds a Node tree from a Dictionary (`type/name/properties/children`).
- `validate()` scans all loaded data and reports missing id references.

### Editor

- **"VML IDs"** panel in the **right dock, next to the Inspector**: browse/create/
  edit/delete registry entries, save to disk. (Mod management stays under
  Tools > "VML Mods".)

### Packaging

- Releases ship **one** zip containing Windows + Linux + macOS binaries.

## 0.1.2

Code-review-hardened maintenance release. One addon zip, all platforms.

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
