# Dev Hot Reload

In development (editor or debug templates) you can edit mod files while the
project runs and see changes applied live.

## How it works

`VML.start_hot_reload()` (or the EditorPlugin in the editor) spawns a poller that
checks mtime+size of every watched content file every ~0.5s. On a change it calls
`VML.reload_resources([...paths])`, which:

1. re-scans the affected mod (added/removed/renamed files),
2. refreshes that mod's entries in the content database,
3. emits `database_entry_changed(id)` and `registry_rebuilt`.

## Manual reload

```gdscript
VML.reload_resources(["res://mods-unpacked/sample_mod/data/mymod/units/archer.json"])
```

## Notes

- Scene/script hot reload follows Godot's `CACHE_MODE_REPLACE` semantics: new
  loads get fresh data; **existing instances keep stale data** (a known Godot
  limitation). `VML.instantiate` always loads fresh in dev.
- Release templates do not poll unless `start_hot_reload` is explicitly called.
