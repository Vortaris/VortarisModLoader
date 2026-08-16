# VortarisModLoader

**English** | [简体中文](README.zh-CN.md)

A data-driven mod loader for Godot 4.7, written in C++ as a GDExtension.
Open source, no runtime dependencies.

**Core idea: ids index everything — reload the resource pointer and the mod applies.**
Every piece of moddable content is addressed by a namespaced id
(`namespace:path`, e.g. `game:units.knight`), inspired by Minecraft
Fabric/Forge's Registry and Resource/Data Pack model. A mod modifies the game by
replacing the file an id points to — no game code changes needed.

Built for composition/ECS data-driven games (systems, components, entities are
all data) and traditional games alike (texture/model/scene overrides).

## Features

- **Id-indexed content**: `namespace:path`; implicit path convention — drop a file
  at `assets/<ns>/<path>.<ext>` / `data/<ns>/<path>.<ext>` and it gets id `ns:path`.
- **Unified content database** (optional): preload all data into an in-memory
  repository at startup for O(1) id lookups; batched async preload
  (`preload_database_async`); `set_data`/`delete_data` rewrite live.
- **Declarative hooks**: hook points are ids; `invoke_hook` (chain rewrite),
  `emit_hook` (broadcast), `check_hook` (predicate). No regex source rewriting.
- **Override arbitration**: later-loaded mods win; deterministic
  (priority + explicit flag + mod id). `extra.godot.id_overrides` in the manifest
  maps a file to an explicit id, beating path inference.
- **Runtime lifecycle**: early scan before autoloads, dynamic enable/disable/
  load/unload of whole mods, transactional zip install (dev convenience).
- **Pck distribution**: `.pck` packs under a mod root are mounted read-only at
  startup and their content indexed (namespaced under `mods/<mod_id>/`).
- **Error dialogs + console**: mod errors are always printed to the console;
  `vortarismodloader/general/show_error_dialogs` shows a modal dialog (non-headless).
- **Advanced debug log**: `vortarismodloader/general/debug_output` emits
  `[vortarismodloader][dbg]` lines (scan, registry, hooks, data, packs) and
  `get_debug_log()` returns the recent lines.
- **Dev hot reload**: mtime polling — edit a mod file and it applies live;
  `vortarismodloader/general/verbose` enables detailed load logging.
- **Native `vml://` loading**: `load("vml://ns:path")` works in exported builds.
- **Beginner-friendly API**: `get`/`load`/`exists`/`get_mod_path` sugar.
- **Id metadata & reservation**: `get_id_info` (full status), `get_id_data_type`,
  `set_id_type`/`list_ids_by_type` (filter by type), `reserve`/`unreserve`.
