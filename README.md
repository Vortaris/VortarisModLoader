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

## What's new in 0.4.0

- **Tags** — `<content>/<ns>/tags/**.json` groups content ids so any mod can
  extend a set; query with `tag_has` / `tag_resolve` / `tags_of` / `list_tags`.
- **Conditional data** — `@condition,mod_loaded:x` (and `tags_populated:`,
  `registry_contains:`, `any/all_mods_loaded:`, `not:`) gates a data file's load.
- **Lifecycle phases** — optional `vml_preload` / `vml_register` / `vml_setup` /
  `vml_ready` on mod_main, plus `defer_register(ns, callable)`.
- **Single mods directory** — folders = source mods, `*.pck` = packed mods, side
  by side under `paths/mod_dir` (the parallel `mods-unpacked` is deprecated).
- **Editor packs force-mounted**; exports auto-scan `<exe dir>/mods`.
- `list_ids(prefix, include_database=true)` also lists `set_data`-only ids.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the full change log.

## Features

- **Id-indexed content**: `namespace:path`; implicit path convention — drop a file
  at `assets/<ns>/<path>.<ext>` / `data/<ns>/<path>.<ext>` and it gets id `ns:path`.
- **Unified content database** (optional): preload all data into an in-memory
  repository at startup for O(1) id lookups; batched async preload
  (`preload_database_async`); `set_data`/`delete_data` rewrite live;
  `get_ids_of_type("cards")` / `get_all_of_type("cards")` enumerate every
  `ns:cards.*` id across all namespaces without manual prefix plumbing;
  `patch_data(id, {"hp": 999})` does a shallow field-level merge so a mod
  overrides just the fields it changes.
- **Declarative hooks**: hook points are ids; `invoke_hook` (chain rewrite),
  `emit_hook` (broadcast), `check_hook` (predicate). No regex source rewriting.
  `get_hook_contract_health()` / `list_unmatched_hooks()` surface contract drift —
  handlers with no declared hook point, or declared points with no handler.
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
  details (manifest / deps / errors / config form), hooks visualization,
  content browser, Install Zip / Install PCK / Create Mod / Rescan / Reload DB /
  **Export PCK** / Uninstall.
