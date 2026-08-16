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
| `extra.godot.id_overrides` | object | `{ "<rel path>": "<full id>" }` — explicitly map a file to an id, beating path inference (see below) |
| `extra.godot.config_schema` | object | JSON-Schema-ish `{type, properties}`; the editor generates the Config dialog form from it, and `VML.get_config_schema()` returns it |

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

## id namespace source

The id namespace comes from the **directory name**, never from the file or the
mod id:

```
assets/<ns>/<path>.<ext>   -> id "ns:path"
data/<ns>/<path>.<ext>     -> id "ns:path"
```

- A mod's own new content uses **its own namespace**: `data/mymod/units/archer.json`
  → id `mymod:units.archer`.
- To **override** original content, ship a file under the **original namespace**
  at the **same relative path**: base has `data/game/units/knight.json` → id
  `game:units.knight`; a mod shipping `data/game/units/knight.json` overrides it.
- The same rule applies to overriding another mod's content: use that mod's
  namespace and path.

## Overriding game content

A mod overrides a base id by shipping a file at the **same** implicit path inside
its own package. Example: base game has `data/game/units/knight.json` → id
`game:units.knight`. A mod ships `data/game/units/knight.json` in its own tree →
the mod's version wins (later-loaded mod wins on ties).

## Explicit id overrides (`extra.godot.id_overrides`)

Sometimes a file should be addressable under an id that its path does not imply
(e.g. a data file named for its source living under the mod's own namespace but
targeting the `game` namespace). Declare the mapping in `manifest.json`:

```json
{
	"name": "Override Mod",
	"namespace": "override_mod",
	"version_number": "1.0.0",
	"extra": {
		"godot": {
			"id_overrides": {
				"data/override_mod/units/archer.json": "game:units.elite_archer",
				"data/override_mod/units/archer_b.json": "game:units.elite_archer"
			}
		}
	}
}
```

- Keys are **paths relative to the mod root** (`data/...`, `assets/...`); values
  are full dotted ids (`ns:path`).
- An explicit override **wins over path inference**: the file is indexed under the
  override id only — it does not also get its path-inferred id.
- Several files may point at the **same id**; they are registered as normal
  providers and resolved by the standard override arbitration (priority, then
  explicit, then mod id).
- An invalid override id (bad namespace or path) is a manifest error surfaced at
  discovery time.

## Pck distribution (0.3.0)

A `.pck` file dropped under a configured mod root is **mounted read-only** at
startup (`ProjectSettings.load_resource_pack`). Its content must live under a
`mods/<mod_id>/` namespace inside the pack so it lands at `res://mods/<mod_id>/`
and is discovered like any unpacked mod folder:

```
sample_mod.pck
  mods/sample_mod/
    manifest.json
    data/sample_mod/units/archer.json
```

- After mounting, the pack's content is indexed with the **same scanner rules**
  (path inference **and** `id_overrides` apply).
- A pack that ships **no manifest** still contributes its content: the mod id is
  derived from the `mods/<mod_id>/` folder.
- Pcks are the **distribution** mechanism — they are read-only, so they pair with
  exported builds. **Development** uses unpacked folders under
  `res://mods-unpacked` / `res://mods`; zip install is an optional dev convenience.

## Zip distribution (optional, dev)

Any standard zip. Install at runtime with `VML.install_mod_from_zip("path.zip")`.
The zip is extracted into the first writable configured mod root (in dev,
`res://mods`). For shipping to players, prefer **.pck packs** (read-only, no
extraction); zip install is kept for development workflows.
