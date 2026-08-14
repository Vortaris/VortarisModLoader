@tool
extends Control
## VML mod management dock — lives in the LEFT-BOTTOM dock, next to Import.
## Tabs: Mods (list/state/toggle/install), Hooks (hook points/handlers).
## The ID registry browser lives in the "VML IDs" panel (right dock).

const ModWizard = preload("mod_wizard.gd")

var _mod_tree: Tree
var _hook_tree: Tree
var _status: Label
var _wizard: ConfirmationDialog
var _last_mod_id := ""
var _config_dlg: ConfirmationDialog
var _config_text: TextEdit
var _config_status: Label
var _config_error: Label
var _config_mod_id := ""
var _confirm_dlg: ConfirmationDialog
var _pending_disable_id := ""


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
	_add_btn(hbox, "Config", _on_config)

	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(tabs)

	_mod_tree = Tree.new()
	_setup_columns(_mod_tree, ["Mod", "Version", "State"], [110, 60, 90])
	_mod_tree.name = "Mods"
	_mod_tree.item_selected.connect(_on_mod_selected)
	tabs.add_child(_mod_tree)

	_hook_tree = Tree.new()
	_setup_columns(_hook_tree, ["Hook", "Type", "Detail"], [130, 70, 200])
	_hook_tree.name = "Hooks"
	tabs.add_child(_hook_tree)

	_status = Label.new()
	_status.text = ""
	vbox.add_child(_status)

	_wizard = ModWizard.new()
	add_child(_wizard)
	_wizard.mod_created.connect(_on_mod_created)

	_build_config_dialog()
	_build_confirm_dialog()

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


func refresh() -> void:
	if not Engine.has_singleton("VML"):
		_status.text = "VML engine singleton not loaded"
		return
	_refresh_mods()
	_refresh_hooks()
	_status.text = "%d mods, db=%s" % [VML.get_mod_ids().size(), VML.get_database_mode()]


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


func _refresh_hooks() -> void:
	_hook_tree.clear()
	var root := _hook_tree.create_item()
	var hooks: Dictionary = VML.list_hooks()
	for hook_id in hooks:
		var info: Dictionary = hooks[hook_id]
		var item := _hook_tree.create_item(root)
		item.set_text(0, hook_id)
		item.set_text(1, "registered")
		item.set_text(2, "%d handler(s) by %s" % [info.get("count", 0), str(info.get("mods", []))])
	var points: Dictionary = VML.list_hook_points()
	for point_id in points:
		var info: Dictionary = points[point_id]
		var item := _hook_tree.create_item(root)
		item.set_text(0, point_id)
		item.set_text(1, "declared")
		item.set_text(2, info.get("description", ""))


func _on_mod_selected() -> void:
	var item := _mod_tree.get_selected()
	_last_mod_id = item.get_meta("mod_id", "") if item else ""


func _selected_mod_id() -> String:
	var item := _mod_tree.get_selected()
	return item.get_meta("mod_id", "") if item else _last_mod_id


func _on_install_zip() -> void:
	if not Engine.has_singleton("VML"):
		return
	var fd := FileDialog.new()
	fd.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	fd.access = FileDialog.ACCESS_FILESYSTEM
	fd.exclusive = false # avoid "exclusive child window" clash with other dialogs
	fd.add_filter("*.zip", "Vortaris Mod (zip)")
	add_child(fd)
	fd.file_selected.connect(func(p: String):
		_on_zip_selected(p)
		fd.queue_free())
	fd.canceled.connect(func(): fd.queue_free())
	fd.popup_centered_ratio(0.5)


func _on_zip_selected(path: String) -> void:
	if not Engine.has_singleton("VML"):
		return
	var err := VML.install_mod_from_zip(path)
	_status.text = "install: %s" % error_string(err)
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


func _build_confirm_dialog() -> void:
	_confirm_dlg = ConfirmationDialog.new()
	_confirm_dlg.ok_button_text = "Disable"
	_confirm_dlg.cancel_button_text = "Cancel"
	add_child(_confirm_dlg)
	_confirm_dlg.confirmed.connect(_on_confirm_disable)


func _on_toggle_selected() -> void:
	var id := _selected_mod_id()
	if id.is_empty():
		_status.text = "select a mod first"
		return
	if not Engine.has_singleton("VML"):
		return
	_last_mod_id = id
	if VML.is_mod_enabled(id):
		# Dependent mods would be cascade-disabled too — ask first.
		var dependents := VML.get_mod_dependents(id)
		if dependents.size() > 0:
			_pending_disable_id = id
			_confirm_dlg.dialog_text = "Disable '%s'?\nThis will also disable:\n%s" % [id, "\n".join(dependents)]
			_confirm_dlg.popup_centered(Vector2(440, 260))
			return
		_do_disable(id)
	else:
		_do_enable(id)


func _on_confirm_disable() -> void:
	_do_disable(_pending_disable_id)
	_pending_disable_id = ""


func _do_disable(id: String) -> void:
	var ok := VML.disable_mod(id)
	print("VML: disable_mod(", id, ") -> ", ok)
	if ok:
		_status.text = "disabled %s" % id
	else:
		_status.text = "cannot disable %s — %s" % [id, " · ".join(VML.get_mod_errors(id))]
	refresh()


func _do_enable(id: String) -> void:
	var ok := VML.enable_mod(id)
	print("VML: enable_mod(", id, ") -> ", ok)
	if ok:
		_status.text = "enabled %s" % id
	else:
		_status.text = "cannot enable %s — %s" % [id, " · ".join(VML.get_mod_errors(id))]
	refresh()


func _build_config_dialog() -> void:
	_config_dlg = ConfirmationDialog.new()
	_config_dlg.title = "Mod Config"
	_config_dlg.ok_button_text = "Save"
	_config_dlg.cancel_button_text = "Cancel"
	var vbox := VBoxContainer.new()
	vbox.custom_minimum_size = Vector2(440, 320)
	_config_dlg.add_child(vbox)
	_config_status = Label.new()
	vbox.add_child(_config_status)
	_config_text = TextEdit.new()
	_config_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(_config_text)
	_config_error = Label.new()
	_config_error.add_theme_color_override("font_color", Color(1, 0.3, 0.3))
	_config_error.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(_config_error)
	add_child(_config_dlg)
	_config_dlg.confirmed.connect(_on_config_save)


func _on_config() -> void:
	var id := _selected_mod_id()
	if id.is_empty():
		_status.text = "select a mod first"
		return
	if not Engine.has_singleton("VML"):
		return
	_config_mod_id = id
	_config_text.text = JSON.stringify(VML.get_config(id), "  ")
	_config_error.text = ""
	var schema: Dictionary = VML.get_config_schema(id)
	_config_status.text = "mod: %s  ·  config_schema: %s" % [id, "declared" if not schema.is_empty() else "none"]
	_config_dlg.popup_centered(Vector2(480, 380))


func _on_config_save() -> void:
	var parsed = JSON.parse_string(_config_text.text)
	if not (parsed is Dictionary):
		_config_error.text = "invalid JSON — not saved; fix the text and Save again"
		_config_dlg.popup_centered(Vector2(480, 380))
		return
	if VML.set_config(_config_mod_id, parsed):
		_status.text = "config saved for %s" % _config_mod_id
		print("VML: config saved for ", _config_mod_id)
	else:
		_config_error.text = "failed to save config"
		_config_dlg.popup_centered(Vector2(480, 380))
