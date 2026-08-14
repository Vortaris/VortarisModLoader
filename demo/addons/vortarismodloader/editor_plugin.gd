@tool
extends EditorPlugin

# VortarisModLoader 的运行时由 vortarismodloader.gdextension 自动加载。
# 此 EditorPlugin 默认不占用界面：点 Editor > Tools > "VML Mods" 才把管理面板
# 挂到左侧 dock（与 FileSystem/Scene 并列）。面板只通过 VML 引擎单例读写数据。

const PANEL_MENU_NAME := "VML Mods"

var _panel: Control = null


func _enter_tree() -> void:
	add_tool_menu_item(PANEL_MENU_NAME, _on_toggle_panel)


func _exit_tree() -> void:
	remove_tool_menu_item(PANEL_MENU_NAME)
	_close_panel()


func _on_toggle_panel() -> void:
	if _panel != null:
		_close_panel()
		return
	if not Engine.has_singleton("VML"):
		push_warning("VML engine singleton not loaded yet; panel will be empty.")
	_panel = preload("mod_manager_panel.gd").new()
	add_control_to_dock(DOCK_SLOT_LEFT_UL, _panel)


func _close_panel() -> void:
	if _panel:
		remove_control_from_docks(_panel)
		_panel.queue_free()
		_panel = null
