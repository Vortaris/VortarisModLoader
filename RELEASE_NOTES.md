# Release Notes

## 0.3.0

Stage A of the GDExtension rewrite: explicit id overrides, error dialogs +
console, advanced debug output, pck distribution, and the enable/load state fix.

### A1 — Explicit id overrides (#1)

- `manifest.json` gains `extra.godot.id_overrides`:
  `{ "data/mymod/units/archer.json": "game:units.elite_archer" }`.
- The explicit override **wins over path inference** at scan time (the file is
  indexed under the override id only). Several files may map to the **same id**;
  they register as normal providers and are resolved by the standard override
  arbitration (priority → explicit → mod id). Invalid override ids are manifest
  errors surfaced at discovery.
- `docs/mod_format.md` documents the field; a `demo/mods-unpacked/override_mod/`
  fixture exercises it.

### A2 — Error dialogs + console (#3)

- New project setting `vortarismodloader/show_error_dialogs` (bool, default
  false), registered in `src/register_types.cpp`.
- After `finish_startup()` / `rescan()`, mod errors are **always printed to the
  console**; when the setting is on **and** the display is not headless, a modal
  `AcceptDialog` lists `<mod>: <reason>` lines.
- New API: `get_error_summary()` returns the exact text that is printed/shown.

### A3 — Advanced debug output (#4)

- New project setting `vortarismodloader/debug_output` (bool, default false).
- When on, key paths emit `[vortarismodloader][dbg]` lines: discovery/scan per
  file, registry add/remove/priority, database set/erase/preload, hook
  register/invoke/emit/check, loader data/resource loads, pck mounts.
- New API: `get_debug_log()` (recent lines) / `clear_debug_log()`. Gated
  independently of the existing `vortarismodloader/verbose`.

### A4 — Mod path defaults + pck support (#5)

- Default `vortarismodloader/mod_paths` is now `["res://mods",
  "res://mods-unpacked"]` — `user://vml/mods` is no longer a default root.
- `.pck` files under a configured root are **mounted read-only** at startup
  (`ProjectSettings.load_resource_pack`). Content must be namespaced under
  `mods/<mod_id>/` so it lands at `res://mods/<mod_id>/` and is discovered like
  an unpacked mod folder; a pack without a manifest derives its id from the
  folder. Mounted content is indexed by the normal scanner rules (path inference
  **and** `id_overrides`).
- `install_mod_from_zip` now extracts into the first writable configured root
  (`res://mods` in dev) and is documented as an optional dev convenience —
  distribution uses packs.
- `docs/mod_format.md` / `docs/release_mods.md` updated with the pck convention.

### A5 — Enable/load state fix (#6)

- `is_mod_loaded()` now means "enabled **and** content active" — it reflects
  `enable_mod`/`disable_mod` immediately, including for pure-data mods with no
  mod_main (previously it stayed false until a rescan re-scanned content).

### Docs, version & tests

- `plugin.cfg` → `0.3.0`; `doc_classes/VMLModLoader.xml` and `README.md` synced
  (new methods/settings, new defaults, pck/id_overrides).
- Regression extended to **T67 (220 assertions)** covering id_overrides (T63),
  debug output gate (T64), pck mount (T65), error summary (T66) and
  enable/load state (T67). Headless smoke stays green.

### B1 — Resizable table columns (#2)

- Every Tree table in `mod_manager_panel.gd` / `id_editor_panel.gd` now has
  user-draggable column headers, sensible `set_column_custom_minimum_width`, and
  `set_column_clip_content(false)` so long values are never cut off.

### B2 — Hooks table columns (#7)

- The hooks table is now `Hook / Mod / Priority / Description`. The **Mod**
  column shows each handler's owning mod id from `list_hook_handlers()` (no more
  `["chantmod"]` array literals); the **Description** column shows the declared
  hook-point description from `list_hook_points()`. One row per handler.

### B3 — Fixed Create-Mod dialog size (#8)

- `mod_wizard.gd` sets a compact fixed size (`set_min_size(Vector2i(400, 200))` +
  `reset_size()`) so it never auto-fills the screen on first open.

### B4 — Export mod as ZIP (#9)

- The mod manager (main screen) gains **Export ZIP**: pick a mod, choose a
  destination with a save `FileDialog`, and its root folder is packed with
  `ZIPPacker` (import metadata skipped). The result re-installs via
  `install_mod_from_zip`.

### B5 — VML Mods becomes an editor main screen (#10)

- `VML Mods` moved from the left-bottom dock to the **central workspace** — the
  "VML" tab next to 2D/3D/Script/AssetLib (`_has_main_screen` /
  `_make_visible` / `_get_plugin_name` / `_get_plugin_icon` in
  `editor_plugin.gd`).
