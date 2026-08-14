@tool
extends Control
## VML ID editor — right dock, next to the Inspector.
##
## Two tabs:
##   Registry — edit the persisted id→resource route table (saved to
##             user://vml/registry.json, auto-loaded at VML.finish_startup()).
##   Loaded   — browse every id currently indexed by the loader and its status.
##
## New / Edit use a popup dialog; the type field is a dropdown.

const TYPES := ["data", "scene", "script", "image", "audio", "font", "resource"]

var _tree: Tree
var _status: Label
var _dlg: ConfirmationDialog
var _dlg_id: LineEdit
var _dlg_path: LineEdit
var _dlg_type: OptionButton
var _dlg_desc: LineEdit
var _selected_id := ""


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

	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(tabs)

	_tree = Tree.new()
	_setup_columns(_tree, ["ID", "Path", "Type", "Desc"], [130, 180, 60, 80])
	_tree.name = "Registry"
	_tree.item_selected.connect(_on_selected)
	tabs.add_child(_tree)

	var loaded := Tree.new()
	_setup_columns(loaded, ["ID", "Path", "Provider", "Type"], [130, 180, 70, 60])
	loaded.name = "Loaded"
	tabs.add_child(loaded)
	loaded.item_selected.connect(_on_loaded_selected)
	loaded.set_meta("loaded_tree", true)

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
		# Only the last column expands; the others are user-resizable.
		tree.set_column_expand(i, i == titles.size() - 1)
		tree.set_column_custom_minimum_width(i, widths[i])


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
	for t in TYPES:
		_dlg_type.add_item(t)
	_dlg_type.add_separator()
	_dlg_type.add_item("custom")
	form.add_child(_dlg_type)
	form.add_child(_lbl("Desc"))
	_dlg_desc = LineEdit.new()
	form.add_child(_dlg_desc)
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
	_refresh_registry()
	_refresh_loaded()


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
	_status.text = "%d registry entries" % reg.size()


func _refresh_loaded() -> void:
	var loaded := _find_loaded_tree()
	if loaded == null:
		return
	loaded.clear()
	var root := loaded.create_item()
	var ids := VML.list_ids()
	var count := 0
	for ns in ids:
		for path in ids[ns]:
			var full: String = str(ns) + ":" + str(path)
			var info: Dictionary = VML.get_id_info(full)
			var item := loaded.create_item(root)
			item.set_text(0, full)
			item.set_text(1, info.get("path", ""))
			item.set_text(2, info.get("provider_mod", ""))
			item.set_text(3, info.get("type", ""))
			count += 1
	_status.text = "%d registry entries · %d ids loaded" % [VML.get_registry().size(), count]


func _find_loaded_tree() -> Tree:
	var tabs := get_child(0)
	for child in tabs.get_children():
		if child is Tree and child.has_meta("loaded_tree"):
			return child
	return null


func _on_selected() -> void:
	var item := _tree.get_selected()
	_selected_id = item.get_meta("id", "") if item else ""


func _on_loaded_selected() -> void:
	var loaded := _find_loaded_tree()
	if loaded == null:
		return
	var item := loaded.get_selected()
	_selected_id = item.get_text(0) if item else ""


func _on_new() -> void:
	_selected_id = ""
	_dlg.title = "New ID"
	_dlg_id.text = ""
	_dlg_path.text = ""
	_dlg_type.select(0)
	_dlg_desc.text = ""
	_dlg.popup_centered(Vector2(420, 240))


func _on_edit() -> void:
	if _selected_id.is_empty():
		return
	_dlg.title = "Edit ID"
	_dlg_id.text = _selected_id
	var entry: Dictionary = VML.get_registry_entry(_selected_id)
	_dlg_path.text = entry.get("path", "")
	var t: String = entry.get("type", "")
	var idx := _dlg_type.get_item_index(TYPES.find(t) if TYPES.has(t) else 0)
	if TYPES.has(t):
		_dlg_type.select(TYPES.find(t))
	else:
		_dlg_type.select(_dlg_type.get_item_count() - 1)  # custom
	_dlg_desc.text = entry.get("description", "")
	_dlg.popup_centered(Vector2(420, 240))


func _on_delete() -> void:
	if _selected_id.is_empty() or not Engine.has_singleton("VML"):
		return
	if VML.remove_registry_entry(_selected_id):
		print("VML: removed registry entry: ", _selected_id)
		_selected_id = ""
		refresh()


func _on_save() -> void:
	if not Engine.has_singleton("VML"):
		return
	var err := VML.save_registry("user://vml/registry.json")
	_status.text = "saved: %s" % err
	print("VML: save_registry -> ", err)


func _on_reload() -> void:
	if not Engine.has_singleton("VML"):
		return
	VML.load_registry("user://vml/registry.json")
	refresh()


func _on_browse() -> void:
	var fd := FileDialog.new()
	fd.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	fd.access = FileDialog.ACCESS_RESOURCES
	add_child(fd)
	fd.file_selected.connect(func(p): _dlg_path.text = p)
	fd.popup_centered_ratio(0.5)


func _on_dialog_ok() -> void:
	var id := _dlg_id.text.strip_edges()
	var path := _dlg_path.text.strip_edges()
	if id.is_empty() or path.is_empty():
		_status.text = "ID and Path are required"
		return
	var t: String = _dlg_type.get_item_text(_dlg_type.selected)
	if t == "custom":
		t = ""
	if VML.set_registry_entry(id, path, t, _dlg_desc.text.strip_edges()):
		print("VML: registry entry set: ", id, " -> ", path)
		_selected_id = id
		refresh()
	else:
		_status.text = "invalid id or path"
