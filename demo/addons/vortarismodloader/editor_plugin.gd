@tool
extends EditorPlugin

# VortarisModLoader 的运行时由 vortarismodloader.gdextension 自动加载。
#
# 两个面板默认打开（通过各自 dock 区域的选项卡切换）：
#   - "VML Mods"（mod 管理）：左侧底部，与 Import 面板并列（DOCK_SLOT_LEFT_BL）。
#   - "VML IDs"（ID 编辑器）：右侧，与 Inspector 并列（DOCK_SLOT_RIGHT_UL）。

var _mod_panel: Control = null
var _id_panel: Control = null


func _enter_tree() -> void:
	_mod_panel = preload("mod_manager_panel.gd").new()
	add_control_to_dock(DOCK_SLOT_LEFT_BL, _mod_panel)
	_id_panel = preload("id_editor_panel.gd").new()
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, _id_panel)


func _exit_tree() -> void:
	if _mod_panel:
		remove_control_from_docks(_mod_panel)
		_mod_panel.queue_free()
		_mod_panel = null
	if _id_panel:
		remove_control_from_docks(_id_panel)
		_id_panel.queue_free()
		_id_panel = null
