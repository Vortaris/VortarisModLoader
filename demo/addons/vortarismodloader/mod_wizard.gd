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
	# Fixed, compact dialog — never auto-fills the screen on first open. The size
	# must be set explicitly (not just min_size): Godot's dialog size/state restore
	# otherwise gives the FIRST open a large default and the second open the small one.
	size = Vector2i(400, 200)
	min_size = Vector2i(400, 200)

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


## Opens the dialog at a fixed compact size. The extra process_frame await lets
## the layout settle so the first open (which otherwise restores a large default
## size) is correct from the start.
func show_create() -> void:
	size = Vector2i(400, 200)
	min_size = Vector2i(400, 200)
	await get_tree().process_frame
	popup_centered(Vector2i(400, 200))


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
		popup_centered(Vector2(400, 200))
		return
	var re := RegEx.new()
	re.compile("^[a-z0-9_]{1,32}$")
	if re.search(id) == null:
		_error.text = "invalid id (expected ^[a-z0-9_]{1,32}$)"
		popup_centered(Vector2(400, 200))
		return

	var base := "res://mods-unpacked/" + id
	if DirAccess.dir_exists_absolute(base):
		_error.text = "mod already exists: " + base
		popup_centered(Vector2(400, 200))
		return
	if DirAccess.make_dir_recursive_absolute(base + "/assets/" + id) != OK:
		_error.text = "cannot create dirs under res:// (read-only?)"
		popup_centered(Vector2(400, 200))
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
	# A sample data file so the new mod is immediately addressable by id. Its
	# `"id"` matches the path-inferred id (data/<ns>/<file>.json -> <ns>:<file>).
	var sample := {
		"id": "%s:sample" % id,
		"name": "Sample",
		"description": "Example data file for the %s mod" % id,
	}
	if not _write(base + "/manifest.json", JSON.stringify(manifest, "  ")) \
			or not _write(base + "/mod_main.gd",
					"extends Node\n\nfunc _init() -> void:\n\t# Register hooks / config here (attributed to this mod automatically).\n\tpass\n") \
			or not _write(base + "/data/%s/sample.json" % id, JSON.stringify(sample, "  ")):
		_error.text = "failed to write mod files (res:// read-only?)"
		popup_centered(Vector2(400, 200))
		return

	mod_created.emit(id)
