extends SceneTree
## Minimal, newcomer-facing demo — the fastest way to see what the loader does.
## Run:  godot --headless --path demo --script res://scripts/quickstart.gd

func _initialize() -> void:
	print("=== VortarisModLoader quickstart ===")

	# 1) Read data by id. Everything under res://data/game/ is indexed and
	#    preloaded into memory, so this is an O(1) lookup.
	var knight: Dictionary = VML.get_data("game:units.knight")
	print("knight: ", knight.get("name"), " (atk ", knight.get("attack"), ")")

	# 2) Instantiate a scene by id (assets/game/scenes/camp.tscn -> game:scenes.camp).
	var camp: Node = VML.instantiate("game:scenes.camp")
	print("camp scene instantiated: ", camp != null)
	camp.free()

	# 3) Hooks: sample_mod doubles all outgoing damage. The game calls
	#    invoke_hook at an instrumented point; mods rewrite the value.
	VML.finish_startup()
	print("damage 10 after mod hooks: ", VML.invoke_hook("game:modify_damage", [10.0, "sword"], 10.0))

	# 4) Which mods are active, and what else is indexed?
	print("load order: ", VML.get_load_order())
	print("namespaces: ", VML.list_namespaces())

	print("=== quickstart OK ===")
	quit(0)
