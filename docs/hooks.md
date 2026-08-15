# Declarative Hooks

VortarisModLoader does not rewrite GDScript source (no regex hooks). Instead, the
**game** declares hook points by calling the VML dispatch API at the places it
wants to be moddable; **mods** register `Callable`s on those points.

## Hook points are ids

Every hook point is a namespaced id, e.g. `game:modify_damage`. The namespace
prevents collisions between games/mods; ids make hooks discoverable and
documentable.

## Three semantics

### invoke — pipeline (rewrite a value)

The game calls `invoke_hook` with a starting value and arguments. Handlers run by
priority (higher first); each receives `(current, ...args)` and returns the
rewritten value, which becomes the next handler's `current`. The final value is
returned to the caller.

```gdscript
# game side
var final_damage: float = VML.invoke_hook("game:modify_damage", [10.0, "sword"], 10.0)

# mod side
func _on_modify_damage(current: Variant, _amount: int, _weapon: String) -> Variant:
	return current * 2.0
```

### emit — broadcast (no return)

```gdscript
VML.emit_hook("game:on_entity_killed", ["archer"])
```

### invoke_ctx — context pipeline (mutate a Dictionary)

The game passes a context Dictionary; handlers receive `(ctx, ...args)` and may
return a modified Dictionary. The final ctx is returned to the caller.

```gdscript
# game side
var ctx := VML.invoke_hook_ctx("game:on_hit", {"damage": 10.0, "element": "fire"}, [target])

# mod side
func _on_hit(ctx: Dictionary, _target: Node) -> Dictionary:
	ctx["damage"] = ctx["damage"] * 2.0
	return ctx
```

### check — predicate (veto)

Returns **bool = whether to allow**: `true` when no handler vetoes; **any handler
returning `false` short-circuits** and the whole check returns `false`. A handler
that returns a non-bool is treated as a veto.

```gdscript
if VML.check_hook("game:can_open_door", [door]):
	open(door)
```

## Registering handlers

```gdscript
VML.add_hook("game:modify_damage", _on_modify_damage, 10)  # priority 10
VML.remove_hook("game:modify_damage", _on_modify_damage)
```

When called from a `mod_main._init`, the handler is attributed to that mod
automatically (used for cleanup on unload). Handlers registered by other code are
attributed to `__runtime__`.

## Declaring hook points

Games should declare the hook points they offer so the editor can list them:

```gdscript
VML.register_hook_point("game:modify_damage", "Rewrite outgoing damage",
		["current", "amount", "weapon"])
```

`VML.list_hook_points()` returns them; `VML.list_hooks()` returns what is
actually registered (plus which mods registered it).

## Important notes

- Prefer named methods over lambdas when connecting to **VML signals** — a lambda
  held across engine shutdown crashes at exit. (This does not apply to `add_hook`,
  whose Callables are held by the mod itself.)
- `preload()` cannot use custom loaders; dynamic content goes through
  `VML.get_data`/`VML.get_resource`/`load("vml://…")`.
