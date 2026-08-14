@tool
extends Control
## VML ID Registry editor — lives in the RIGHT dock, next to the Inspector.
##
## Manages the persisted content registry (id → default resource route). Every
## change is applied live with VML.set_registry_entry() and can be saved to
## user://vml/registry.json; VML.finish_startup() loads it automatically at boot.
## A mod providing the same id (higher priority) overrides the registry route.

var _tree: Tree
var _id_edit: LineEdit
var _path_edit: LineEdit
var _type_edit: LineEdit
var _desc_edit: LineEdit
var _status: Label
var _selected_id := ""


func _ready() -> void:
	name = "VML IDs"

	var vbox := VBoxContainer.new()
	vbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(vbox)

	var hbox := HBoxContainer.new()
	vbox.add_child(hbox)
	_add_btn(hbox, "New", _on_new)
	_add_btn(hbox, "Apply", _on_apply)
	_add_btn(hbox, "Save", _on_save)
	_add_btn(hbox, "Reload", _on_reload)
	_add_btn(hbox, "Delete", _on_delete)

	_tree = Tree.new()
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tree.columns = 4
	_tree.set_column_titles_visible(true)
	_tree.set_column_title(0, "ID")
	_tree.set_column_title(1, "Path")
	_tree.set_column_title(2, "Type")
	_tree.set_column_title(3, "Desc")
	_tree.set_column_expand(0, false)
	_tree.set_column_custom_minimum_width(0, 130)
	_tree.item_selected.connect(_on_selected)
	vbox.add_child(_tree)

	var form := GridContainer.new()
	form.columns = 2
	vbox.add_child(form)
	form.add_child(_lbl("ID"))
	_id_edit = LineEdit.new()
	_id_edit.placeholder_text = "mygame:mainmenu.bg"
	form.add_child(_id_edit)
	form.add_child(_lbl("Path"))
	_path_edit = LineEdit.new()
	_path_edit.placeholder_text = "res://assets/... or user://..."
	form.add_child(_path_edit)
	form.add_child(_lbl("Type"))
	_type_edit = LineEdit.new()
	_type_edit.placeholder_text = "image / data / scene / ..."
	form.add_child(_type_edit)
	form.add_child(_lbl("Desc"))
	_desc_edit = LineEdit.new()
	form.add_child(_desc_edit)

	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(_status)

	refresh()


func _lbl(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.custom_minimum_size = Vector2(46, 0)
	return l


func _add_btn(parent: Control, text: String, callable: Callable) -> void:
	var b := Button.new()
	b.text = text
	b.pressed.connect(callable)
	parent.add_child(b)


func refresh() -> void:
	_tree.clear()
	if not Engine.has_singleton("VML"):
		_status.text = "VML not loaded"
		return
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
	_status.text = "%d registry entries (saved to user://vml/registry.json)" % reg.size()


func _on_selected() -> void:
	var item := _tree.get_selected()
	if item == null:
		return
	_selected_id = item.get_meta("id", "")
	_id_edit.text = _selected_id
	_path_edit.text = item.get_text(1)
	_type_edit.text = item.get_text(2)
	_desc_edit.text = item.get_text(3)


func _on_new() -> void:
	_selected_id = ""
	_id_edit.text = ""
	_path_edit.text = ""
	_type_edit.text = ""
	_desc_edit.text = ""
	_id_edit.grab_focus()


func _on_apply() -> void:
	if not Engine.has_singleton("VML"):
		return
	var id := _id_edit.text.strip_edges()
	var path := _path_edit.text.strip_edges()
	if id.is_empty() or path.is_empty():
		_status.text = "ID and Path are required"
		return
	if VML.set_registry_entry(id, path, _type_edit.text.strip_edges(), _desc_edit.text.strip_edges()):
		print("VML: registry entry set: ", id, " -> ", path)
		refresh()
	else:
		_status.text = "invalid id or path"


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


func _on_delete() -> void:
	if not Engine.has_singleton("VML") or _selected_id.is_empty():
		return
	if VML.remove_registry_entry(_selected_id):
		print("VML: removed registry entry: ", _selected_id)
		_selected_id = ""
		refresh()