- **Persisted content registry**: a saveable `id → resource` route table
  (`res://vml/registry.json`, auto-loaded at `finish_startup`, falls back to
  `user://vml/registry.json` when res:// is read-only); a mod shipping
  the same id overrides it — `load("vml://main_menu_bg")` swaps the background.
- **Runtime reroute**: `reroute`/`clear_reroute` hot-swap content at runtime.
- **Per-mod config**: `config_schema` in the manifest, `get_config`/`set_config`
  to `user://vml/configs/<mod_id>.json`.
- **Data-driven scenes**: `build_node(id)` builds a Node tree from Dictionary data.
- **Validation**: `validate()` scans all data and reports missing id references.
- **Editor main screen**: the "VML" tab (next to 2D/3D/Script/AssetLib) is a full
  mod-management workspace — mod list with **drag-to-reorder priority**,
  details (manifest / deps / errors / config form), hooks visualization, content
  browser, Install Zip / Create Mod / Rescan / **Export ZIP** / Uninstall.
- **ID placeholders**: declare an id + default value in the editor ("New
  Placeholder"), it auto-registers at startup, and `load("vml://id")` resolves to
  the default until a mod overrides it. The right-dock "VML IDs" panel edits the
  persisted registry / placeholders (`res://vml/registry.json`, git-committable).
- **Cross-platform**: Windows / Linux / macOS, GitHub Actions builds + tag release.

## Quick start

1. Copy `addons/vortarismodloader/` into your Godot 4.7 project.
2. Enable VortarisModLoader in Project > Project Settings > Plugins.
3. Add a bootstrap autoload (first in the autoload list) that calls `VML.finish_startup()`:

```gdscript
extends Node

func _ready() -> void:
	VML.finish_startup()
```

4. Read content by id:

```gdscript
var knight: Dictionary = VML.get_data("game:units.knight")     # JSON -> Dictionary
var camp: PackedScene = VML.get_resource("game:scenes.camp")   # scene
var node: Node = VML.instantiate("game:scenes.camp")           # instantiate
var dmg: float = VML.invoke_hook("game:modify_damage", [10.0], 10.0)
```

Put base content under `res://assets/game/` and `res://data/game/` (namespace
`game`); mods under `res://mods-unpacked/<mod_id>/` (dev folders),
`res://mods/<mod_id>/` (dev folders or `.pck` packs), or any custom root added
with `VML.add_mod_root()`. Distribution ships mods as `.pck` packs.

## Mod format

```
<mod_id>/
  manifest.json      # namespace/name/version_number/dependencies/... (Thunderstore-compatible)
  mod_main.gd        # optional entry, extends Node, register hooks in _init
  icon.png           # optional
  assets/<ns>/<path>.<ext>   -> id "ns:path"
  data/<ns>/<path>.<ext>     -> id "ns:path"
```

`manifest.json`: `namespace` (= mod id, `^[a-z0-9_]{1,32}$`), `name`,
`version_number`, `dependencies`/`optional_dependencies` (`"lib_mod"` or
`"lib_mod@>=1.0"`), `load_before`, `incompatibilities`,
`extra.godot.main_script`, `extra.godot.id_overrides` (explicit id mapping).
See [docs/mod_format.md](docs/mod_format.md).

## What's new in 0.3.0

- **Explicit id overrides** (`extra.godot.id_overrides`): map a manifest file to
  an explicit id — wins over path inference; several files may share one id.
- **Pck distribution**: `.pck` files under a mod root are mounted read-only at
  startup; content is indexed with the normal scanner rules. Pcks are the
  recommended way to ship mods.
- **Error dialog + console**: mod errors are printed to the console at
  startup/rescan; `vortarismodloader/general/show_error_dialogs` (default false) shows a
  modal dialog (non-headless only).
- **Advanced debug output**: `vortarismodloader/general/debug_output` (default false)
  emits `[vortarismodloader][dbg]` lines; `get_debug_log()`/`clear_debug_log()`
  expose the recent lines.
- **Default mod roots** are now `["res://mods", "res://mods-unpacked"]`
  (`user://vml/mods` is no longer a default). `install_mod_from_zip` extracts into
  the first writable root (`res://mods` in dev).
- **Enable/load state fix**: `is_mod_loaded()` reflects enable/disable
  immediately (pure-data mods included), no rescan required.

### Stage B (editor GUI + ID placeholders)

- **VML Mods main screen**: mod management moved from a dock to the central
  workspace (the "VML" tab). Left mod list columns:
  Mod / Namespace / Enabled / Loaded / Priority / Deps, with **drag-to-reorder
  priority** persisted via `VML.set_mod_order()` to `user://vml/load_order.json`
  (dependency order is enforced). Right side: manifest info, deps, errors/
  warnings, config form, hooks visualization (`Hook / Mod / Priority /
  Description`), content browser, and Enable/Disable, Export ZIP, Uninstall,
  Install Zip buttons. The old left-bottom dock is removed (`mod_manager_panel.gd`
  now just hosts the same screen).
- **Resizable columns**: every Tree table (mod manager + ID editor) has
  user-draggable column headers, sensible minimum widths and no content clipping.
- **Create Mod dialog**: fixed compact size (`set_min_size` + `reset_size`), no
  more auto-filling the screen.
- **Export ZIP**: select a mod and export its root folder to a `.zip` you choose;
  the result re-installs via `install_mod_from_zip`.
- **ID placeholders**: `VML.set_placeholder(id, type, default, description)` +
  `VML.get_placeholder_ids(type="")`. Resource types resolve through
  `load("vml://id")` / `get_resource`; "data" stores a constant read via
  `get_data`. Stored in the project registry (`res://vml/registry.json`), loaded
  at `finish_startup`, overridden by any mod. The VML IDs panel has a
  "New Placeholder" button + Placeholders tab. See
  [docs/registry.md](docs/registry.md).

## Migrating from 0.2.1

- **`register` vs `register_id`**: `VML.register(id, path)` is unchanged. The new
  `VML.register_id(id, value, priority)` registers a direct Variant provider (no
  file). Don't confuse the two — the path variant stays under the `register` name.
- **`has` new semantics**: `has()` now also returns true when the id has a live
  in-memory database entry (previously routing/reservation only). Use
  `has_data(id)` to query the database specifically.
- **`finish_startup` vs `finish_startup_auto`**: `finish_startup()` is unchanged
  (call it from a bootstrap autoload). `finish_startup_auto()` defers and retries
  until the scene tree is ready, so autoload `_ready` runs first — set
  `vortarismodloader/general/auto_finish_startup` to trigger it automatically.
- **`get_mod_errors` vs `get_mod_report`/`get_errors_summary`**: `get_mod_errors`
  stays errors-only. For warnings too, use `get_mod_report(id)`; for every
  problematic mod at once, `get_errors_summary()`; `get_startup_report()` gives
  the aggregate.
- **`get_all` new semantics**: now returns the union of registry ids and loaded
  database ids (values lazily resolved). The shape `{ canonical_id: value }` is
  unchanged.
- **`set_data` persistence**: `set_data(id, value, true)` persists the value as a
  project-level `__registry__` entry (priority 0, so mods override it). The
  registry now defaults to `res://vml/registry.json`
  (`vortarismodloader/paths/registry_path`), falling back to `user://vml/registry.json`
  in read-only exports.
- **Custom mod roots**: mods are scanned from two per-directory project settings,
  `vortarismodloader/paths/mod_dir` (default `res://mods`) and
  `vortarismodloader/paths/unpacked_dir` (default `res://mods-unpacked`). Add/remove
  extra roots at runtime with `add_mod_root`/`remove_mod_root`; `rescan()` respects
  them. Legacy `vortarismodloader/paths/mod_paths` / `vortarismodloader/mod_paths`
  array entries (0.3.0/0.3.1) are still merged for compatibility.
- **Export policy**: `vortarismodloader/export/export_mods`
  (`embedded`/`external`/`none`) + `vortarismodloader/paths/scan_user_mods` control what
  gets scanned; query with `get_mod_package_plan()` and set with
  `set_export_policy()`.

## Docs

- [mod_format.md](docs/mod_format.md) — mod package format & manifest reference
- [quickstart.md](docs/quickstart.md)
- [registry.md](docs/registry.md) — ID content registry (persistence + editor panel + reroute + mod config)
- [hooks.md](docs/hooks.md) — declarative hooks guide
- [database.md](docs/database.md) — unified content database
- [dev_hot_reload.md](docs/dev_hot_reload.md)
- [release_mods.md](docs/release_mods.md) — shipping mods in a release build
- [AI_DEBUGGING.md](docs/AI_DEBUGGING.md) — AI / headless-CLI debugging: MCP `run_script`
  API snippets, CLI args & exit codes

## Building

```bash
pip install scons
scons platform=windows target=template_debug arch=x86_64          # inside godot-cpp
scons -j 8 platform=windows target=template_debug arch=x86_64 build_library=False \
      godot_cpp_path=<path-to-godot-cpp>
godot --headless --path demo --quit
godot --headless --path demo --script res://scripts/regression_test.gd
```

See [docs/cross_platform.md](docs/cross_platform.md) and
[.github/workflows/build.yml](.github/workflows/build.yml).

Chinese docs: [README.zh-CN.md](README.zh-CN.md)

## License

MIT.