- Left: mod list (`Mod / Namespace / Enabled / Loaded / Priority / Deps`) with
  **drag-to-reorder priority** (Tree `_get_drag_data`/`_can_drop_data`/
  `_drop_data` → `VML.set_mod_order()`). Dependency order is enforced and the
  order persists to `user://vml/load_order.json` across rescans/restarts.
- Right: manifest info, dependencies, errors/warnings, config form, hooks
  visualization, content browser, and Enable/Disable, Export ZIP, Uninstall,
  Install Zip.
- The old dock is removed; `mod_manager_panel.gd` now just hosts the same screen
  (kept so the T29 instantiation test passes).
- New API: `get_mod_priority`, `get_mod_order`, `set_mod_order`,
  `get_mod_display_name`, `get_mod_description`.

### B6 — ID placeholder system (#11)

- New API: `VML.set_placeholder(id, type, default, description)` declares a
  placeholder id with a default value — resource types resolve through
  `load("vml://id")` / `get_resource`, "data" stores a constant read via
  `get_data`. `VML.get_placeholder_ids(type="")` lists/filters them.
- Placeholders are registry entries marked `placeholder: true` (base-layer,
  priority 0, mods override them), saved to the project-level
  `res://vml/registry.json` and auto-loaded at `finish_startup()`.
- The `VML IDs` panel gains a **New Placeholder** button + **Placeholders** tab;
  `Save`/`Reload` now target the project-level registry path.
- `docs/registry.md` documents the workflow; README synced.

### Tests, version & docs (Stage B)

- Regression extended to **T73 (242 assertions)**: mod-order validation/
  persistence (T68) and placeholders incl. `load("vml://…")` resolution and
  save/load round-trip (T71–T73). Headless smoke and `--editor --import --quit`
  stay green.

### 0.3.0 review fixes (F1–F8)

Code-review hardening of the 0.3.0 baseline.

- **F1 — registry re-registration is immediate.** `RegistryIndex` now treats
  `__registry__` / `__reroute__` / `__explicit__` providers as singletons: adding a
  new one first removes the mod's prior provider for that id. Editing a registry
  entry, a second `reroute`, a `set_placeholder` type change, and
  `remove_registry_entry` all take effect immediately with no stale provider left
  behind (`has()` flips correctly, `get_id_info` resolves the new route).
- **F2 — manifestless `.pck` mods honour `export_mods`.** `mount_packs()` and the
  manifestless-pck discovery step now apply the same policy as the discovery loop:
  `none` loads nothing (packs are not even mounted) and `external` skips the
  embedded `res://` root, so a pack's content is not registered when the policy
  forbids it.
- **F3 — `set_mod_order` validates the full order.** A partial list that hoists a
  mod above its unlisted dependency is rejected; the persisted
  `user://vml/load_order.json` is validated on load too — an invalid file falls
  back to the default topological order with a warning instead of corrupting the
  load order.
- **F4 — project settings are no longer reset at startup.**
  `vortarismodloader/show_error_dialogs` and `debug_output` only get their default
  written when the setting is absent, so a `true` in `project.godot` survives
  every launch.
- **F5 — legacy 0.2.x zip mods get a one-time migration notice.** 0.3.0 removed
  `user://vml/mods` from the default `mod_paths` (distribution now uses `.pck`
  packs). On startup, if `user://vml/mods` still contains zip-installed mods and is
  not a configured root, a one-time notice is printed explaining the change:
  **move the mods into a configured root (e.g. `res://mods`) or call
  `VML.add_mod_root("user://vml/mods")`**. `get_legacy_mod_migration_notice()`
  returns the hint; a marker under `user://vml/` keeps it one-time across restarts.
- **F6 — `install_root()` uses a writable probe.** A plain `res://`/`user://`
  prefix check could return a read-only `res://` root in an exported build (or
  while a pack is mounted). The first configured root that passes a create+remove
  probe is chosen, custom absolute-path roots are eligible, and exports fall back
  to `user://vml/mods`.
- **F7 — deterministic same-mod provider order.** Equal providers (a mod mapping
  several files onto one id via `id_overrides`) now resolve with an insertion-order
  tiebreak, so the winner is a strict, stable total order instead of an unspecified
  `std::sort` permutation.
- **F8 — error dialogs are debounced and editor-safe.** `maybe_show_error_dialogs`
  skips the editor (`is_editor_hint()`) and pops at most one `AcceptDialog` per
  distinct error summary, so repeated rescans no longer stack modals.

### Tests (review fixes)

- Regression extended to **290 assertions**: per-fix coverage for every scenario
  above (F1 re-register/reroute/placeholder/remove, F2 policy × 3, F3 partial
  list + persisted bad file, F4 setting guard, F5 notice + re-add root, F6
  writable-root selection, F7 deterministic winner, F8 no dialog stacking).
  Headless smoke stays green.

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
