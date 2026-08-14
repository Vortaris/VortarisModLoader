# Quick Start

## Integrate into a game

1. Copy `addons/vortarismodloader/` into your project.
2. Enable the plugin (Project > Project Settings > Plugins).
3. Add a bootstrap autoload **first** in the autoload list:

```gdscript
# Bootstrap.gd
extends Node

func _ready() -> void:
	VML.finish_startup()
```

4. Expose your content through VML ids. Put data under `res://data/game/` and
   assets under `res://assets/game/` (namespace `game`). Read them by id:

```gdscript
var unit: Dictionary = VML.get_data("game:units/peasant")
var scene: PackedScene = VML.get_resource("game:scenes/camp")
```

5. Declare hook points anywhere you want mods to change behavior:

```gdscript
VML.register_hook_point("game:modify_damage", "Rewrite outgoing damage", ["current", "amount", "weapon"])
var dmg: float = VML.invoke_hook("game:modify_damage", [base, weapon], base)
```

## Write a mod

Create `res://mods-unpacked/<mod_id>/` — use the editor wizard (VML Mods dock →
Create Mod) or by hand:

```
mymod/
  manifest.json
  mod_main.gd
  assets/mymod/icons/archer.png
  data/mymod/units/archer.json
  data/game/units/knight.json   # overrides the base knight
```

- New content → your own namespace (`mymod`).
- Overrides → the target's namespace (`game`), same implicit path.

Run the project in the editor; the mod applies. Edit a mod file while running —
dev hot reload refreshes it live.

## Install a zip mod at runtime

```gdscript
var err: int = VML.install_mod_from_zip("user://downloads/archer_pack.zip")
```

The zip is extracted to `user://vml/mods/<mod_id>/` and activated.
