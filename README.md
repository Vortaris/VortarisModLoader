# VortarisModLoader

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
- **Dev hot reload**: mtime polling — edit a mod file and it applies live.
- **Native `vml://` loading**: `load("vml://ns:path")` works in exported builds.
- **Beginner-friendly API**: `get`/`load`/`exists`/`get_mod_path` sugar.
- **EditorPlugin**: off by default — open via Editor > Tools > "VML Mods"
  (Mods/IDs/Hooks tabs) + one-click mod skeleton wizard.
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

## Docs

- [mod_format.md](docs/mod_format.md) — mod package format & manifest reference
- [quickstart.md](docs/quickstart.md)
- [hooks.md](docs/hooks.md) — declarative hooks guide
- [database.md](docs/database.md) — unified content database
- [dev_hot_reload.md](docs/dev_hot_reload.md)

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
