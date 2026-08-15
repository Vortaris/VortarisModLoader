# Shipping & Installing Mods in a Release Build

This guide covers where mods live in a **released** (exported) game, how a player
installs them, and how to configure scanning for your export.

## Where installed mods live

In a release build, `user://` resolves to the engine's per-project user data
directory. On Windows that is:

```
%APPDATA%\Godot\app_userdata\<project_name>\vml\mods\
```

For the demo (project name `VortarisModLoader Demo`) the path is:

```
C:\Users\<you>\AppData\Roaming\Godot\app_userdata\VortarisModLoader Demo\vml\mods\
```

Each zip-installed mod is extracted to `vml/mods/<mod_id>/` (transactional:
temp + manifest validation + rename + rollback). Mod enable-state lives in
`vml/profile.json`, per-mod configs in `vml/configs/`, and the registry in
`vml/registry.json` when the project-level path is read-only.

## Zip structure

Any standard zip. The mod root is the folder that directly contains
`manifest.json` — either the zip root or a single top folder (`<mod_id>/`); the
installer accepts both.

```
archer_pack.zip
  manifest.json            # required
  mod_main.gd              # optional
  data/archerpack/units/ranger.json   -> id "archerpack:units.ranger"
```

## Installing a zip at runtime

The game (or your in-game UI) calls:

```gdscript
var err: int = VML.install_mod_from_zip("user://downloads/archer_pack.zip")
```

The mod is extracted into `user://vml/mods/<mod_id>/` and activated immediately.

## Scanning controls

| Project setting | Values | Meaning |
|---|---|---|
| `vortarismodloader/mod_paths` | PackedStringArray | Root directories scanned for mods. Default `["res://mods-unpacked", "user://vml/mods"]`. |
| `vortarismodloader/scan_user_mods` | bool (default true) | When false, `user://vml/mods` is **not** scanned at boot — zip installs still insert directly. |
| `vortarismodloader/export_mods` | `"embedded"` / `"external"` / `"none"` | `embedded` (default): scan res:// roots too. `external`: only user/custom roots. `none`: no scanning at all. |
| `vortarismodloader/validate_on_startup` | bool (default true) | Validate mods at startup; problems are marked but never refuse to boot. |
| `vortarismodloader/registry_path` | String | Project-level registry file. Default `res://vml/registry.json`; falls back to `user://vml/registry.json` when res:// is read-only (exports). |

## Embedded vs external mods

- **Embedded**: mods shipped inside the exported PCK (under `res://mods-unpacked/`
  or any res:// root in `mod_paths`). Read-only at runtime — players cannot edit
  them, and `uninstall_mod` refuses them.
- **External / user**: mods under `user://vml/mods/` (or a custom non-res:// root).
  Installed/uninstalled at runtime, persisted across launches.

For a release that ships a curated set of mods, leave `export_mods` at
`"embedded"`. For a mod-first game that expects players to add mods from disk,
use `"external"` (skip embedded) or keep the default and let players drop zips
into `user://vml/mods/` plus install via the in-game UI.

## Custom mod roots

Add a directory of unpacked mods at runtime:

```gdscript
VML.add_mod_root("user://my_mods")
VML.rescan()
```

Roots are persisted to ProjectSettings (`vortarismodloader/mod_paths`), so
`rescan()` (and the next boot) pick them up. Remove with `VML.remove_mod_root()`.
