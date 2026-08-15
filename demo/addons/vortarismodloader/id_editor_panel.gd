@tool
extends Control
## VML ID editor — right dock, next to the Inspector.
##
## Registry tab edits the persisted id→resource route table; Loaded tab browses
## every id currently indexed. New/Edit use a popup; type is a dropdown.

const TYPES := ["data", "scene", "script", "image", "audio", "font", "resource"]

var _tree: Tree
var _tabs: TabContainer
var _status: Label
var _browse_tree: Tree
var _browse_filter_ns: LineEdit
var _browse_filter_type: OptionButton
var _dlg: ConfirmationDialog
var _dlg_error: Label
var _dlg_id: LineEdit
var _dlg_path: LineEdit
var _dlg_type: OptionButton
var _dlg_desc: LineEdit
var _selected_id := ""

# Placeholder editor (B6: ID placeholder system).
var _ph_tree: Tree
var _ph_dlg: ConfirmationDialog
var _ph_error: Label
var _ph_id: LineEdit
var _ph_type: OptionButton
var _ph_value: LineEdit
var _ph_value_text: TextEdit
var _ph_value_row: HBoxContainer
var _ph_desc: LineEdit


func _ready() -> void:
	name = "VML IDs"

	var vbox := VBoxContainer.new()
	vbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(vbox)

	var hbox := HBoxContainer.new()
	vbox.add_child(hbox)
	_add_btn(hbox, "New", _on_new)
	_add_btn(hbox, "Edit", _on_edit)
	_add_btn(hbox, "Delete", _on_delete)
	_add_btn(hbox, "Save", _on_save)
	_add_btn(hbox, "Reload", _on_reload)
	_add_btn(hbox, "New Placeholder", _on_new_placeholder)

	_tabs = TabContainer.new()
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(_tabs)

	_tree = Tree.new()
	_setup_columns(_tree, ["ID", "Path", "Type", "Desc"], [130, 180, 60, 80])
	_tree.name = "Registry"
	_tree.item_selected.connect(_on_selected)
	_tabs.add_child(_tree)

	var loaded := Tree.new()
	_setup_columns(loaded, ["ID", "Path", "Provider", "Type"], [130, 180, 70, 60])
	loaded.name = "Loaded"
	_tabs.add_child(loaded)
	loaded.item_selected.connect(_on_loaded_selected)
	loaded.set_meta("loaded_tree", true)

	# Browse tab: every id expandable to its providers (best highlighted),
	# filtered by namespace and data/type tag.
	var browse_vbox := VBoxContainer.new()
	browse_vbox.name = "Browse"
	_tabs.add_child(browse_vbox)
	var filter_row := HBoxContainer.new()
	browse_vbox.add_child(filter_row)
	filter_row.add_child(_lbl("ns:"))
	_browse_filter_ns = LineEdit.new()
	_browse_filter_ns.placeholder_text = "game"
	_browse_filter_ns.text_changed.connect(func(_t: String): _refresh_browse())
	filter_row.add_child(_browse_filter_ns)
	filter_row.add_child(_lbl("type:"))
	_browse_filter_type = OptionButton.new()
	_browse_filter_type.add_item("all")
	for t in TYPES:
		_browse_filter_type.add_item(t)
	_browse_filter_type.item_selected.connect(func(_i: int): _refresh_browse())
	filter_row.add_child(_browse_filter_type)
	_browse_tree = Tree.new()
	_setup_columns(_browse_tree, ["ID / Provider", "Path", "Priority", "Explicit"], [150, 220, 60, 60])
	_browse_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	browse_vbox.add_child(_browse_tree)

	# Placeholders tab: developer-declared ids with default values.
	_ph_tree = Tree.new()
	_setup_columns(_ph_tree, ["ID", "Type", "Default", "Desc"], [140, 70, 200, 120])
	_ph_tree.name = "Placeholders"
	_ph_tree.item_selected.connect(_on_placeholder_selected)
	_tabs.add_child(_ph_tree)

	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(_status)

	_build_dialog()
	_build_placeholder_dialog()

	refresh()


