# Unified Content Database

By default the loader preloads every **data** id (`.json/.csv/.tres`) into an
in-memory repository at startup. Id lookups then are O(1) hash hits with zero
file I/O — the "load everything, then ask fast" model.

## Modes (`vortarismodloader/database_mode`)

| mode | behavior |
|---|---|
| `data` (default) | data files preloaded; heavy assets load on demand |
| `all` | data + assets preloaded (watch memory; exclude big dirs) |
| `off` | fully lazy; only the router caches |

Change at runtime with `VML.set_database_mode("...")`; read with
`VML.get_database_mode()`.

## Reading

```gdscript
VML.get_data("game:units.peasant")        # O(1) after preload
VML.get_all("game:units.")                # { id: value } for a prefix
```

## Rewriting live

The repository is mutable — "reload the resource pointer" applies to live content:

```gdscript
VML.set_data("game:units.peasant", {"name": "Peasant MK2", "health": 60})
VML.delete_data("game:units.peasant")     # falls back to the file
```

`set_data`/`delete_data` emit `database_entry_changed(id)`. Hot reload refreshes
the affected mod's entries automatically.

## Signals

- `database_loaded` — after a full preload.
- `database_entry_changed(id)` — an entry was rewritten/removed.
