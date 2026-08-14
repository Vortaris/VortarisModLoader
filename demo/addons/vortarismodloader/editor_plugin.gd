@tool
extends EditorPlugin

# VortarisModLoader 的运行时由 vortarismodloader.gdextension 自动加载。
# 此 EditorPlugin 注册 mod 管理面板（dock），面板只通过 VML 引擎单例读写数据。

var _panel: Control = null


func _enter_tree() -> void:
	if not Engine.has_singleton("VML"):
		# GDExtension 尚未加载（例如首次打开项目扫描中）；下次刷新时面板仍会检查。
		pass
	_panel = preload("mod_manager_panel.gd").new()
	add_control_to_dock(DOCK_SLOT_LEFT_UL, _panel)


func _exit_tree() -> void:
	if _panel:
		remove_control_from_docks(_panel)
		_panel.queue_free()
		_panel = null
