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
  `res://registry.json` (built-in defaults) then `user://vml/registry.json` (edits).

## API

```gdscript
VML.set_registry_entry("main_menu_bg", "res://assets/game/menus/bg.png", "image", "menu background")
VML.get_registry_entry("main_menu_bg")      # { path, type, description }
VML.get_registry()                          # { id: { ... } }
VML.remove_registry_entry("main_menu_bg")
VML.save_registry()                         # -> user://vml/registry.json
VML.load_registry()                         # reload from disk
```

## Editor: "VML IDs" panel

Open the plugin and you'll find **VML IDs** in the **right dock, next to the
Inspector** (tab-switched). It lets you:

- browse every registry entry (id / path / type / description), resizable columns;
- **New** + fill id/path/type/desc → **Apply** to create or update an entry;
- **Delete** a selected entry; **Save** persists to `user://vml/registry.json`;
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
