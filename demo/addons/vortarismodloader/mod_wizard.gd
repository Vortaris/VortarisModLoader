@tool
extends ConfirmationDialog
## One-click mod skeleton generator. Writes a loadable mod into
## res://mods-unpacked/<id>/ with manifest.json + mod_main.gd + assets/data dirs.

signal mod_created(mod_id: String)

var _id_edit: LineEdit


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
	add_child(vbox)

	confirmed.connect(_on_confirmed)


func _label(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	return l


func _on_confirmed() -> void:
	var id := _id_edit.text.strip_edges()
	if id.is_empty():
		return
	# Reuse the same validation rules as the manifest parser (lowercase a-z0-9_).
	var re := RegEx.new()
	re.compile("^[a-z0-9_]{1,32}$")
	if re.search(id) == null:
		_id_edit.text = ""
		return

	var base := "res://mods-unpacked/" + id
	DirAccess.make_dir_recursive_absolute(base + "/assets/" + id)
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
	FileAccess.open(base + "/manifest.json", FileAccess.WRITE).store_string(
			JSON.stringify(manifest, "  "))
	FileAccess.open(base + "/mod_main.gd", FileAccess.WRITE).store_string(
			"extends Node\n\nfunc _init() -> void:\n\t# Register hooks / config here (attributed to this mod automatically).\n\tpass\n")

	mod_created.emit(id)
