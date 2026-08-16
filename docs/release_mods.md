# Shipping & Installing Mods in a Release Build

This guide covers where mods live in a **released** (exported) game, how a player
installs them, and how to configure scanning for your export.

## Two distribution paths (0.3.0)

| Path | Use for | Read-only? |
|---|---|---|
| **`.pck` pack** | players / released builds | yes (mounted read-only) |
| **unpacked folder** | development (source tree) | writable in dev |
| **`.zip` install** | optional dev convenience | extracted, then treated like a folder |

**Recommended:** ship mods as `.pck` packs (read-only, namespaced under
`mods/<mod_id>/`, no extraction step). Use unpacked folders during development.
`VML.install_mod_from_zip()` is retained as an optional dev helper.

## Where mods live

Default mod roots — two per-directory settings compose the scan roots (0.3.2):

```
vortarismodloader/paths/mod_dir        = "res://mods"          # .pck packs + zip-installed mods (dev writable)
vortarismodloader/paths/unpacked_dir   = "res://mods-unpacked" # unpacked dev mod folders
```

(`VML.get_mod_roots()` returns the composed list. Legacy 0.3.0/0.3.1
`vortarismodloader/paths/mod_paths` / `vortarismodloader/mod_paths` array entries
are still read and merged, so an existing project keeps its custom roots.)

A `.pck` found under any configured root is mounted read-only at startup. Its
internal content must be namespaced under `mods/<mod_id>/` so it appears at
`res://mods/<mod_id>/` and is discovered like any unpacked mod folder.

Zip-installed mods are extracted into the first writable root (`res://mods` in
dev). `user://vml/mods/` is **no longer a default root** (0.3.0).

## Pack structure

A `.pck` is built with Godot's `PCKPacker` (or any packer) and its content is
namespaced under `mods/<mod_id>/`:

```
sample_mod.pck
  mods/sample_mod/
    manifest.json            # required (or the id is derived from the folder)
    mod_main.gd              # optional
    data/sample_mod/units/archer.json   -> id "sample_mod:units.archer"
```

A pack without a manifest still contributes its content; the mod id is derived
from the `mods/<mod_id>/` folder name.

## Installing mods

- **Packs**: drop the `.pck` under a configured mod root (e.g. the exported game's
  `mods/` folder). It is mounted at startup — no runtime call needed.
- **Editor flow**: the **VML Mods** main screen has **Export PCK** (builds a
  namespaced `res://mods/<mod_id>/` pack from the selected mod, exclusions applied
  automatically) and **Install PCK** (copies a `.pck` into the writable root and
  rescans; packs mount on the next game run). **Install Zip** stays as a legacy
  dev option.
- **Zip (dev)**: `VML.install_mod_from_zip("path.zip")` extracts into
  `res://mods/<mod_id>/` (dev). In an exported build `res://` is read-only, so zip
  install returns an error — use packs there.

## Scanning controls

| Project setting | Values | Meaning |
|---|---|---|
| `vortarismodloader/paths/mod_dir` | String dir (default `res://mods`) | Dev mod main directory: `.pck` packs + zip-installed mods are scanned here. |
| `vortarismodloader/paths/unpacked_dir` | String dir (default `res://mods-unpacked`) | Legacy unpacked dev mod folder, scanned too. |
| `vortarismodloader/paths/extra_roots` | PackedStringArray (runtime) | Extra roots added with `add_mod_root()`; not shown in the editor, merged by `get_mod_roots()`. Legacy `paths/mod_paths` / `vortarismodloader/mod_paths` arrays are also still merged. |
| `vortarismodloader/paths/scan_user_mods` | bool (default true) | When false, non-res:// roots are **not** scanned at boot. |
| `vortarismodloader/export/export_mods` | `"embedded"` / `"external"` / `"none"` | `embedded` (default): scan res:// roots too. `external`: only user/custom roots. `none`: no scanning at all. |
| `vortarismodloader/general/validate_on_startup` | bool (default true) | Validate mods at startup; problems are marked but never refuse to boot. |
| `vortarismodloader/paths/registry_path` | String path (default `res://vml/registry.json`) | Project-level registry file; falls back to `user://vml/registry.json` when res:// is read-only (exports). |
| `vortarismodloader/general/show_error_dialogs` | bool (default false) | Show a modal dialog listing mod errors at startup/rescan (non-headless only). Errors are always printed to the console. |
| `vortarismodloader/general/debug_output` | bool (default false) | Advanced `[vortarismodloader][dbg]` logging (scan, registry, hooks, data, packs). |
| `vortarismodloader/general/verbose` | bool (default false) | Detailed load logging. |
| `vortarismodloader/general/auto_finish_startup` | bool (default false) | Auto-run `finish_startup()` once the scene tree is ready. |
| `vortarismodloader/general/database_mode` | `"data"` / `"all"` / `"off"` (default `"data"`) | How much content is preloaded into the content database. |

## Embedded vs external mods

- **Embedded**: mods shipped inside the exported PCK (under `res://mods/` or
  `res://mods-unpacked/`). Read-only at runtime — players cannot edit them, and
  `uninstall_mod` refuses them.
- **External / user**: mods under a custom non-res:// root. Installed/uninstalled
  at runtime, persisted across launches.

For a release that ships a curated set of mods, leave `export_mods` at
`"embedded"` and ship the mods as `.pck` packs under `res://mods/`.

## Custom mod roots

Add a directory of unpacked mods (or packs) at runtime:

```gdscript
VML.add_mod_root("user://my_mods")
VML.rescan()
```

Roots are persisted to ProjectSettings (`vortarismodloader/paths/extra_roots`), so
`rescan()` (and the next boot) pick them up. Remove with `VML.remove_mod_root()`.