func _setup_columns(tree: Tree, titles: Array, widths: Array) -> void:
	tree.columns = titles.size()
	tree.set_column_titles_visible(true)
	for i in titles.size():
		tree.set_column_title(i, titles[i])
		# Only the last column expands; the rest are user-draggable with a sensible
		# minimum width and never clip their content.
		tree.set_column_expand(i, i == titles.size() - 1)
		tree.set_column_custom_minimum_width(i, widths[i])
		tree.set_column_clip_content(i, false)


func _add_btn(parent: Control, text: String, callable: Callable) -> void:
	var b := Button.new()
	b.text = text
	b.pressed.connect(callable)
	parent.add_child(b)


func _build_dialog() -> void:
	_dlg = ConfirmationDialog.new()
	_dlg.ok_button_text = "OK"
	_dlg.cancel_button_text = "Cancel"
	var form := GridContainer.new()
	form.columns = 2
	_dlg.add_child(form)
	form.add_child(_lbl("ID"))
	_dlg_id = LineEdit.new()
	_dlg_id.placeholder_text = "mygame:mainmenu.bg"
	form.add_child(_dlg_id)
	form.add_child(_lbl("Path"))
	var path_row := HBoxContainer.new()
	_dlg_path = LineEdit.new()
	_dlg_path.placeholder_text = "res://assets/... or user://..."
	_dlg_path.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	path_row.add_child(_dlg_path)
	var browse := Button.new()
	browse.text = "Browse"
	browse.pressed.connect(_on_browse)
	path_row.add_child(browse)
	form.add_child(path_row)
	form.add_child(_lbl("Type"))
	_dlg_type = OptionButton.new()
	form.add_child(_dlg_type)
	form.add_child(_lbl("Desc"))
	_dlg_desc = LineEdit.new()
	form.add_child(_dlg_desc)
	_dlg_error = Label.new()
	_dlg_error.add_theme_color_override("font_color", Color(1, 0.3, 0.3))
	_dlg_error.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_dlg_error.custom_minimum_size = Vector2(380, 0)
	_dlg.add_child(_dlg_error)
	add_child(_dlg)
	_dlg.confirmed.connect(_on_dialog_ok)


