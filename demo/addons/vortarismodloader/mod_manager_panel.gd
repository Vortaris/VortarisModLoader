@tool
extends Control
## VML mod management dock. Three tabs: Mods (list/state/toggle/install),
## IDs (browse the content registry) and Hooks (registered hook points/handlers).
## Talks only to the VML engine singleton. Interface language is English.

const ModWizard = preload("mod_wizard.gd")

var _mod_tree: Tree
var _id_tree: Tree
var _hook_tree: Tree
var _status: Label
var _wizard: ConfirmationDialog
var _last_mod_id := ""


func _ready() -> void:
	name = "VML Mods"

	var vbox := VBoxContainer.new()
	vbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(vbox)

	var hbox := HBoxContainer.new()
	vbox.add_child(hbox)
	_add_btn(hbox, "Install Zip", _on_install_zip)
	_add_btn(hbox, "Create Mod", _on_create_mod)
	_add_btn(hbox, "Rescan", _on_rescan)
	_add_btn(hbox, "Reload DB", _on_reload)
	_add_btn(hbox, "Toggle", _on_toggle_selected)

	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(tabs)

	_mod_tree = _make_tree(["Mod", "Version", "State"])
	tabs.add_child(_tab("Mods", _mod_tree))
	_id_tree = _make_tree(["Namespace", "Path", "Source"])
	tabs.add_child(_tab("IDs", _id_tree))
	_hook_tree = _make_tree(["Hook", "Type", "Detail"])
	tabs.add_child(_tab("Hooks", _hook_tree))

	_status = Label.new()
	vbox.add_child(_status)

	_wizard = ModWizard.new()
	add_child(_wizard)
	_wizard.mod_created.connect(_on_mod_created)

	refresh()


func _make_tree(columns: Array) -> Tree:
	var t := Tree.new()
	t.columns = columns.size()
	t.set_column_titles_visible(true)
	for i in columns.size():
		t.set_column_title(i, columns[i])
		# Keep column widths user-adjustable (draggable separators) and set a sane default.
		t.set_column_expand(i, i == 0)
		t.set_column_custom_minimum_width(i, 90)
	return t


func _tab(title: String, content: Control) -> Control:
	content.name = title
	return content


func _add_btn(parent: Control, text: String, callable: Callable) -> void:
	var b := Button.new()
	b.text = text
	b.pressed.connect(callable)
	parent.add_child(b)


func refresh() -> void:
	if not Engine.has_singleton("VML"):
		_status.text = "VML engine singleton not loaded"
		return
	_refresh_mods()
	_refresh_ids()
	_refresh_hooks()
	var total := 0
	for ns in VML.list_ids():
		total += VML.list_ids()[ns].size()
	_status.text = "%d mods, %d ids, db=%s" % [VML.get_mod_ids().size(), total, VML.get_database_mode()]


func _refresh_mods() -> void:
	_mod_tree.clear()
	var root := _mod_tree.create_item()
	var to_select: TreeItem = null
	for mod_id in VML.get_mod_ids():
		var item := _mod_tree.create_item(root)
		item.set_text(0, mod_id)
		item.set_text(1, VML.get_mod_version(mod_id))
		var state := "enabled" if VML.is_mod_enabled(mod_id) else "disabled"
		if VML.is_mod_loaded(mod_id):
			state += " +loaded"
		var errs := VML.get_mod_errors(mod_id)
		if errs.size() > 0:
			state += " (!)"
		item.set_text(2, state)
		item.set_meta("mod_id", mod_id)
		if mod_id == _last_mod_id:
			to_select = item
	if to_select != null:
		_mod_tree.set_selected(to_select, 0)
		_mod_tree.scroll_to_item(to_select)


func _refresh_ids() -> void:
	_id_tree.clear()
	var root := _id_tree.create_item()
	for ns in VML.list_ids():
		for path in VML.list_ids()[ns]:
			var item := _id_tree.create_item(root)
			item.set_text(0, ns)
			item.set_text(1, path)
			item.set_text(2, VML.resolve(ns + ":" + path))


func _refresh_hooks() -> void:
	_hook_tree.clear()
	var root := _hook_tree.create_item()
	for hook_id in VML.list_hooks():
		var info: Dictionary = VML.list_hooks()[hook_id]
		var item := _hook_tree.create_item(root)
		item.set_text(0, hook_id)
		item.set_text(1, "registered")
		item.set_text(2, "%d handler(s) by %s" % [info["count"], str(info["mods"])])
	for point_id in VML.list_hook_points():
		var info: Dictionary = VML.list_hook_points()[point_id]
		var item := _hook_tree.create_item(root)
		item.set_text(0, point_id)
		item.set_text(1, "declared")
		item.set_text(2, info["description"])


func _selected_mod_id() -> String:
	var item := _mod_tree.get_selected()
	return item.get_meta("mod_id", "") if item else _last_mod_id


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
	print("VML: install_mod_from_zip(%s) -> %s" % [path, err])
	refresh()


func _on_create_mod() -> void:
	_wizard.popup_centered(Vector2(380, 260))


func _on_mod_created(mod_id: String) -> void:
	print("VML: mod created: ", mod_id)
	if Engine.has_singleton("VML"):
		VML.rescan()
	refresh()


func _on_rescan() -> void:
	if not Engine.has_singleton("VML"):
		return
	VML.rescan()
	print("VML: rescan done (", VML.get_mod_ids().size(), " mods)")
	refresh()


func _on_reload() -> void:
	if not Engine.has_singleton("VML"):
		return
	VML.reload_database()
	print("VML: database reloaded (mode ", VML.get_database_mode(), ")")
	refresh()


func _on_toggle_selected() -> void:
	var id := _selected_mod_id()
	if id.is_empty() or not Engine.has_singleton("VML"):
		return
	_last_mod_id = id
	if VML.is_mod_enabled(id):
		var ok := VML.disable_mod(id)
		print("VML: disable_mod(", id, ") -> ", ok)
	else:
		var ok := VML.enable_mod(id)
		print("VML: enable_mod(", id, ") -> ", ok)
	refresh()
