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

	# Routing layer demo: resolve + data by namespaced id.
	var peasant: Dictionary = VML.get_data("game:units/peasant")
	print("peasant  -> ", JSON.stringify(peasant))
	print("knight   -> ", VML.resolve("game:units/knight"))
	print("ns       -> ", VML.list_namespaces())

	if DisplayServer.get_name() == "headless":
		get_tree().quit(0)
