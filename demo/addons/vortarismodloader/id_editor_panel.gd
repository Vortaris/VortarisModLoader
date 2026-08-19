@tool
extends Control
## VML ID editor — right dock, next to the Inspector.
##
## Registry tab edits the persisted id→resource route table. Registry routes and
## ID placeholders are **unified**: a placeholder is just a registry entry with a
## default value (a resource path, or a constant for data types). The Loaded tab
## browses every id currently indexed; Browse groups ids by namespace/provider.
## New/Edit use a single popup: id + type + default value + description — leaving
## the default empty is a pure route (the default *is* the path for resource ids).

const TYPES := ["data", "scene", "script", "image", "audio", "font", "resource"]

const ResizableTree = preload("resizable_tree.gd")
const TreeCopy = preload("tree_copy.gd")

var _tree: Tree
var _tabs: TabContainer
var _status: Label
var _browse_tree: Tree
var _browse_filter_ns: LineEdit
var _browse_filter_type: OptionButton
var _dlg: ConfirmationDialog
var _dlg_error: Label
var _dlg_id: LineEdit
var _dlg_type: OptionButton
var _dlg_value: LineEdit # resource-path default
var _dlg_value_row: HBoxContainer
var _dlg_value_text: TextEdit # data-constant default
var _dlg_desc: LineEdit
var _selected_id := ""
var _edit_original_id := "" # set while the Edit/Remap dialog is open ("" = New)


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

	_tabs = TabContainer.new()
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(_tabs)

	_tree = ResizableTree.new()
	_setup_columns(_tree, ["ID", "Path / Default", "Type", "Desc"], [130, 180, 60, 80])
	_tree.name = "Registry"
	_tree.item_selected.connect(_on_selected)
	_tree.set_meta("vml_copy_helper", TreeCopy.new(_tree)) # issue #3
	_tabs.add_child(_tree)

	var loaded := ResizableTree.new()
	_setup_columns(loaded, ["ID", "Path", "Provider", "Type"], [130, 180, 70, 60])
	loaded.name = "Loaded"
	loaded.set_meta("vml_copy_helper", TreeCopy.new(loaded)) # issue #3
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
	filter_row.add_child(_lbl("namespace:"))
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
	_browse_tree = ResizableTree.new()
	_setup_columns(_browse_tree, ["ID / Provider", "Path", "Priority", "Explicit"], [150, 220, 60, 60])
	_browse_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_browse_tree.set_meta("vml_copy_helper", TreeCopy.new(_browse_tree)) # issue #3
	browse_vbox.add_child(_browse_tree)

	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(_status)

	_build_dialog()

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
	# Explicit size (not just min_size): first-open must not auto-fill the screen.
	_dlg.size = Vector2i(460, 420)
	_dlg.min_size = Vector2i(460, 420)
	var form := GridContainer.new()
	form.columns = 2
	_dlg.add_child(form)

	form.add_child(_lbl("ID"))
	_dlg_id = LineEdit.new()
	_dlg_id.placeholder_text = "mygame:mainmenu.bg"
	_dlg_id.custom_minimum_size = Vector2(0, 30)
	_dlg_id.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	form.add_child(_dlg_id)

	form.add_child(_lbl("Type"))
	_dlg_type = OptionButton.new()
	_dlg_type.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_dlg_type.item_selected.connect(func(_i: int): _on_type_changed())
	form.add_child(_dlg_type)

	form.add_child(_lbl("Default"))
	# The Default value column hosts the resource-path row and the data-constant
	# TextEdit; exactly one is visible depending on the chosen type. Grouping them
	# in one column keeps the GridContainer labels/inputs aligned (G3/G5).
	var default_col := VBoxContainer.new()
	default_col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	# Resource-path default (resource / scene / script / image / audio / font types).
	_dlg_value_row = HBoxContainer.new()
	_dlg_value_row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_dlg_value = LineEdit.new()
	_dlg_value.placeholder_text = "res://assets/... or user://..."
	_dlg_value.custom_minimum_size = Vector2(0, 30)
	_dlg_value.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_dlg_value_row.add_child(_dlg_value)
	var browse := Button.new()
	browse.text = "Browse"
	browse.pressed.connect(_on_browse)
	_dlg_value_row.add_child(browse)
	default_col.add_child(_dlg_value_row)
	# Data-constant default (data type): JSON value, or a res://data/... path.
	_dlg_value_text = TextEdit.new()
	_dlg_value_text.custom_minimum_size = Vector2(0, 64)
	_dlg_value_text.placeholder_text = "constant (JSON), or a res://data/... path for a route"
	default_col.add_child(_dlg_value_text)
	form.add_child(default_col)

	form.add_child(_lbl("Desc"))
	_dlg_desc = LineEdit.new()
	_dlg_desc.placeholder_text = "what does this id mean? (shown in the editor)"
	_dlg_desc.custom_minimum_size = Vector2(0, 30)
	_dlg_desc.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	form.add_child(_dlg_desc)

	_dlg_error = Label.new()
	_dlg_error.add_theme_color_override("font_color", Color(1, 0.3, 0.3))
	_dlg_error.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_dlg_error.custom_minimum_size = Vector2(400, 0)
	_dlg.add_child(_dlg_error)
	add_child(_dlg)
	_dlg.confirmed.connect(_on_dialog_ok)


