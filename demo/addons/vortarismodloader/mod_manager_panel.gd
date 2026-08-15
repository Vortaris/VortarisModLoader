@tool
extends Control
## VML Mods — legacy dock wrapper.
##
## The full mod-management UI now lives in the editor **main screen**
## (mods_main_screen.gd, the "VML" tab next to 2D/3D/Script/AssetLib). This panel
## hosts that same screen so the old left-bottom dock slot (and the T29
## instantiation test) keeps working for anyone who still wants a dock.

const MainScreen = preload("mods_main_screen.gd")


func _ready() -> void:
	name = "VML Mods"
	var screen := MainScreen.new()
	screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(screen)
