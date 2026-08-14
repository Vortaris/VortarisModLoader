@tool
extends ConfirmationDialog
## One-click mod skeleton generator. Writes a loadable mod into
## res://mods-unpacked/<id>/ with manifest.json + mod_main.gd + assets/data dirs.

signal mod_created(mod_id: String)

var _id_edit: LineEdit
var _error: Label


func _ready() -> void:
	title = "Create Vortaris Mod"
	ok_button_text = "Create"
	cancel_button_text = "Cancel"

	var vbox := VBoxContainer.new()
	vbox.add_child(_label("Mod id (namespace, ^[a-z0-9_]{1,32}$):"))
	_id_edit = LineEdit.new()
	_id_edit.placeholder_text = "e.g. my_mod"
	_id_edit.custom_minimum_size = Vector2(260, 0)
	vbox.add_child(_id_edit)
	vbox.add_child(_label("Creates: manifest.json, mod_main.gd, assets/<id>/, data/<id>/"))
	_error = Label.new()
	_error.add_theme_color_override("font_color", Color(1, 0.3, 0.3))
	_error.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(_error)
	add_child(vbox)

	confirmed.connect(_on_confirmed)


func _label(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	return l


func _write(path: String, content: String) -> bool:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(content)
	f.close()
	return true


func _on_confirmed() -> void:
	var id := _id_edit.text.strip_edges()
	if id.is_empty():
		_error.text = "mod id is required"
		popup_centered(Vector2(380, 260))
		return
	var re := RegEx.new()
	re.compile("^[a-z0-9_]{1,32}$")
	if re.search(id) == null:
		_error.text = "invalid id (expected ^[a-z0-9_]{1,32}$)"
		popup_centered(Vector2(380, 260))
		return

	var base := "res://mods-unpacked/" + id
	if DirAccess.dir_exists_absolute(base):
		_error.text = "mod already exists: " + base
		popup_centered(Vector2(380, 260))
		return
	if DirAccess.make_dir_recursive_absolute(base + "/assets/" + id) != OK:
		_error.text = "cannot create dirs under res:// (read-only?)"
		popup_centered(Vector2(380, 260))
		return
	DirAccess.make_dir_recursive_absolute(base + "/data/" + id)

	var manifest := {
		"name": id.capitalize().replace("_", " "),
		"namespace": id,
		"version_number": "1.0.0",
		"description": "",
		"extra": {
			"godot": {
				"main_script": "mod_main.gd"
			}
		},
	}
	if not _write(base + "/manifest.json", JSON.stringify(manifest, "  ")) \
			or not _write(base + "/mod_main.gd",
					"extends Node\n\nfunc _init() -> void:\n\t# Register hooks / config here (attributed to this mod automatically).\n\tpass\n"):
		_error.text = "failed to write mod files (res:// read-only?)"
		popup_centered(Vector2(380, 260))
		return

	mod_created.emit(id)