func _lbl(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.custom_minimum_size = Vector2(64, 0)
	l.size_flags_vertical = Control.SIZE_SHRINK_CENTER
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
		var path: String = e.get("path", "")
		# Placeholders (placeholder=true) always read as [ph]; the older branch first
		# matched a data placeholder's empty path against "value", so it showed
		# "[value] {...}" instead of "[ph]" (L1).
		if e.get("placeholder", false):
			if path.is_empty() and e.has("value"):
				path = "[ph] value=%s" % str(e["value"])
			else:
				path = "[ph] " + path
		elif path.is_empty() and e.has("value"):
			path = "[value] " + str(e["value"])
		item.set_text(1, path)
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


func _on_type_changed() -> void:
	var t := _dlg_type.get_item_text(_dlg_type.selected)
	var is_data := t == "data" or t == "value"
	_dlg_value_row.visible = not is_data
	_dlg_value_text.visible = is_data


func _on_new() -> void:
	_selected_id = ""
	_edit_original_id = ""
	_dlg.title = "New ID"
	_dlg_id.text = ""
	_dlg_id.editable = true
	_dlg_value.text = ""
	_dlg_value_text.text = ""
	_dlg_desc.text = ""
	_dlg_error.text = ""
	_reset_type_options()
	_dlg_type.select(0)
	_on_type_changed()
	_dlg.popup_centered(Vector2(460, 420))


func _on_edit() -> void:
	if _selected_id.is_empty():
		return
	var entry: Dictionary = VML.get_registry_entry(_selected_id)
	if entry.is_empty():
		# Issue #7: auto-registered ids (base/mod scans) are NOT registry
		# entries, so the old code refused them ("not a registry entry"). Open
		# the dialog in create/remap mode instead, prefilled from get_id_info:
		#  - keep the id  -> persist a registry route override for the auto id
		#  - change the id -> register the new id pointing at the same path
		#    (a persistent remap; the auto id itself stays scannable)
		var info: Dictionary = VML.get_id_info(_selected_id)
		_edit_original_id = _selected_id
		_dlg.title = "Remap Auto-Registered ID"
		_dlg_id.text = _selected_id
		_dlg_id.editable = true
		_dlg_value.text = str(info.get("path", ""))
		_dlg_value_text.text = ""
		_dlg_desc.text = ""
		_dlg_error.text = ""
		_reset_type_options()
		var t: String = str(info.get("type", ""))
		var idx := TYPES.find(t)
		if idx >= 0:
			_dlg_type.select(idx)
		else:
			_dlg_type.select(0)
		_on_type_changed()
		_dlg.popup_centered(Vector2(460, 420))
		return
	_edit_original_id = _selected_id
	_dlg.title = "Edit ID"
	_dlg_id.text = _selected_id
	_dlg_id.editable = false # renaming would duplicate entries
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
	# Default: data placeholders keep a constant under "value"; everything else a path.
	if entry.has("value"):
		_dlg_value_text.text = str(entry["value"])
		_dlg_value.text = ""
	else:
		_dlg_value.text = entry.get("path", "")
		_dlg_value_text.text = ""
	_on_type_changed()
	_dlg.popup_centered(Vector2(460, 420))


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
	_dlg_value.text = p
	# queue_free the FileDialog after selection too (cleanup).
	for child in get_children():
		if child is FileDialog:
			child.queue_free()


func _on_dialog_ok() -> void:
	var id := _dlg_id.text.strip_edges()
	if id.is_empty():
		_dlg_error.text = "ID is required"
		_dlg.popup_centered(Vector2(460, 420))
		return
	if not _id_is_valid(id):
		_dlg_error.text = "invalid id (namespace:path, dotted, lower-case a-z0-9_-.)"
		_dlg.popup_centered(Vector2(460, 420))
		return
	var t: String = _dlg_type.get_item_text(_dlg_type.selected)
	# "custom" is kept as-is: set_placeholder treats an empty type as data, so a
	# custom-type resource route must pass a non-data type tag.
	var desc := _dlg_desc.text.strip_edges()
	var is_data := t == "data" or t == "value"
	if is_data:
		var raw := _dlg_value_text.text.strip_edges()
		# A path in the data field is a route (id -> JSON file); anything else is a
		# constant placeholder (a persisted value provider).
		if raw.begins_with("res://") or raw.begins_with("user://"):
			if not VML.set_registry_entry(id, raw, "data", desc):
				_dlg_error.text = "failed to set registry entry"
				_dlg.popup_centered(Vector2(460, 420))
				return
			print("VML: registry entry set: ", id, " -> ", raw)
		else:
			var parsed = JSON.parse_string(raw)
			var default_val: Variant = parsed if parsed != null else raw
			if not VML.set_placeholder(id, "data", default_val, desc):
				_dlg_error.text = "failed to set placeholder"
				_dlg.popup_centered(Vector2(460, 420))
				return
			print("VML: placeholder set: ", id, " -> ", str(default_val))
	else:
		var path := _dlg_value.text.strip_edges()
		if path.is_empty():
			_dlg_error.text = "a default path is required"
			_dlg.popup_centered(Vector2(460, 420))
			return
		# Resource ids: the default value IS the path. Registry routes and resource
		# placeholders are the same provider (base-layer priority 0), unified here.
		if not VML.set_placeholder(id, t, path, desc):
			_dlg_error.text = "failed to set placeholder"
			_dlg.popup_centered(Vector2(460, 420))
			return
		print("VML: placeholder set: ", id, " -> ", path)
	var remapped := not _edit_original_id.is_empty() and id != _edit_original_id
	var remap_from := _edit_original_id
	_selected_id = id
	_edit_original_id = ""
	refresh()
	if remapped:
		# The auto-registered original stays scannable; the NEW id is the
		# persisted registry route users should reference from now on.
		_status.text = "remapped %s -> new registry id %s (auto id stays scannable)" % [remap_from, id]


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
