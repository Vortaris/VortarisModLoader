# Dev Hot Reload

In development (editor or debug templates) you can edit mod files while the
project runs and see changes applied live.

## How it works

`VML.start_hot_reload(interval = 0.5)` (or the EditorPlugin in the editor)
spawns a `VMLHotReloader` node that polls mtime+size of every watched content
file every `interval` seconds (clamped to >= 0.05). On a change it calls
`VML.reload_resources([...paths])`, which:

1. re-scans the affected mod (added/removed/renamed files),
2. refreshes that mod's entries in the content database,
3. emits `database_entry_changed(id)` and `registry_rebuilt`.

The watcher covers `VML.get_content_roots()` — the base layer plus every enabled
mod's asset/data dirs. It re-seeds its watched directories every ~5s so newly
added files are picked up without a manual rescan.

## Controls

```gdscript
VML.start_hot_reload(0.5)    # start the poller (dev builds / editor)
VML.reload_resources([...])  # manual: apply specific changed paths now
VML.get_content_roots()      # the directories currently watched
```

`VMLHotReloader` is also a public node class: add it to your own scene and call
`set_poll_interval(seconds)` / `rescan()` if you want finer control than the
`VML.start_hot_reload` convenience.

## Manual reload

```gdscript
VML.reload_resources(["res://mods-unpacked/sample_mod/data/mymod/units/archer.json"])
```

## Notes

- Scene/script hot reload follows Godot's `CACHE_MODE_REPLACE` semantics: new
  loads get fresh data; **existing instances keep stale data** (a known Godot
  limitation). `VML.instantiate` always loads fresh in dev.
- Release templates do not poll unless `start_hot_reload` is explicitly called.
