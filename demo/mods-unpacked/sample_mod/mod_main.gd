extends Node
## Sample mod entry point. Instantiated by VML.finish_startup(); registers
## declarative hooks in _init (VML attributes them to this mod automatically).

func _init() -> void:
	# Declare hook points the game offers (optional, for docs/editor discovery).
	VML.register_hook_point("game:modify_damage", "Rewrite outgoing damage",
			["current", "amount", "weapon"])
	VML.register_hook_point("game:on_entity_killed", "Any unit died", ["entity"])
	VML.register_hook_point("game:can_open_door", "May the player open this door?", ["door"])

	# invoke chain: double all outgoing damage.
	VML.add_hook("game:modify_damage", _on_modify_damage, 10)
	# emit listener.
	VML.add_hook("game:on_entity_killed", _on_entity_killed, 5)
	# check predicate: lock every door (demo of interception).
	VML.add_hook("game:can_open_door", _on_can_open_door, 0)


func _on_modify_damage(current: Variant, _amount: int, _weapon: String) -> Variant:
	return current * 2.0


func _on_entity_killed(entity: String) -> void:
	print("sample_mod: [hook] entity killed -> ", entity)


func _on_can_open_door(_door: String) -> bool:
	return false
