# Release Notes

## 0.2.3

Headless CLI debugging entry + AI debugging guide (aligned with VortarisCSV/ECS 0.2.1).

### Headless CLI

- **`demo/scripts/cli_entry.gd`** (thin bootstrap, `extends SceneTree`, never mentions
  the engine singleton at parse time) + **`demo/scripts/cli_impl.gd`** (actual logic,
  loaded only after a `ClassDB.class_exists` guard passes). Same two-layer design as
  VortarisCSV: on a fresh clone with no `.godot/extension_list.cfg`, the CLI prints a
  clear `[vortarismodloader] ERROR: GDExtension not loaded` message + the one-time
  `godot --headless --editor --import --quit --path demo` hint and exits 1 — no cryptic
  "Identifier not declared" parse error.
- Commands (all after `--`, output prefixed `[vortarismodloader]`):
  - `--vortaris-vml-report` — `get_startup_report()` (broken_mods/errors/warnings) + mod ids + load_order.
  - `--vortaris-vml-validate <mod_id>` — `validate_mod(id)`; exit 0 valid / 1 invalid or unknown.
  - `--vortaris-vml-list` — mods with namespace/enabled/loaded/deps + load_order + namespaces.
  - `--vortaris-vml-get <id>` — `get_data(id)` value + `get_id_info(id)`; exit 1 if not found.
  - `--vortaris-vml-install <zip>` — `install_mod_from_zip(zip)`; exit 0 on OK.
  - Unknown / missing args print usage and exit 1.
- Each command first runs `VML.finish_startup()` (idempotent) so mods are scanned,
  startup validation has populated `get_startup_report`, and `mod_main`s are
  instantiated — the same bootstrap `demo/scripts/main.gd` performs.

### Docs

- **`docs/AI_DEBUGGING.md`** — MCP `run_script` API snippets (`get_startup_report`,
  `get_mod_ids`, `validate_mod`, `get_data`, `get_all`, `add_hook`/`invoke_hook`
  debugging, `list_hook_handlers`), the CLI argument table with exit-code conventions,
  editor-panel note (AI uses API/CLI, not the human docks).
- `README.md` / `README.zh-CN.md` link to the new guide.

### Tests

- Regression extended with CLI-facing assertions (entry script parse/load, no engine
  singleton identifier in `cli_entry.gd`, `get_startup_report` structure, `validate_mod`
  shapes, and the `cmd_*` command functions via direct API). All prior T0–T57 stay green.

## 0.2.2

Three-tier feedback pass + new-feature core.

### 问题档 (problem-tier API fixes)

- **`has()` unified semantics**: now reports true for routing providers, reserved
  ids, **and** live in-memory database entries; new `has_data(id)` queries the DB.
- **`register_id(id, value, priority)`**: register a direct Variant provider (no
  file); co-exists with the path-based `register(id, path)`. `unregister` removes
  both forms; value providers report `data_type = "value"`.
- **`invoke_hook_ctx(id, ctx, args)`**: context pipeline — handlers receive
  `(ctx, ...args)` and return a possibly-modified Dictionary; the final ctx is
  returned. `check_hook` semantics documented (bool = allow; any `false` vetoes).
- **`finish_startup_auto()` + `is_startup_done()`**: deferred, retries until the
  scene tree is ready so autoload `_ready` runs first; opt-in via
  `vortarismodloader/auto_finish_startup`.
- **`list_ids_in_namespace(ns)` / `count_ids(prefix)`**; `get_all` now returns the
  union of registry + database ids (values lazily resolved). The mod wizard emits
  a `data/<id>/sample.json` whose `"id"` matches the path-inferred id.

### 改进档 (improvement tier)

- **Runtime dependency re-check** on enable: missing dep / non-cascadeable /
  version mismatch / enabled incompatibility now reject the enable with a reason.
- **Startup data validation** (`validate_mod`): manifest completeness, loadable
  `mod_main`, parseable data JSON, JSON `"id"` vs path-inferred id cross-check
  (mismatch → warning). `vortarismodloader/validate_on_startup` (default true)
  marks problems at boot but never refuses to start.
- **Error/warning aggregation**: `get_mod_report(id)`, `get_errors_summary()`,
  `get_startup_report()`; `get_mod_errors` stays errors-only.

### 新功能核心 (new-feature core)

- **`reload_mod(id)`**: hot-reload one mod in place — drops hooks/content/DB,
  re-scans, re-instantiates `mod_main`, emits `mod_reloaded` (no duplicate hooks).
- **`set_data(id, value, persist=true)`**: persists the value as a project-level
  `__registry__` entry (priority 0, mods override it). Registry now defaults to
  `res://vml/registry.json` (`vortarismodloader/registry_path`, git-committable),
  falling back to `user://vml/registry.json` in read-only exports; old registry
  files load unchanged.
- **Export/scan controls**: `vortarismodloader/export_mods`
  (`embedded`/`external`/`none`) + `vortarismodloader/scan_user_mods`;
  `get_mod_package_plan()` / `set_export_policy()`.
- **Custom mod roots**: `vortarismodloader/mod_paths` + `get_mod_roots` /
  `add_mod_root` / `remove_mod_root`; `rescan()` respects them.

### Editor & in-game UI

- **Hooks tab** expands each hook to its handler rows [Hook, Handler, Priority, Mod].
- **ID editor "Browse" tab**: per-id provider list (best highlighted), namespace/
  type filters.
- **Config dialog** generates a form from `config_schema` (SpinBox / CheckBox /
  OptionButton / LineEdit), falling back to JSON.
- **In-game mod menu** (`demo/scenes/mods_menu.tscn`): toggle, Install Zip,
  Uninstall (user:// only), Rescan.
- New doc: `docs/release_mods.md` (release installation paths, scan switches).
- Full `doc_classes` update for every new method/signal.

### Tests

T0–T57 green (176 assertions), repeatable.

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
