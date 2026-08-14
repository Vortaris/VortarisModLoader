@tool
extends EditorPlugin

# VortarisModLoader 的运行时由 vortarismodloader.gdextension 自动加载。
#
# 两个面板：
#   - "VML IDs"（ID 编辑器）：常驻右侧 dock，与 Inspector 并列（选项卡切换）。
#     管理持久化的 id → 资源路由，可保存到 user://vml/registry.json。
#   - "VML Mods"（mod 管理）：默认不占界面，Editor > Tools > "VML Mods" 打开到左侧 dock。

const PANEL_MENU_NAME := "VML Mods"

var _mod_panel: Control = null
var _id_panel: Control = null


func _enter_tree() -> void:
	add_tool_menu_item(PANEL_MENU_NAME, _on_toggle_mod_panel)
	_id_panel = preload("id_editor_panel.gd").new()
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, _id_panel)


func _exit_tree() -> void:
	remove_tool_menu_item(PANEL_MENU_NAME)
	if _id_panel:
		remove_control_from_docks(_id_panel)
		_id_panel.queue_free()
		_id_panel = null
	_close_mod_panel()


func _on_toggle_mod_panel() -> void:
	if _mod_panel != null:
		_close_mod_panel()
		return
	if not Engine.has_singleton("VML"):
		push_warning("VML engine singleton not loaded yet; panel will be empty.")
	_mod_panel = preload("mod_manager_panel.gd").new()
	add_control_to_dock(DOCK_SLOT_LEFT_UL, _mod_panel)


func _close_mod_panel() -> void:
	if _mod_panel:
		remove_control_from_docks(_mod_panel)
		_mod_panel.queue_free()
		_mod_panel = null
