extends Node
## Functional smoke test / demo entry.
##
## In headless CI runs this prints the OK marker and quits; in the editor it
## stays running so the demo can be inspected. Later milestones turn this into
## the full "Vortaria" data-driven mini-game demo.

func _ready() -> void:
	if Engine.has_singleton("VML"):
		print("=== VortarisModLoader Demo OK ===")
	else:
		push_error("VML engine singleton is missing")

	# Headless CLI / CI smoke: exit after the marker.
	if DisplayServer.get_name() == "headless":
		get_tree().quit(0)
