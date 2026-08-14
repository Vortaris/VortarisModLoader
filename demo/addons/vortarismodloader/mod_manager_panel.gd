@tool
extends Control
## VML mod management dock. Lists discovered mods with their runtime state,
## exposes enable/disable, zip install/uninstall, rescan, hot reload and the
## skeleton wizard. Talks only to the VML engine singleton.

const ModWizard = preload("mod_wizard.gd")

var _tree: Tree
var _status: Label
var _wizard: ConfirmationDialog


func _ready() -> void:
	var vbox := VBoxContainer.new()
	vbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(vbox)

	var hbox := HBoxContainer.new()
	vbox.add_child(hbox)
	_add_btn(hbox, "Install Zip", _on_install_zip)
	_add_btn(hbox, "Create Mod", _on_create_mod)
	_add_btn(hbox, "Rescan", _on_rescan)
	_add_btn(hbox, "Toggle", _on_toggle_selected)

	_tree = Tree.new()
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tree.columns = 3
	_tree.set_column_titles_visible(true)
	_tree.set_column_title(0, "Mod")
	_tree.set_column_title(1, "Version")
	_tree.set_column_title(2, "State")
	vbox.add_child(_tree)

	_status = Label.new()
	_status.text = ""
	vbox.add_child(_status)

	_wizard = ModWizard.new()
	add_child(_wizard)
	_wizard.mod_created.connect(_on_mod_created)

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
	_tree.clear()
	var root := _tree.create_item()
	var mod_ids := VML.get_mod_ids()
	for mod_id in mod_ids:
		var item := _tree.create_item(root)
		item.set_text(0, mod_id)
		item.set_text(1, "1.0.0")
		var state := "enabled" if VML.is_mod_enabled(mod_id) else "disabled"
		if VML.is_mod_loaded(mod_id):
			state += " +loaded"
		var errs := VML.get_mod_errors(mod_id)
		if errs.size() > 0:
			state += " (!)"
		item.set_text(2, state)
		item.set_meta("mod_id", mod_id)
	_status.text = "%d mods, %d ids indexed, db=%s" % [mod_ids.size(), VML.list_ids().size(), VML.get_database_mode()]


func _selected_mod_id() -> String:
	var item := _tree.get_selected()
	return item.get_meta("mod_id", "") if item else ""


func _on_install_zip() -> void:
	if not Engine.has_singleton("VML"):
		return
	var fd := FileDialog.new()
	fd.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	fd.access = FileDialog.ACCESS_FILESYSTEM
	fd.add_filter("*.zip", "Vortaris Mod (zip)")
	add_child(fd)
	fd.file_selected.connect(_on_zip_selected)
	fd.popup_centered_ratio(0.5)


func _on_zip_selected(path: String) -> void:
	if not Engine.has_singleton("VML"):
		return
	var err := VML.install_mod_from_zip(path)
	_status.text = "install: %s" % err
	refresh()


func _on_create_mod() -> void:
	_wizard.popup_centered_ratio(0.4)


func _on_mod_created(mod_id: String) -> void:
	if Engine.has_singleton("VML"):
		VML.rescan()
	refresh()


func _on_rescan() -> void:
	if not Engine.has_singleton("VML"):
		return
	VML.rescan()
	refresh()


func _on_toggle_selected() -> void:
	var id := _selected_mod_id()
	if id.is_empty() or not Engine.has_singleton("VML"):
		return
	if VML.is_mod_enabled(id):
		VML.disable_mod(id)
	else:
		VML.enable_mod(id)
	refresh()
