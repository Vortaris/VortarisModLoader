# ID Content Registry

The persisted content registry is a saveable `id → resource` route table. It is
the **game author's** layer: you declare which id means which resource by default,
mods stack above and override it.

## Concept

```
registry:   main_menu_bg -> res://assets/game/menus/bg.png   (default)
mod ships:  assets/mymod/data/game/menus/bg.png               (same id, higher priority)
game code:  load("vml://main_menu_bg")  ->  automatically the mod's version
```

- Registry entries are **base-layer explicit routes** (priority 0).
- Any mod provider (priority > 0) wins — override arbitration as usual.
- The registry is loaded automatically at `VML.finish_startup()`:
  `res://registry.json` (legacy built-in defaults), then the project-level path
  (default `res://vml/registry.json`, configurable via
  `vortarismodloader/registry_path`), then `user://vml/registry.json` (legacy edits).

## API

```gdscript
VML.set_registry_entry("main_menu_bg", "res://assets/game/menus/bg.png", "image", "menu background")
VML.get_registry_entry("main_menu_bg")      # { path, type, description, value? }
VML.get_registry()                          # { id: { ... } }
VML.remove_registry_entry("main_menu_bg")
VML.save_registry()                         # -> project-level path (res://vml/registry.json default)
VML.load_registry()                         # reload from disk
```

## Value providers (persisted `set_data`)

`VML.set_data("mygame:score", 1000, true)` persists the value as a
`__registry__` **value provider** (priority 0, empty path). It is saved to the
registry file under a `"value"` key, survives restarts, and is overridden by any
mod (priority > 0) shipping the same id. Old registry files without a `"value"`
key load unchanged (fully backward compatible).

## Placeholder IDs

A **placeholder id** is a developer-declared id with a **default value** — a
"declare an id first, fill it later" mechanism. You create the id in the editor,
it auto-registers on startup, and any resource reference can point at it:
`load("vml://mygame:mainmenu.bg")` resolves to the placeholder default until a
mod ships the same id and overrides it (placeholders are base-layer, priority 0).

```gdscript
# Resource placeholder: default is a path, resolved via get_resource / load().
VML.set_placeholder("mygame:mainmenu.bg", "image", "res://assets/game/menus/bg.png", "menu background")
var bg: Texture2D = load("vml://mygame:mainmenu.bg")     # the default until overridden

# Data placeholder: default is a constant, read via get_data().
VML.set_placeholder("mygame:start_health", "data", 100)
var hp: int = VML.get_data("mygame:start_health")
```

- **Storage**: placeholders live in the same project registry
  (`res://vml/registry.json` by default) and are saved with
  [VML.save_registry()](VMLModLoader.xml#class-vmlmodloader-method-save_registry);
  they are loaded automatically at `VML.finish_startup()` just like any registry
  entry.
- **Editor**: the **VML IDs** panel (right dock) has a single **New** button —
  registry routes and placeholders are unified. Fill id + type
  (image/scene/audio/data/…) + default value (a resource path, or a constant for
  data types) and press OK — it writes into the project registry so it is
  git-committable. A resource path default is a route; a data type accepts either
  a JSON constant (placeholder) or a `res://data/...` path (a route).
- **API**: `VML.set_placeholder(id, type, default, description="")` and
  `VML.get_placeholder_ids(type="")` (list / filter by type). A placeholder is
  also just a registry entry — `get_registry_entry(id)` returns
  `{ path | value, type, description, placeholder: true }`.
- **Mod overrides**: a mod shipping the same id (any priority > 0) beats the
  placeholder default, exactly like any other registry entry.

## Editor: "VML IDs" panel

Open the plugin and you'll find **VML IDs** in the **right dock, next to the
Inspector** (tab-switched). It lets you:

- browse every registry entry (id / path-or-default / type / description),
  resizable columns (drag the column headers);
- **New** (or **Edit**) + fill id/type/default/desc → **OK** to create or update an
  entry — the default value is a resource path (a route, or a placeholder default)
  or, for `data`, a JSON constant; a `res://data/...` path in the data field makes
  it a data route. Leaving the data constant empty stores an empty default.
- **Delete** a selected entry; **Save** persists to the project-level
  `res://vml/registry.json` (git-committable, falls back to user:// when read-only);
- **Reload** re-reads from disk.

Because `finish_startup()` loads the registry, your saved routes are applied every
launch — and mods can still override them at runtime.

## Hot-swapping content at runtime

```gdscript
# Game code reads content by id; swapping the background is one call:
VML.reroute("main_menu_bg", "res://assets/game/menus/bg_night.png")  # runtime override
VML.clear_reroute("main_menu_bg")                                     # back to default
```

`reroute` is highest-priority and not persisted — perfect for theme switches,
day/night cycles, or A/B testing.

## Per-mod config

Mods can declare a `config_schema` in `manifest.json`:

```json
"extra": { "godot": { "config_schema": { "type": "object", "properties": { "difficulty": { "type": "number" } } } } }
```

```gdscript
VML.get_config_schema("mymod")     # the schema
VML.get_config("mymod")            # {} if unset
VML.set_config("mymod", {"difficulty": 2.0})
```
