# Mod Package Format

A mod is a folder (unpacked) or a zip (shipped). Both share the same layout.

## Layout

```
<mod_id>/
  manifest.json          # required
  mod_main.gd            # optional entry point (extends Node)
  icon.png               # optional, shown in the manager panel
  assets/<namespace>/<path>.<ext>   # content ids  -> "namespace:path"
  data/<namespace>/<path>.<ext>     # content ids  -> "namespace:path"
```

- `<mod_id>` equals the manifest's `namespace` (e.g. `my_mod`).
- A zip contains this tree at its root, or under a single top folder (`<mod_id>/`);
  the installer accepts both.
- A mod may provide content under **any** namespace: its own new content uses its
  own namespace; overrides of the base game use the game's namespace (`game`);
  overrides of another mod use that mod's namespace.

## manifest.json

Thunderstore-compatible field names; Vortaris-specific keys live under `extra.godot`.

| field | type | notes |
|---|---|---|
| `namespace` | string | **content namespace = mod id**, required `^[a-z0-9_]{1,32}$` |
| `name` | string | display name |
| `version_number` | string | `"1.2.0"` (semver) |
| `description` / `website_url` | string | optional |
| `dependencies` | string[] | `"lib_mod"` or `"lib_mod@>=1.0"`; loaded before this mod |
| `optional_dependencies` | string[] | enabled only when present |
| `load_before` / `load_after` | string[] | extra ordering constraints |
| `incompatibilities` | string[] | mutually exclusive; enforced at runtime |
| `extra.godot.main_script` | string | default `"mod_main.gd"` |
| `extra.godot.icon` | string | default `"icon.png"` |
| `extra.godot.asset_dirs` / `data_dirs` | string[] | default `["assets"]` / `["data"]` |

Example:

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
			"main_script": "mod_main.gd"
		}
	}
}
```

## mod_main.gd

Optional entry point. Must `extends Node`. Register hooks/config in **`_init`**
(not `_ready` — the VML singleton is not in the scene tree). Hooks registered
there are attributed to this mod automatically.

```gdscript
extends Node

func _init() -> void:
	VML.add_hook("game:modify_damage", _on_modify_damage, 10)

func _on_modify_damage(current: Variant, _amount: int, _weapon: String) -> Variant:
	return current * 2.0
```

## Overriding game content

A mod overrides a base id by shipping a file at the **same** implicit path inside
its own package. Example: base game has `data/game/units/knight.json` → id
`game:units.knight`. A mod ships `data/game/units/knight.json` in its own tree →
the mod's version wins (later-loaded mod wins on ties).

## Zip distribution

Any standard zip. Install at runtime with `VML.install_mod_from_zip("path.zip")`,
or drop it into the game's `mods/` folder if the game wires that up.
