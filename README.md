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
  (priority + explicit flag + mod id).
- **Runtime lifecycle**: early scan before autoloads, dynamic enable/disable/
  load/unload of whole mods, transactional zip install.
- **Dev hot reload**: mtime polling — edit a mod file and it applies live;
  `vortarismodloader/verbose` enables detailed load logging.
- **Native `vml://` loading**: `load("vml://ns:path")` works in exported builds.
- **Beginner-friendly API**: `get`/`load`/`exists`/`get_mod_path` sugar.
- **Id metadata & reservation**: `get_id_info` (full status), `get_id_data_type`,
  `set_id_type`/`list_ids_by_type` (filter by type), `reserve`/`unreserve`.
- **Persisted content registry**: a saveable `id → resource` route table
  (`user://vml/registry.json`, auto-loaded at `finish_startup`); a mod shipping
  the same id overrides it — `load("vml://main_menu_bg")` swaps the background.
- **Runtime reroute**: `reroute`/`clear_reroute` hot-swap content at runtime.
- **Per-mod config**: `config_schema` in the manifest, `get_config`/`set_config`
  to `user://vml/configs/<mod_id>.json`.
- **Data-driven scenes**: `build_node(id)` builds a Node tree from Dictionary data.
- **Validation**: `validate()` scans all data and reports missing id references.
- **EditorPlugin**: right-dock "VML IDs" panel (next to the Inspector) to edit the
  registry visually + Tools > "VML Mods" for mod management (Mods/IDs/Hooks tabs,
  resizable columns) + one-click mod skeleton wizard; every action logs to console.
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
`game`); mods under `res://mods-unpacked/<mod_id>/` (dev) or `user://vml/mods/`
(runtime zip installs).

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
`extra.godot.main_script`. See [docs/mod_format.md](docs/mod_format.md).

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
  `vortarismodloader/auto_finish_startup` to trigger it automatically.
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
  (`vortarismodloader/registry_path`), falling back to `user://vml/registry.json`
  in read-only exports.
- **Custom mod roots**: mods are scanned from `vortarismodloader/mod_paths`
  (default `["res://mods-unpacked", "user://vml/mods"]`). Add/remove roots at
  runtime with `add_mod_root`/`remove_mod_root`; `rescan()` respects them.
- **Export policy**: `vortarismodloader/export_mods`
  (`embedded`/`external`/`none`) + `vortarismodloader/scan_user_mods` control what
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
