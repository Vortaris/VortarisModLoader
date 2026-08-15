@tool
extends EditorPlugin
## VortarisModLoader editor plugin.
##
## VML Mods is a **main-screen workspace** (the "VML" tab, next to 2D/3D/Script/
## AssetLib): mod list + drag-to-reorder priority + details/hooks/content. The
## old left-bottom dock was removed to avoid a duplicate entry — mod_manager_panel.gd
## remains as a thin wrapper that hosts the same screen.
##
## VML IDs stays in the right dock (next to the Inspector) and edits the persisted
## id registry / placeholders.

var _main_screen: Control = null
var _id_panel: Control = null


func _enter_tree() -> void:
	# Main screen (VML tab).
	_main_screen = preload("mods_main_screen.gd").new()
	EditorInterface.get_editor_main_screen().add_child(_main_screen)
	_make_visible(false)
	# ID registry editor stays in the right dock.
	_id_panel = preload("id_editor_panel.gd").new()
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, _id_panel)


func _exit_tree() -> void:
	if _main_screen:
		EditorInterface.get_editor_main_screen().remove_child(_main_screen)
		_main_screen.queue_free()
		_main_screen = null
	if _id_panel:
		remove_control_from_docks(_id_panel)
		_id_panel.queue_free()
		_id_panel = null


func _has_main_screen() -> bool:
	return true


func _make_visible(visible: bool) -> void:
	if _main_screen:
		_main_screen.visible = visible


func _get_plugin_name() -> String:
	return "VML"


func _get_plugin_icon() -> Texture2D:
	var icon := load("res://addons/vortarismodloader/icon.svg")
	if icon is Texture2D:
		return icon
	return EditorInterface.get_editor_theme().get_icon("Node", "EditorIcons")


func _handles(_object: Object) -> bool:
	return true
