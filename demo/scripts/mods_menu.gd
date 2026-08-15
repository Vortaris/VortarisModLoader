extends Control
## In-game mod management menu (runtime demo UI). Lists every mod with its
## enable/disable state and error marker, lets you toggle mods, install a zip,
## uninstall user://-installed mods, and rescan.

var _list: ItemList
var _status: Label


func _ready() -> void:
	name = "ModsMenu"
	set_anchors_preset(Control.PRESET_TOP_LEFT)

	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(panel)

	var vbox := VBoxContainer.new()
	panel.add_child(vbox)

	var title := Label.new()
	title.text = "VML Mods"
	vbox.add_child(title)

	var btns := HBoxContainer.new()
	vbox.add_child(btns)
	_add_btn(btns, "Toggle", _on_toggle)
	_add_btn(btns, "Install Zip", _on_install_zip)
	_add_btn(btns, "Uninstall", _on_uninstall)
	_add_btn(btns, "Rescan", _on_rescan)

	_list = ItemList.new()
	_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(_list)

	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(_status)

	refresh()


func _add_btn(parent: Control, text: String, callable: Callable) -> void:
	var b := Button.new()
	b.text = text
	b.pressed.connect(callable)
	parent.add_child(b)


func refresh() -> void:
	if not Engine.has_singleton("VML"):
		_status.text = "VML engine singleton not loaded"
		return
	_list.clear()
	var ids := VML.get_mod_ids()
	for mod_id in ids:
		var state := "enabled" if VML.is_mod_enabled(mod_id) else "disabled"
		var errs := VML.get_mod_errors(mod_id)
		if errs.size() > 0:
			state += " (!)"
		_list.add_item("%s  [%s]" % [mod_id, state])
	_status.text = "%d mods · db=%s" % [ids.size(), VML.get_database_mode()]


func _selected_mod_id() -> String:
	var sel := _list.get_selected_items()
	if sel.is_empty():
		return ""
	var text: String = _list.get_item_text(sel[0])
	return text.split("  [")[0]


func _on_toggle() -> void:
	if not Engine.has_singleton("VML"):
		return
	var id := _selected_mod_id()
	if id.is_empty():
		_status.text = "select a mod first"
		return
	if VML.is_mod_enabled(id):
		VML.disable_mod(id)
		_status.text = "disabled %s" % id
	else:
		VML.enable_mod(id)
		_status.text = "enabled %s" % id
	refresh()


func _on_install_zip() -> void:
	if not Engine.has_singleton("VML"):
		return
	var fd := FileDialog.new()
	fd.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	fd.access = FileDialog.ACCESS_FILESYSTEM
	fd.add_filter("*.zip", "Vortaris Mod (zip)")
	add_child(fd)
	fd.file_selected.connect(func(p: String):
		var err := VML.install_mod_from_zip(p)
		_status.text = "install: %s" % error_string(err)
		fd.queue_free())
	fd.canceled.connect(func(): fd.queue_free())
	fd.popup_centered_ratio(0.5)


func _on_uninstall() -> void:
	if not Engine.has_singleton("VML"):
		return
	var id := _selected_mod_id()
	if id.is_empty():
		_status.text = "select a mod first"
		return
	if not VML.get_mod_path(id).begins_with("user://"):
		_status.text = "only user:// (installed) mods can be uninstalled: %s" % id
		return
	var err := VML.uninstall_mod(id)
	_status.text = "uninstall %s: %s" % [id, error_string(err)]
	refresh()


func _on_rescan() -> void:
	if not Engine.has_singleton("VML"):
		return
	VML.rescan()
	_status.text = "rescanned: %d mods" % VML.get_mod_ids().size()
	refresh()
