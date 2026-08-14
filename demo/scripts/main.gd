extends Node
## Functional smoke / demo entry (M2: routing layer).
## Headless CI prints the OK marker and quits; the editor run stays alive so the
## demo can be inspected. Later milestones turn this into the full "Vortaria"
## data-driven mini-game demo.

func _ready() -> void:
	if not Engine.has_singleton("VML"):
		push_error("VML engine singleton is missing")
		get_tree().quit(1)
		return

	print("=== VortarisModLoader Demo OK ===")
	print("db mode  -> ", VML.get_database_mode())
	print("ns       -> ", VML.list_namespaces())

	# Unified database: everything was preloaded at startup.
	var units: Dictionary = VML.get_all("game:units/")
	for id in units:
		var u: Dictionary = units[id]
		print("unit ", id, " -> ", u.get("name"), " (atk ", u.get("attack"), ")")

	# Mutable database: rewrite a live entry in place.
	VML.set_data("game:units/peasant", {"name": "Peasant MK2", "health": 60})
	print("modified -> ", JSON.stringify(VML.get_data("game:units/peasant")))

	if DisplayServer.get_name() == "headless":
		get_tree().quit(0)