func _lbl(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.custom_minimum_size = Vector2(46, 0)
	return l


func refresh() -> void:
	if not Engine.has_singleton("VML"):
		_status.text = "VML not loaded"
		return
	var reg_count := VML.get_registry().size()
	var loaded_count := _count_loaded_ids()
	var ph_count := VML.get_placeholder_ids().size()
	_status.text = "%d registry entries · %d ids loaded · %d placeholders" % [reg_count, loaded_count, ph_count]
	_refresh_registry()
	_refresh_loaded()
	_refresh_browse()
	_refresh_placeholders()


func _count_loaded_ids() -> int:
	var ids := VML.list_ids()
	var n := 0
	for ns in ids:
		n += ids[ns].size()
	return n


func _refresh_registry() -> void:
	_tree.clear()
	var root := _tree.create_item()
	var reg: Dictionary = VML.get_registry()
	for id in reg:
		var e: Dictionary = reg[id]
		var item := _tree.create_item(root)
		item.set_text(0, id)
		item.set_text(1, e.get("path", ""))
		item.set_text(2, e.get("type", ""))
		item.set_text(3, e.get("description", ""))
		item.set_meta("id", id)


func _refresh_loaded() -> void:
	var loaded := _find_loaded_tree()
	if loaded == null:
		return
	loaded.clear()
	var root := loaded.create_item()
	var ids := VML.list_ids()
	for ns in ids:
		for path in ids[ns]:
			var full: String = str(ns) + ":" + str(path)
			var info: Dictionary = VML.get_id_info(full)
			var item := loaded.create_item(root)
			item.set_text(0, full)
			item.set_text(1, info.get("path", ""))
			item.set_text(2, info.get("provider_mod", ""))
			item.set_text(3, info.get("type", ""))


func _find_loaded_tree() -> Tree:
	if _tabs == null:
		return null
	for child in _tabs.get_children():
		if child is Tree and child.has_meta("loaded_tree"):
			return child
	return null


func _refresh_browse() -> void:
	if _browse_tree == null or not Engine.has_singleton("VML"):
		return
	_browse_tree.clear()
	var root := _browse_tree.create_item()
	var ns_filter := _browse_filter_ns.text.strip_edges()
	var type_filter := _browse_filter_type.get_item_text(_browse_filter_type.selected)
	var ids := VML.list_ids()
	for ns in ids:
		if not ns_filter.is_empty() and not str(ns).begins_with(ns_filter):
			continue
		for path in ids[ns]:
			var full: String = str(ns) + ":" + str(path)
			var info: Dictionary = VML.get_id_info(full)
			if type_filter != "all":
				var dt: String = info.get("data_type", "")
				var t: String = info.get("type", "")
				if dt != type_filter and t != type_filter:
					continue
			var providers: Dictionary = VML.list_providers(full)
			var best: int = providers.get("best", -1)
			var plist: Array = providers.get("providers", [])
			var item := _browse_tree.create_item(root)
			item.set_text(0, full)
			item.set_text(1, "")
			item.set_text(2, str(plist.size()) + " provider(s)")
			item.set_text(3, "")
			var idx := 0
			for p in plist:
				var row := _browse_tree.create_item(item)
				row.set_text(0, str(p.get("mod_id", "")))
				row.set_text(1, str(p.get("path", "")))
				row.set_text(2, str(p.get("priority", 0)))
				row.set_text(3, "yes" if p.get("explicit", false) else "no")
				if idx == best:
					row.set_custom_color(0, Color(0.25, 0.85, 0.35))
				idx += 1
			item.collapsed = true


func _on_selected() -> void:
	var item := _tree.get_selected()
	_selected_id = item.get_meta("id", "") if item else ""


func _on_loaded_selected() -> void:
	var loaded := _find_loaded_tree()
	if loaded == null:
		return
	var item := loaded.get_selected()
	_selected_id = item.get_text(0) if item else ""


func _reset_type_options() -> void:
	_dlg_type.clear()
	for t in TYPES:
		_dlg_type.add_item(t)
	_dlg_type.add_separator()
	_dlg_type.add_item("custom")


func _on_new() -> void:
	_selected_id = ""
	_dlg.title = "New ID"
	_dlg_id.text = ""
	_dlg_id.editable = true
	_dlg_path.text = ""
	_dlg_desc.text = ""
	_dlg_error.text = ""
	_reset_type_options()
	_dlg_type.select(0)
	_dlg.popup_centered(Vector2(420, 260))


func _on_edit() -> void:
	if _selected_id.is_empty():
		return
	var entry: Dictionary = VML.get_registry_entry(_selected_id)
	if entry.is_empty():
		_status.text = "not a registry entry: " + _selected_id
		return
	_dlg.title = "Edit ID"
	_dlg_id.text = _selected_id
	_dlg_id.editable = false # renaming would duplicate entries
	_dlg_path.text = entry.get("path", "")
	_dlg_desc.text = entry.get("description", "")
	_dlg_error.text = ""
	_reset_type_options()
	var t: String = entry.get("type", "")
	var idx := TYPES.find(t)
	if idx >= 0:
		_dlg_type.select(idx)
	elif t.is_empty():
		_dlg_type.select(0)
	else:
		# Keep real custom types: add the actual value to the dropdown.
		_dlg_type.add_item(t)
		_dlg_type.select(_dlg_type.get_item_count() - 1)
	_dlg.popup_centered(Vector2(420, 260))


func _on_delete() -> void:
	if _selected_id.is_empty() or not Engine.has_singleton("VML"):
		return
	if VML.remove_registry_entry(_selected_id):
		print("VML: removed registry entry: ", _selected_id)
		_selected_id = ""
		refresh()
	else:
		_status.text = "not a registry entry: " + _selected_id


func _on_save() -> void:
	if not Engine.has_singleton("VML"):
		return
	# Project-level path (res://vml/registry.json by default) so entries are
	# git-committable; falls back to user:// when res:// is read-only.
	var err := VML.save_registry()
	_status.text = "saved: %s" % error_string(err)
	print("VML: save_registry -> ", err)


func _on_reload() -> void:
	if not Engine.has_singleton("VML"):
		return
	VML.load_registry()
	refresh()


func _on_browse() -> void:
	var fd := FileDialog.new()
	fd.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	fd.access = FileDialog.ACCESS_RESOURCES
	fd.exclusive = false
	add_child(fd)
	fd.file_selected.connect(_on_path_picked)
	fd.canceled.connect(func(): fd.queue_free())
	fd.popup_centered_ratio(0.5)


func _on_path_picked(p: String) -> void:
	_dlg_path.text = p
	# queue_free the FileDialog after selection too (cleanup).
	for child in get_children():
		if child is FileDialog:
			child.queue_free()


func _on_dialog_ok() -> void:
	var id := _dlg_id.text.strip_edges()
	var path := _dlg_path.text.strip_edges()
	if id.is_empty() or path.is_empty():
		_dlg_error.text = "ID and Path are required"
		_dlg.popup_centered(Vector2(420, 260))
		return
	if not _id_is_valid(id):
		_dlg_error.text = "invalid id (namespace:path, dotted, lower-case a-z0-9_-.)"
		_dlg.popup_centered(Vector2(420, 260))
		return
	var t: String = _dlg_type.get_item_text(_dlg_type.selected)
	if t == "custom":
		t = ""
	if VML.set_registry_entry(id, path, t, _dlg_desc.text.strip_edges()):
		print("VML: registry entry set: ", id, " -> ", path)
		_selected_id = id
		refresh()
	else:
		_dlg_error.text = "failed to set registry entry"
		_dlg.popup_centered(Vector2(420, 260))


func _id_is_valid(id: String) -> bool:
	var parts := id.split(":", true, 1)
	if parts.size() != 2 or parts[0].is_empty() or parts[1].is_empty():
		return false
	var re := RegEx.new()
	re.compile("^[a-z0-9_]+$")
	if re.search(parts[0]) == null:
		return false
	re.compile("^[a-zA-Z0-9_\\-\\.]+$")
	return re.search(parts[1]) != null


# --- ID placeholders (B6) ---------------------------------------------------

func _build_placeholder_dialog() -> void:
	_ph_dlg = ConfirmationDialog.new()
	_ph_dlg.ok_button_text = "Create"
	_ph_dlg.cancel_button_text = "Cancel"
	var form := GridContainer.new()
	form.columns = 2
	_ph_dlg.add_child(form)
	form.add_child(_lbl("ID"))
	_ph_id = LineEdit.new()
	_ph_id.placeholder_text = "mygame:mainmenu.bg"
	form.add_child(_ph_id)
	form.add_child(_lbl("Type"))
	_ph_type = OptionButton.new()
	for t in TYPES:
		_ph_type.add_item(t)
	_ph_type.select(0)
	_ph_type.item_selected.connect(func(_i: int): _on_ph_type_changed())
	form.add_child(_ph_type)
	form.add_child(_lbl("Default"))
	_ph_value_row = HBoxContainer.new()
	_ph_value = LineEdit.new()
	_ph_value.placeholder_text = "res://assets/... or user://..."
	_ph_value.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_ph_value_row.add_child(_ph_value)
	var browse := Button.new()
	browse.text = "Browse"
	browse.pressed.connect(_on_ph_browse)
	_ph_value_row.add_child(browse)
	form.add_child(_ph_value_row)
	_ph_value_text = TextEdit.new()
	_ph_value_text.custom_minimum_size = Vector2(0, 64)
	_ph_value_text.placeholder_text = "constant value (JSON object/array, number, string)"
	form.add_child(_ph_value_text)
	form.add_child(_lbl("Desc"))
	_ph_desc = LineEdit.new()
	_ph_desc.placeholder_text = "what does this id mean? (shown in the editor)"
	form.add_child(_ph_desc)
	_ph_error = Label.new()
	_ph_error.add_theme_color_override("font_color", Color(1, 0.3, 0.3))
	_ph_error.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_ph_error.custom_minimum_size = Vector2(400, 0)
	_ph_dlg.add_child(_ph_error)
	add_child(_ph_dlg)
	_ph_dlg.confirmed.connect(_on_ph_ok)


func _on_new_placeholder() -> void:
	_ph_dlg.title = "New Placeholder"
	_ph_id.text = ""
	_ph_desc.text = ""
	_ph_value.text = ""
	_ph_value_text.text = ""
	_ph_error.text = ""
	_ph_type.select(0)
	_on_ph_type_changed()
	_ph_dlg.popup_centered(Vector2(460, 320))


func _on_placeholder_selected() -> void:
	var item := _ph_tree.get_selected()
	_selected_id = item.get_meta("id", "") if item else ""


func _on_ph_type_changed() -> void:
	var t := _ph_type.get_item_text(_ph_type.selected)
	var is_data := t == "data" or t == "value"
	_ph_value_row.visible = not is_data
	_ph_value_text.visible = is_data


func _on_ph_browse() -> void:
	var fd := FileDialog.new()
	fd.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	fd.access = FileDialog.ACCESS_RESOURCES
	fd.exclusive = false
	add_child(fd)
	fd.file_selected.connect(func(p: String):
		_ph_value.text = p
		fd.queue_free())
	fd.canceled.connect(func(): fd.queue_free())
	fd.popup_centered_ratio(0.5)


func _on_ph_ok() -> void:
	var id := _ph_id.text.strip_edges()
	if id.is_empty():
		_ph_error.text = "ID is required"
		_ph_dlg.popup_centered(Vector2(460, 320))
		return
	if not _id_is_valid(id):
		_ph_error.text = "invalid id (namespace:path, dotted, lower-case a-z0-9_-.)"
		_ph_dlg.popup_centered(Vector2(460, 320))
		return
	var t := _ph_type.get_item_text(_ph_type.selected)
	var is_data := t == "data" or t == "value"
	var default_val: Variant
	if is_data:
		var raw := _ph_value_text.text
		var parsed = JSON.parse_string(raw)
		default_val = parsed if parsed != null else raw
	else:
		var path := _ph_value.text.strip_edges()
		if path.is_empty():
			_ph_error.text = "default resource path is required"
			_ph_dlg.popup_centered(Vector2(460, 320))
			return
		default_val = path
	if VML.set_placeholder(id, t, default_val, _ph_desc.text.strip_edges()):
		var err := VML.save_registry()
		print("VML: placeholder set: ", id, " -> ", str(default_val), " (save ", err, ")")
		_selected_id = id
		refresh()
	else:
		_ph_error.text = "failed to set placeholder"
		_ph_dlg.popup_centered(Vector2(460, 320))


func _refresh_placeholders() -> void:
	if _ph_tree == null or not Engine.has_singleton("VML"):
		return
	_ph_tree.clear()
	var root := _ph_tree.create_item()
	for id in VML.get_placeholder_ids():
		var entry: Dictionary = VML.get_registry_entry(id)
		var item := _ph_tree.create_item(root)
		item.set_text(0, id)
		item.set_text(1, entry.get("type", ""))
		var default_val: Variant
		if entry.has("value"):
			default_val = entry["value"]
		else:
			default_val = entry.get("path", "")
		item.set_text(2, str(default_val))
		item.set_text(3, entry.get("description", ""))
		item.set_meta("id", id)