- **ID placeholders**: declare an id + default value in the editor ("New
  Placeholder"), it auto-registers at startup, and `load("vml://id")` resolves to
  the default until a mod overrides it. The right-dock "VML IDs" panel edits the
  persisted registry / placeholders (`res://vml/registry.json`, git-committable).
- **Cross-platform**: Windows / Linux / macOS, GitHub Actions builds + tag release.

## Table of contents

1. [Quick start](#quick-start)
2. [Mod package format](#mod-package-format)
3. [Write a mod: a full walkthrough](#write-a-mod-a-full-walkthrough)
4. [API overview](#api-overview)
5. [Editor: the VML main screen & VML IDs panel](#editor-the-vml-main-screen--vml-ids-panel)
6. [Project settings reference](#project-settings-reference)
7. [Distribution: folders, pck packs, zips](#distribution-folders-pck-packs-zips)
8. [What's new in 0.3.3](#whats-new-in-033)
9. [Migrating from 0.2.1](#migrating-from-021)
10. [FAQ](#faq)
11. [Docs](#docs)
12. [Building](#building)
13. [License](#license)

---

## Quick start

### 1. Install the plugin

1. Copy `addons/vortarismodloader/` into your Godot 4.7 project (or unzip the
   release addon zip into your project root).
2. Enable the plugin: **Project > Project Settings > Plugins** → toggle on
   **VortarisModLoader**.
3. Create a bootstrap autoload and place it **first** in the autoload list
   (**Project > Project Settings > Autoload**). Its only job is to finish the
   loader's startup once the scene tree is ready:

```gdscript
# Bootstrap.gd
extends Node

func _ready() -> void:
	VML.finish_startup()
```

   `finish_startup()` is idempotent: it loads the persisted registry, runs
   startup validation, instantiates every enabled mod's `mod_main.gd`, and
   prints any mod errors. Alternatively set
   `vortarismodloader/general/auto_finish_startup` to `true` and skip the
   bootstrap autoload entirely (`finish_startup_auto()` defers + retries until
   the scene tree is ready).

### 2. Put your content under ids

Place base content in `res://assets/` (heavy assets: scenes, textures, audio)
and `res://data/` (data: JSON, CSV) under a namespace directory. The loader
indexes every file implicitly:

```
res://data/game/units/knight.json   ->  id "game:units.knight"
res://assets/game/scenes/camp.tscn  ->  id "game:scenes.camp"
```

### 3. Read content by id

```gdscript
var knight: Dictionary = VML.get_data("game:units.knight")     # JSON -> Dictionary
var camp: PackedScene = VML.get_resource("game:scenes.camp")   # a Resource
var node: Node = VML.instantiate("game:scenes.camp")           # instantiate a scene
var dmg: float = VML.invoke_hook("game:modify_damage", [10.0], 10.0)  # hooks
```

Data ids (`.json`/`.csv`) are parsed into `Dictionary`/`Array` via
`get_data()`; everything else is loaded as a `Resource` via `get_resource()`
(`load("vml://game:scenes.camp")` is the native-loader equivalent, and works in
exported builds too).

### 4. Declare hook points where mods may change behavior

```gdscript
# Game side, at an instrumented point:
VML.register_hook_point("game:modify_damage", "Rewrite outgoing damage", ["current", "amount", "weapon"])
var final_damage: float = VML.invoke_hook("game:modify_damage", [base_damage, weapon], base_damage)
```

### 5. Run it

Press **F5** in the editor. The demo project (`demo/`) does exactly this — see
`demo/scripts/quickstart.gd` for a minimal, commented walkthrough that runs
headless with `godot --headless --path demo --script res://scripts/quickstart.gd`.

---

## Mod package format

A mod is a folder (development) or a zip / pck (shipped). Both share the same
layout:

```
<mod_id>/
  manifest.json      # REQUIRED — namespace/name/version_number/dependencies/...
  mod_main.gd        # optional entry point (extends Node), register hooks in _init
  icon.png           # optional, shown in the editor's mod list
  assets/<ns>/<path>.<ext>   ->  id "ns:path"      (heavy assets)
  data/<ns>/<path>.<ext>     ->  id "ns:path"      (data)
```

- `<mod_id>` **equals** the manifest `namespace` (e.g. `my_mod`), and the
  namespace must match `^[a-z0-9_]{1,32}$`.
- A mod may provide content under **any** namespace:
  - its **own** namespace for new content (`data/my_mod/units/archer.json` →
    id `my_mod:units.archer`);
  - the **target's** namespace + same relative path to override it
    (`data/game/units/knight.json` → id `game:units.knight`, winning over the
    base game and any earlier-loaded mod);
  - another mod's namespace to override that mod.

### manifest.json

Thunderstore-compatible field names; Vortaris-specific keys live under
`extra.godot`.

| field | type | notes |
|---|---|---|
| `namespace` | string | **content namespace = mod id**, required `^[a-z0-9_]{1,32}$` |
| `name` | string | display name (shown in the editor / `get_mod_display_name`) |
| `version_number` | string | `"1.2.0"` (semver; `get_mod_version`) |
| `description` / `website_url` | string | optional |
| `dependencies` | string[] | `"lib_mod"` or `"lib_mod@>=1.0"`; loaded before this mod, required to enable |
| `optional_dependencies` | string[] | enabled when present, ignored when absent |
| `load_before` / `load_after` | string[] | extra ordering constraints |
| `incompatibilities` | string[] | mutually exclusive; enforced at runtime (enable refused) |
| `extra.godot.main_script` | string | default `"mod_main.gd"` |
| `extra.godot.icon` | string | default `"icon.png"` |
| `extra.godot.asset_dirs` / `data_dirs` | string[] | default `["assets"]` / `["data"]` |
| `extra.godot.id_overrides` | object | `{ "<rel path>": "<full id>" }` — explicit id mapping (below) |
| `extra.godot.config_schema` | object | JSON-Schema-ish `{type, properties}` for the editor config form |

Example (a full, valid manifest):

```json
{
	"name": "Sample Mod",
	"namespace": "mymod",
	"version_number": "1.0.0",
	"description": "Adds archers and overrides the knight.",
	"dependencies": ["mylib@>=1.0"],
	"incompatibilities": ["other_mod"],
	"extra": {
		"godot": {
			"main_script": "mod_main.gd",
			"id_overrides": {
				"data/mymod/units/archer.json": "game:units.elite_archer"
			},
			"config_schema": {
				"type": "object",
				"properties": { "difficulty": { "type": "number" } }
			}
		}
	}
}
```

See [docs/mod_format.md](docs/mod_format.md) for the full field-by-field
reference, edge cases, and pck layout.

---

## Write a mod: a full walkthrough

This walks a complete mod from an empty folder to a distributable pack.

### Step 1 — Scaffold

Create `res://mods-unpacked/my_mod/`. In the editor you can use
**VML tab → Create Mod** (fills a valid `manifest.json`, `mod_main.gd`, and
`assets/<id>/` + `data/<id>/` sample) — or create the files by hand:

```
my_mod/
  manifest.json
  mod_main.gd
  data/my_mod/units/archer.json
  assets/my_mod/icons/archer.png
```

### Step 2 — Content (data + assets)

Every file under `assets/` or `data/` becomes an id:

```json
// data/my_mod/units/archer.json
{
	"id": "my_mod:units.archer",
	"name": "Archer",
	"health": 40,
	"attack": 8,
	"speed": 6,
	"cost": { "game:items.wood": 15 }
}
```

```json
// data/game/units/knight.json  — OVERRIDES the base knight, same namespace+path
{
	"id": "game:units.knight",
	"name": "Knight",
	"health": 150,
	"attack": 15,
	"speed": 3
}
```

The `"id"` field inside a JSON file is **optional metadata** — the loader
indexes by path, and `validate_mod` cross-checks that the JSON `"id"` (when
present) matches the path-inferred id, warning on mismatch.

### Step 3 — Logic (mod_main.gd)

`mod_main.gd` is your mod's entry point. It **must `extends Node`** and it
registers hooks/config in **`_init`**, not `_ready` — the `VML` singleton is not
in the scene tree, so `_ready` may never fire, while hooks registered in `_init`
are attributed to this mod automatically (cleaned up on disable/unload).

```gdscript
# my_mod/mod_main.gd
extends Node

func _init() -> void:
	# Declare the game hook points this mod expects (optional, editor/docs).
	VML.register_hook_point("game:modify_damage", "Rewrite outgoing damage",
			["current", "amount", "weapon"])
	# Register handlers with a priority (higher runs first).
	VML.add_hook("game:modify_damage", _on_modify_damage, 10)
	# Read your own config (set from the editor's Config dialog).
	var difficulty: float = VML.get_config("my_mod").get("difficulty", 1.0)

func _on_modify_damage(current: Variant, _amount: int, _weapon: String) -> Variant:
	return current * 2.0
```

### Step 4 — Hooks (what a game can make moddable)

Games call the dispatch API at instrumented points; mods register `Callable`s.
Three + one semantics (details in [docs/hooks.md](docs/hooks.md)):

| API | Semantics | Mod handler signature |
|---|---|---|
| `invoke_hook(id, args, default)` | pipeline — each handler rewrites the value | `func(current, ...args) -> Variant` |
| `invoke_hook_ctx(id, ctx, args)` | pipeline over a context Dictionary | `func(ctx: Dictionary, ...args) -> Dictionary` |
| `emit_hook(id, args)` | broadcast, no return | `func(...args)` |
| `check_hook(id, args)` | predicate — any `false` vetoes | `func(...args) -> bool` |

### Step 5 — Package & distribute

- **Development**: keep the folder under `res://mods-unpacked/`; edit while
  running and hot reload refreshes it.
- **Zip (dev convenience)**: right-click flow or
  `VML.install_mod_from_zip("user://downloads/my_mod.zip")` — extracts into the
  first writable mod root.
- **Pck (ship to players)**: the **VML tab → Export PCK** builds a namespaced
  `res://mods/<my_mod>/` pack from the selected mod. See
  [Distribution](#distribution-folders-pck-packs-zips).

---

## API overview

Everything below is on the `VML` engine singleton (an `Object`, registered at
engine start before any autoload). The full reference is in
[doc_classes/VMLModLoader.xml](doc_classes/VMLModLoader.xml) (in-editor F1 help
as **VMLModLoader**).

### Reading content

| API | Returns | Notes |
|---|---|---|
| `get_data(id)` / `get(id)` | `Variant` | `.json`/`.csv` → `Dictionary`/`Array`; else the `Resource`; O(1) after database preload |
| `get_resource(id)` / `load(id)` | `Resource` | scenes, scripts, textures, fonts, audio |
| `instantiate(id)` | `Node` | instantiate a `PackedScene` resolved by id |
| `has(id)` / `exists(id)` | `bool` | routing provider OR reserved OR live database entry |
| `has_data(id)` | `bool` | specifically a live in-memory database entry |
| `resolve(id)` | `String` | physical path of the winning provider (`""` if unknown) |
| `get_id_info(id)` | `Dictionary` | `{valid, resolved, path, provider_mod, priority, explicit, preloaded, reserved, type, data_type}` |
| `get_id_data_type(id)` | `String` | `data`/`scene`/`script`/`image`/`audio`/`font`/`resource`/`value` |
| `list_ids(prefix)` | `Dictionary` | `{ namespace: PackedStringArray }` (paths without the `ns:` prefix) |
| `list_namespaces()` | `PackedStringArray` | every indexed namespace |
| `list_ids_in_namespace(ns)` | `PackedStringArray` | full `ns:path` ids, sorted |
| `count_ids(prefix)` | `int` | how many ids begin with a dotted prefix |
| `list_providers(id)` | `Dictionary` | `{providers: [{mod_id, path, priority, explicit}], best: int}` |

### Registering & reserving ids

```gdscript
VML.register("mygame:cheat", "res://data/mygame/cheat.json")  # explicit path route
VML.register_id("mygame:value", 42, 0)                        # direct Variant provider (no file)
VML.unregister("mygame:cheat")

VML.reserve("mygame:future_content")   # has() reports true, no provider yet
VML.unreserve("mygame:future_content")

VML.set_id_type("game:units.knight", "unit")   # logical type tag
VML.get_id_type("game:units.knight")           # "unit"
VML.list_ids_by_type("unit")                   # every id tagged "unit"
```

### Content database

```gdscript
VML.get_database_mode()          # "data" | "all" | "off"
VML.set_database_mode("all")     # switch + reload
VML.preload_database()           # synchronous preload
VML.preload_database_async()     # batched across frames (emits preload_progress, database_loaded)
VML.reload_database()            # clear + re-preload

VML.get_all("game:units.")       # { "game:units.knight": {...}, ... } (prefix)
VML.get_all()                    # union of registry ids + loaded database ids

# 0.3.3 — convention-based type queries (dotted ns:type.* prefix, ALL namespaces):
VML.get_ids_of_type("cards")     # PackedStringArray of every ns:cards.* id
VML.get_all_of_type("cards")     # { "game:cards.king": {...}, "mod:cards.jester": {...} }

# Rewrite live entries:
VML.set_data("game:units.peasant", {"name": "Peasant MK2", "health": 60})
VML.set_data("mygame:persisted", {"x": 1}, true)   # persist as a project-level value
VML.patch_data("game:cards.knight", {"hp": 999})   # 0.3.3 shallow field-level merge
VML.delete_data("game:units.peasant")              # lookups fall back to the file
```

`patch_data` updates **only the keys present in the patch**; the merge is shallow
(a nested `Dictionary`/`Array` value in the patch replaces the whole existing
value). If the id has no `Dictionary` value — or does not exist — `patch_data`
behaves exactly like `set_data`.

### Hooks

```gdscript
VML.register_hook_point("game:modify_damage", "Rewrite outgoing damage", ["current", "amount", "weapon"])
VML.add_hook("game:modify_damage", _on_modify_damage, 10)
VML.remove_hook("game:modify_damage", _on_modify_damage)

var dmg: float = VML.invoke_hook("game:modify_damage", [10.0, "sword"], 10.0)
var ctx: Dictionary = VML.invoke_hook_ctx("game:on_hit", {"damage": 10.0}, [target])
VML.emit_hook("game:on_entity_killed", ["archer"])
var allowed: bool = VML.check_hook("game:can_open_door", [door])

# Introspection + 0.3.3 contract health:
VML.list_hooks("game:")                       # { hook_id: { count, mods } }
VML.list_hook_points("game:")                 # { hook_id: { description, arg_types } }
VML.list_hook_handlers("game:modify_damage")  # [{ mod_id, priority }, ...] in call order
VML.get_hook_contract_health()                # { declared, active, unhandled, undeclared, healthy }
VML.list_unmatched_hooks("game:")             # { undeclared: [...], unhandled: [...] }
```

### Registry, placeholders, reroute

```gdscript
VML.set_registry_entry("main_menu_bg", "res://assets/game/menus/bg.png", "image", "menu background")
VML.get_registry_entry("main_menu_bg")   # { path, type, description, value?, placeholder? }
VML.get_registry()                       # { id: {...} }
VML.remove_registry_entry("main_menu_bg")
VML.save_registry()                      # -> project-level res://vml/registry.json (git-committable)
VML.load_registry()

# ID placeholders (declare an id + default; mods override it):
VML.set_placeholder("mygame:mainmenu.bg", "image", "res://assets/game/menus/bg.png", "menu background")
VML.set_placeholder("mygame:start_health", "data", 100)
VML.get_placeholder_ids("image")         # filter by type ("" = all)
var bg: Texture2D = load("vml://mygame:mainmenu.bg")

# Runtime hot-swap (highest priority, not persisted):
VML.reroute("main_menu_bg", "res://assets/game/menus/bg_night.png")
VML.clear_reroute("main_menu_bg")
```

### Mod lifecycle & health

```gdscript
VML.get_mod_ids()            # every discovered mod id
VML.get_load_order()         # dependency-sorted load order
VML.is_mod_enabled(id)       # content stacked in the registry?
VML.is_mod_loaded(id)        # enabled AND content active (immediate, incl. pure-data mods)
VML.enable_mod(id)           # cascade-enables required deps
VML.disable_mod(id)          # refused while enabled mods depend on it (cascade-off with UI)
VML.load_mod(id) / VML.unload_mod(id)   # aliases
VML.reload_mod(id)           # hot-reload one mod in place (no duplicate hooks)
VML.get_mod_path(id) / get_mod_version(id) / get_mod_display_name(id) / get_mod_description(id)
VML.get_mod_priority(id)     # base=0, first mod=1, ... (-1 if absent)
VML.get_mod_dependencies(id) # { dep_id: { exists, enabled } }
VML.get_mod_dependents(id)   # enabled mods that would cascade-off
VML.get_mod_order() / set_mod_order([...])   # persisted user priority (load_order.json)

VML.validate_mod(id)         # { valid, errors, warnings, checked }
VML.get_mod_report(id)       # { errors, warnings }
VML.get_errors_summary()     # { mod_id: { errors, warnings } } for every problem mod
VML.get_startup_report()     # { broken_mods, errors, warnings }
VML.get_error_summary()      # human-readable "<mod>: <err>" text (console / error dialog)
```

### Zip install & mod roots

```gdscript
var err: int = VML.install_mod_from_zip("user://downloads/archer_pack.zip")  # dev only
VML.uninstall_mod("my_mod")   # remove an installed (user://) mod
VML.get_mod_roots()           # mod_dir + unpacked_dir + legacy + extra roots
VML.add_mod_root("user://my_mods")   # persists to vortarismodloader/paths/extra_roots
VML.remove_mod_root("user://my_mods")
VML.install_root()            # first writable configured root (editor Install PCK)
VML.get_mod_package_plan()    # { embedded, external, scan_user_mods }
VML.set_export_policy("embedded", true)
```

### Per-mod config, data-driven scenes, validation

```gdscript
VML.get_config_schema("my_mod")   # the manifest config_schema
VML.get_config("my_mod")          # {} if unset
VML.set_config("my_mod", {"difficulty": 2.0})

var node: Node = VML.build_node("game:ui.camp")   # {type, name, properties, children} -> Node tree
var report: Dictionary = VML.validate()           # { valid, checked, missing }
```

### Startup, hot reload, debug

```gdscript
VML.finish_startup()          # bootstrap autoload; idempotent
VML.finish_startup_auto()     # deferred + retries until scene tree is ready
VML.is_startup_done()

VML.start_hot_reload(0.5)     # dev: poll every ~0.5s
VML.reload_resources(["res://mods-unpacked/sample_mod/data/mymod/units/archer.json"])
VML.get_content_roots()       # dirs the hot-reloader watches
VML.reload_database()

VML.get_debug_log()           # recent [vortarismodloader][dbg] lines
VML.clear_debug_log()
VML.get_legacy_mod_migration_notice()
```

---

## Editor: the VML main screen & VML IDs panel

Enabling the plugin adds two editor surfaces:

### The "VML" main screen (mod management)

A tab next to **2D / 3D / Script / AssetLib**. Selecting a mod on the left shows
everything about it on the right. The screen is refresh-friendly: **it runs
`finish_startup()` automatically in the editor** so mod_main hooks are visible.

**Left — mod list** (`Mod / Namespace / Enabled / Loaded / Priority / Deps`):

- **Drag a row** to reorder load priority. The order is validated against
  dependency edges (a dependency must come before its dependents) and persisted
  to `user://vml/load_order.json`. Invalid/broken mods (not in the load order)
  refuse to reorder and explain why.
- Red = has errors, green = loaded, `-` priority = not in the load order
  (invalid or disabled).

**Toolbar:**

| Button | Action |
|---|---|
| **Rescan** | `VML.rescan()` — full re-discovery + rebuild |
| **Install PCK** | copy a `.pck` into the writable root (mounted on the next run) |
| **Install Zip** | legacy dev flow: `install_mod_from_zip` |
| **Create Mod** | one-click mod skeleton wizard (`res://mods-unpacked/<id>/`) |
| **Reload DB** | `VML.reload_database()` |

**Right — details** (manifest name / version / description / root / deps with
`exists[on]` markers / errors & warnings / **Enable/Disable** with dependency
confirmation, **Export PCK**, **Uninstall**, **Config**):

- **Enable** cascade-enables missing deps (asks first); **Disable** cascade-
  disables dependents (asks first). A missing dependency refuses to enable.
- **Config** opens a generated form from the manifest `config_schema`
  (SpinBox / CheckBox / OptionButton / LineEdit), falling back to a JSON editor.

**Tabs:**

- **Hooks** — one row per handler (`Hook / Mod / Priority / Description`), plus a
  project-wide **contract-health line** (`hook contract: N declared / M active`
  and, when drifting, `MISMATCH: X undeclared / Y unhandled`).
- **Content** — every id in the selected mod's namespace, with a filter box and
  per-id `ID / Path / Provider / Type` columns.

### The "VML IDs" panel (right dock, next to the Inspector)

Edits the persisted content registry and ID placeholders (unified: a placeholder
is a registry entry with a default value).

- **Registry tab**: every entry as `ID / Path-or-Default / Type / Desc`.
  Placeholders show `[ph]`, persisted `set_data` values show `[value]`.
- **Loaded tab**: every id currently indexed (`ID / Path / Provider / Type`).
- **Browse tab**: ids grouped by namespace, expandable to their providers (the
  winning one is highlighted green), filterable by namespace and type.
- **New / Edit**: one dialog — id, type (`data`/`scene`/`script`/`image`/
  `audio`/`font`/`resource`/`custom`), default (a resource path, or — for
  `data` — a JSON constant *or* a `res://data/...` path for a route), description.
- **Save** writes `res://vml/registry.json` (git-committable; falls back to
  `user://vml/registry.json` when res:// is read-only). **Reload** re-reads it.

Because `finish_startup()` loads the registry every launch, saved routes and
placeholders apply automatically — and mods can still override them at runtime.

---

## Project settings reference

Registered under `vortarismodloader/` in **Project > Project Settings**
(categories `general` / `paths` / `export`). All readers go through
`get_ml_setting()` — legacy flat keys (`vortarismodloader/<name>`, 0.3.0) are
still honored and migrated on startup.

| Setting | Values (default) | Meaning |
|---|---|---|
| `general/verbose` | bool (`false`) | Detailed load logging |
| `general/show_error_dialogs` | bool (`false`) | Modal dialog listing mod errors at startup/rescan (non-headless only) — errors are always printed to the console |
| `general/debug_output` | bool (`false`) | `[vortarismodloader][dbg]` advanced logging (scan, registry, hooks, data, packs); `get_debug_log()` |
| `general/auto_finish_startup` | bool (`false`) | Auto-run `finish_startup()` once the scene tree is ready (skip the bootstrap autoload) |
| `general/validate_on_startup` | bool (`true`) | Validate mods at boot; problems are marked, never refuse to boot |
| `general/database_mode` | `data` / `all` / `off` (`data`) | How much content preloads into the content database |
| `paths/mod_dir` | String dir (`res://mods`) | Dev mod main directory: `.pck` packs + zip-installed mods are scanned here |
| `paths/unpacked_dir` | String dir (`res://mods-unpacked`) | Unpacked dev mod folders, scanned too |
| `paths/registry_path` | String path (`res://vml/registry.json`) | Project-level registry file; falls back to `user://vml/registry.json` in read-only exports |
| `paths/scan_user_mods` | bool (`true`) | When `false`, non-`res://` roots are **not** scanned at boot |
| `export/export_mods` | `embedded` / `external` / `none` (`embedded`) | `embedded`: scan res:// roots too. `external`: only user/custom roots. `none`: no scanning |
| `paths/extra_roots` | PackedStringArray (runtime-only) | Extra roots added with `add_mod_root()`; hidden from the editor, merged by `get_mod_roots()` |

**Note**: `vortarismodloader/paths/mod_paths` (0.3.1) and the flat
`vortarismodloader/mod_paths` (0.3.0) arrays are gone from the editor but their
values are still merged as a backward-compatible fallback.

---

## Distribution: folders, pck packs, zips

Three ways a mod reaches a game; see [docs/release_mods.md](docs/release_mods.md)
for the full guide.

| Path | Use for | Read-only? |
|---|---|---|
| **`.pck` pack** | players / released builds | yes (mounted read-only) |
| **unpacked folder** | development (`res://mods-unpacked/`) | writable in dev |
| **`.zip` install** | optional dev convenience | extracted, then treated like a folder |

**Recommended for release: `.pck` packs.** A `.pck` dropped under any configured
mod root is mounted read-only at startup; its content must be namespaced under
`mods/<mod_id>/` so it lands at `res://mods/<mod_id>/`:

```
sample_mod.pck
  mods/sample_mod/
    manifest.json
    data/sample_mod/units/archer.json
```

A pack without a manifest still contributes content (the id is derived from the
folder). The **VML tab → Export PCK** builds exactly this layout from a selected
mod (import metadata excluded). Install PCK copies a pack into the writable root
to be mounted on the next run.

Zip install (`VML.install_mod_from_zip`) extracts into the first writable root
(`res://mods` in dev); in an exported build `res://` is read-only, so zip install
returns an error — use packs there.

---

## What's new in 0.3.3

- **Type queries**: `VML.get_ids_of_type("cards")` returns every `ns:cards.*` id
  across all namespaces; `VML.get_all_of_type("cards")` returns
  `{ id: data }` for them. No more `list_namespaces()` + `get_all(ns + ":")`
  plumbing to enumerate all cards/units/items.
- **`patch_data`**: `VML.patch_data("game:cards.knight", {"hp": 999})` does a
  shallow field-level merge into the existing Dictionary — a mod overrides just
  the fields it changes. Missing ids behave like `set_data`. Nested Dictionaries /
  Arrays are replaced wholesale (no recursive merge).
- **Hook contract health**: `VML.get_hook_contract_health()` returns
  `{ declared, active, unhandled, undeclared, healthy }`;
  `VML.list_unmatched_hooks()` lists the concrete ids of handlers with no
  `register_hook_point` declaration (`undeclared`) and declared points with no
  handler (`unhandled`). The editor's Hooks tab shows the health line. This flags
  contract drift (renamed/removed game hook points, mods listening to undeclared
  hooks) that a plugin can detect — it cannot observe whether the game *fires* a
  hook, so this checks the declarative contract.
- **Image placeholders load via `ResourceLoader`**: `set_placeholder(id, "image",
  path)` and `get_resource(id)` resolve a `res://` image through
  `ResourceLoader`, returning the imported `CompressedTexture2D`. Raw
  `Image::load_from_file` cannot read the imported `.ctex` inside an exported
  `.pck`, so image placeholders broke in distributed builds — now they work.
  `user://` mod images (no import cache) still fall back to a raw `ImageTexture`.

Earlier release notes: `dist/vortarismodloader-changes-0.3.0.md` /
`RELEASE_NOTES.md`.

---

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

---

## FAQ

**What exactly is an id?**
`namespace:path`, lowercase. `path` is dotted, never slashed:
`game:units.knight`, not `game:units/knight`. Filesystem separators map to dots
(`assets/<ns>/<sub>/<name>.<ext>` → `ns:sub.name`). Namespaces are
`^[a-z0-9_]{1,32}$`; a mod's id equals its content namespace.

**Who wins when two providers claim the same id?**
Override arbitration is deterministic: **priority** (higher wins), then the
**explicit** flag (`register`, `id_overrides`, registry/placeholder routes are
explicit; path inference is implicit) — but only at *equal* priority — then
**mod id** ascending. Later-loaded mods have higher load-order priority.

**How do I override the base game?**
Ship a file at the **same namespace + same relative path** inside your mod
(`data/game/units/knight.json` overrides `game:units.knight`). For a file whose
path should map to a *different* id, use `extra.godot.id_overrides`.

**Where do mods live?**
`paths/mod_dir` (default `res://mods`) and `paths/unpacked_dir` (default
`res://mods-unpacked`), plus any roots added with `add_mod_root()`.
`user://vml/mods` is no longer a default root (0.3.0).

**How do I ship a mod to players?**
As a `.pck` pack with its content under `mods/<mod_id>/`, dropped into the
game's `mods/` folder. Packs are read-only and mount at startup. Zip install is
a dev-only convenience.

**Why doesn't my mod_main `_ready()` run?**
`VML` is not in the scene tree, so `add_child` of a mod_main never fires
`_ready`. Register hooks/config in `_init`.

**Why does `load("vml://...")` fail for JSON data ids?**
Data ids (`.json`/`.csv`) are not `Resource`s. Read them with `VML.get_data(id)`.

**How do I enumerate every card/unit/item across all mods?**
`VML.get_ids_of_type("cards")` / `VML.get_all_of_type("cards")` — the dotted
`ns:type.*` prefix is aggregated across every namespace.

**Does a disabled mod keep overriding?**
No. `disable_mod` drops the mod's hooks, content providers, and database entries
and resets its `content_scanned` flag, so re-enabling re-scans fresh.

**Can I hot-swap an id at runtime without a mod?**
`VML.reroute(id, path)` — highest priority, not persisted, cleared with
`clear_reroute`.

**Where is the persisted registry file?**
`res://vml/registry.json` by default (`paths/registry_path`), falling back to
`user://vml/registry.json` in read-only exports. Commit it to version control.

**What does `export_mods = "external"` change?**
Only user/custom (non-`res://`) roots are scanned — embedded mods packed into the
game's own pck are ignored. `"none"` scans nothing.

---

## Docs

- [mod_format.md](docs/mod_format.md) — mod package format & manifest reference
- [quickstart.md](docs/quickstart.md) — quick start + full mod walkthrough
- [registry.md](docs/registry.md) — ID content registry (persistence + editor panel + reroute + mod config)
- [hooks.md](docs/hooks.md) — declarative hooks guide (invoke/emit/check/ctx, contract health)
- [database.md](docs/database.md) — unified content database (modes, type queries, patch_data)
- [dev_hot_reload.md](docs/dev_hot_reload.md) — dev hot reload
- [release_mods.md](docs/release_mods.md) — shipping mods in a release build (pck structure, scan switches)
- [cross_platform.md](docs/cross_platform.md) — building on Windows / Linux / macOS
- [AI_DEBUGGING.md](docs/AI_DEBUGGING.md) — AI / headless-CLI debugging: MCP `run_script`
  API snippets, CLI args & exit codes

## Building

```bash
pip install scons
scons platform=windows target=template_debug arch=x86_64          # inside godot-cpp (once)
scons -j 8 platform=windows target=template_debug arch=x86_64 build_library=False \
      godot_cpp_path=<path-to-godot-cpp>
godot --headless --path demo --quit
godot --headless --path demo --script res://scripts/regression_test.gd
```

See [docs/cross_platform.md](docs/cross_platform.md) and
[.github/workflows/build.yml](.github/workflows/build.yml) (CI builds all three
platforms and attaches one all-platform addon zip to tag releases).

Chinese docs: [README.zh-CN.md](README.zh-CN.md)

## License

MIT.
