@tool
extends Control
## VML Mods — the editor main-screen workspace for mod management.
## Replaces the old left-bottom dock; mod_manager_panel.gd now wraps this screen
## so the legacy dock slot (and the T29 instantiation test) keeps working.
##
## Left:  mod list with drag-to-reorder priority (persisted via VML.set_mod_order).
## Right: details (manifest / deps / errors / config) + hooks + content browser.
## Toolbar: Rescan / Install PCK / Install Zip (legacy) / Create Mod / Reload DB.

const ModWizard = preload("mod_wizard.gd")

## Tree subclass that lets the user drag a mod to reorder its priority. Extends
## VMLResizableTree so column headers are also drag-resizable (G4).
class _ModListTree extends VMLResizableTree:
	signal mods_reordered(order: PackedStringArray)

	func _ready() -> void:
		super._ready() # connects the column-resize gui_input handler
		drop_mode_flags = Tree.DROP_MODE_INBETWEEN

	func _get_drag_data(at_position: Vector2) -> Variant:
		var item := get_item_at_position(at_position)
		if item == null:
			return null
		var preview := Label.new()
		preview.text = str(item.get_text(0))
		set_drag_preview(preview)
		return {"vml_drag_mod": item.get_meta("mod_id", "")}

	func _can_drop_data(at_position: Vector2, data) -> bool:
		return data is Dictionary and data.has("vml_drag_mod") \
				and get_drop_section_at_position(at_position) >= 0

	func _drop_data(at_position: Vector2, data) -> void:
		if not (data is Dictionary) or not data.has("vml_drag_mod"):
			return
		var dragged: String = str(data["vml_drag_mod"])
		var drop_item := get_item_at_position(at_position)
		if drop_item == null:
			return
		var drop_below: bool = get_drop_section_at_position(at_position) == 1
		var root := get_root()
		var order: PackedStringArray = []
		for child in root.get_children():
			order.append(str(child.get_meta("mod_id", "")))
		var dragged_idx := order.find(dragged)
		if dragged_idx < 0:
			return
		order.remove_at(dragged_idx)
		var target_idx := order.find(str(drop_item.get_meta("mod_id", "")))
		if target_idx < 0:
			return
		if drop_below:
			target_idx += 1
		order.insert(target_idx, dragged)
		mods_reordered.emit(order)


var _mod_tree: _ModListTree
var _hook_tree: Tree
var _content_tree: Tree
var _content_filter: LineEdit
var _status: Label
var _wizard: ConfirmationDialog
var _selected_mod := ""

# Detail panel widgets.
var _detail_name: Label
var _detail_meta: Label
var _detail_desc: Label
var _detail_root: Label
var _detail_deps: Label
var _detail_errors: Label
var _enable_btn: Button
var _export_btn: Button
var _uninstall_btn: Button
var _config_tab: VBoxContainer
var _config_status: Label
var _config_form: VBoxContainer
var _config_text: TextEdit
var _config_error: Label
var _config_form_controls := {}

# Dialogs.
var _config_dlg: ConfirmationDialog
var _confirm_dlg: ConfirmationDialog
var _pending_target := ""
var _pending_action := "" # "disable" | "enable" | "uninstall"


func _ready() -> void:
	name = "VML Mods"
	var vbox := VBoxContainer.new()
	vbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(vbox)

	# Toolbar.
	var toolbar := HBoxContainer.new()
	vbox.add_child(toolbar)
	_add_btn(toolbar, "Rescan", _on_rescan)
	_add_btn(toolbar, "Install PCK", _on_install_pck)
	_add_btn(toolbar, "Install Zip", _on_install_zip) # legacy, optional dev flow
	_add_btn(toolbar, "Create Mod", _on_create_mod)
	_add_btn(toolbar, "Reload DB", _on_reload)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toolbar.add_child(spacer)
	_status = Label.new()
	toolbar.add_child(_status)

	# Split: mod list | details.
	var split := HSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(split)

	# Left: mod list.
	_mod_tree = _ModListTree.new()
	_setup_columns(_mod_tree, ["Mod", "Namespace", "Enabled", "Loaded", "Priority", "Deps"],
			[140, 110, 60, 60, 50, 120])
	_mod_tree.item_selected.connect(_on_mod_selected)
	_mod_tree.mods_reordered.connect(_on_mods_reordered)
	_mod_tree.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.add_child(_mod_tree)

	# Right: details + hooks + content.
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.add_child(right)

	_detail_name = Label.new()
	_detail_name.add_theme_font_size_override("font_size", 18)
	_detail_name.text = "(no mod selected)"
	right.add_child(_detail_name)
	_detail_meta = Label.new()
	_detail_meta.add_theme_color_override("font_color", Color(0.6, 0.6, 0.65))
	right.add_child(_detail_meta)
	_detail_desc = Label.new()
	_detail_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	right.add_child(_detail_desc)
	_detail_root = Label.new()
	_detail_root.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_detail_root.add_theme_color_override("font_color", Color(0.55, 0.55, 0.6))
	right.add_child(_detail_root)
	_detail_deps = Label.new()
	_detail_deps.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	right.add_child(_detail_deps)
	_detail_errors = Label.new()
	_detail_errors.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	right.add_child(_detail_errors)

	var actions := HBoxContainer.new()
	right.add_child(actions)
	_enable_btn = _add_btn(actions, "Enable", _on_toggle)
	_export_btn = _add_btn(actions, "Export PCK", _on_export_pck)
	_uninstall_btn = _add_btn(actions, "Uninstall", _on_uninstall)
	_add_btn(actions, "Config", _on_config)

	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(tabs)

	# Config tab.
	_config_tab = VBoxContainer.new()
	_config_tab.name = "Config"
	tabs.add_child(_config_tab)
	_config_status = Label.new()
	_config_tab.add_child(_config_status)
	_config_form = VBoxContainer.new()
	_config_form.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_config_tab.add_child(_config_form)
	_config_text = TextEdit.new()
	_config_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_config_tab.add_child(_config_text)
	_config_error = Label.new()
	_config_error.add_theme_color_override("font_color", Color(1, 0.3, 0.3))
	_config_error.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_config_tab.add_child(_config_error)

	# Hooks tab.
	_hook_tree = VMLResizableTree.new()
	_setup_columns(_hook_tree, ["Hook", "Mod", "Priority", "Description"],
			[150, 90, 60, 220])
	_hook_tree.name = "Hooks"
	tabs.add_child(_hook_tree)

	# Content tab.
	var content_vbox := VBoxContainer.new()
	content_vbox.name = "Content"
	tabs.add_child(content_vbox)
	var filter_row := HBoxContainer.new()
	content_vbox.add_child(filter_row)
	filter_row.add_child(_lbl("ns:"))
	_content_filter = LineEdit.new()
	_content_filter.placeholder_text = "game"
	_content_filter.text_changed.connect(func(_t: String): _refresh_content())
	filter_row.add_child(_content_filter)
	_content_tree = VMLResizableTree.new()
	_setup_columns(_content_tree, ["ID", "Path", "Provider", "Type"], [140, 220, 90, 60])
	_content_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	content_vbox.add_child(_content_tree)

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
		# Only the last column expands; the rest are user-draggable with a sensible
		# minimum width and never clip their content.
		tree.set_column_expand(i, i == titles.size() - 1)
		tree.set_column_custom_minimum_width(i, widths[i])
		tree.set_column_clip_content(i, false)


func _add_btn(parent: Control, text: String, callable: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.pressed.connect(callable)
	parent.add_child(b)
	return b


func _lbl(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.custom_minimum_size = Vector2(36, 0)
	return l


func refresh() -> void:
	if not Engine.has_singleton("VML"):
		_status.text = "VML engine singleton not loaded"
		return
	_refresh_mods()
	_refresh_detail()
	_refresh_hooks()
	_refresh_content()
	_status.text = "%d mods, db=%s" % [VML.get_mod_ids().size(), VML.get_database_mode()]


func _displayed_mod_ids() -> Array:
	# Load-order first, then any remaining (invalid) mods.
	var out: Array = []
	for mid in VML.get_load_order():
		out.append(mid)
	for mid in VML.get_mod_ids():
		if not out.has(mid):
			out.append(mid)
	return out


func _refresh_mods() -> void:
	_mod_tree.clear()
	var root := _mod_tree.create_item()
	var to_select: TreeItem = null
	var selected_visible := false
	for mod_id in _displayed_mod_ids():
		var item := _mod_tree.create_item(root)
		var display := VML.get_mod_display_name(mod_id)
		item.set_text(0, display if not display.is_empty() else mod_id)
		item.set_text(1, mod_id)
		item.set_text(2, "yes" if VML.is_mod_enabled(mod_id) else "no")
		item.set_text(3, "yes" if VML.is_mod_loaded(mod_id) else "no")
		var pri := VML.get_mod_priority(mod_id)
		item.set_text(4, str(pri) if pri >= 0 else "-")
		var deps: Dictionary = VML.get_mod_dependencies(mod_id)
		if deps.is_empty():
			item.set_text(5, "")
		else:
			var parts: PackedStringArray = []
			for dep_id in deps:
				parts.append(dep_id)
			item.set_text(5, ", ".join(parts))
		item.set_meta("mod_id", mod_id)
		var errs: Array = VML.get_mod_report(mod_id).get("errors", [])
		if errs.size() > 0:
			item.set_custom_color(0, Color(1, 0.45, 0.45))
		elif VML.is_mod_loaded(mod_id):
			item.set_custom_color(0, Color(0.6, 0.9, 0.6))
		if mod_id == _selected_mod:
			to_select = item
			selected_visible = true
	if to_select != null:
		_mod_tree.set_selected(to_select, 0)
		_mod_tree.scroll_to_item(to_select)
	if not selected_visible:
		_selected_mod = ""


func _refresh_detail() -> void:
	var id := _selected_mod
	if id.is_empty() or not Engine.has_singleton("VML"):
		_detail_name.text = "(no mod selected)"
		_detail_meta.text = ""
		_detail_desc.text = ""
		_detail_root.text = ""
		_detail_deps.text = ""
		_detail_errors.text = ""
		_enable_btn.text = "Enable"
		_enable_btn.disabled = true
		_export_btn.disabled = true
		_uninstall_btn.disabled = true
		_config_status.text = ""
		return
	var display := VML.get_mod_display_name(id)
	_detail_name.text = display if not display.is_empty() else id
	_detail_meta.text = "%s  ·  v%s" % [id, VML.get_mod_version(id)]
	_detail_desc.text = VML.get_mod_description(id)
	_detail_root.text = "root: " + VML.get_mod_path(id)
	var deps: Dictionary = VML.get_mod_dependencies(id)
	if deps.is_empty():
		_detail_deps.text = "deps: (none)"
	else:
		var parts: PackedStringArray = []
		for dep_id in deps:
			var ok: bool = deps[dep_id].get("exists", false)
			var en: bool = deps[dep_id].get("enabled", false)
			parts.append("%s[%s%s]" % [dep_id, "exists" if ok else "missing", "/on" if en else "/off"])
		_detail_deps.text = "deps: " + ", ".join(parts)
	var report: Dictionary = VML.get_mod_report(id)
	var errs: Array = report.get("errors", [])
	var warns: Array = report.get("warnings", [])
	var problem := ""
	if errs.size() > 0:
		problem += "errors:\n  - " + "\n  - ".join(errs) + "\n"
	if warns.size() > 0:
		problem += "warnings:\n  - " + "\n  - ".join(warns)
	_detail_errors.text = problem
	if errs.size() > 0:
		_detail_errors.add_theme_color_override("font_color", Color(1, 0.45, 0.45))
	elif warns.size() > 0:
		_detail_errors.add_theme_color_override("font_color", Color(1, 0.8, 0.4))
	else:
		_detail_errors.remove_theme_color_override("font_color")
	var enabled := VML.is_mod_enabled(id)
	_enable_btn.text = "Disable" if enabled else "Enable"
	_enable_btn.disabled = false
	_export_btn.disabled = VML.get_mod_path(id).is_empty()
	_uninstall_btn.disabled = false


func _refresh_hooks() -> void:
	_hook_tree.clear()
	var root := _hook_tree.create_item()
	var hooks: Dictionary = VML.list_hooks()
	var points: Dictionary = VML.list_hook_points()
	for hook_id in hooks:
		var info: Dictionary = hooks[hook_id]
		var desc: String = points.get(hook_id, {}).get("description", "") if points.has(hook_id) else ""
		var item := _hook_tree.create_item(root)
		item.set_text(0, hook_id)
		item.set_text(1, "%d handler(s)" % info.get("count", 0))
		item.set_text(2, "")
		item.set_text(3, desc)
		# One row per handler: [Hook, Mod, Priority, Description].
		var handlers: Array = VML.list_hook_handlers(hook_id)
		for h in handlers:
			var row := _hook_tree.create_item(item)
			row.set_text(0, "")
			row.set_text(1, str(h.get("mod_id", "")))
			row.set_text(2, str(h.get("priority", 0)))
			row.set_text(3, desc)
		item.collapsed = true
	for point_id in points:
		if hooks.has(point_id):
			continue # already shown as the hook's parent row
		var info: Dictionary = points[point_id]
		var item := _hook_tree.create_item(root)
		item.set_text(0, point_id)
		item.set_text(1, "declared")
		item.set_text(2, "")
		item.set_text(3, info.get("description", ""))


func _refresh_content() -> void:
	_content_tree.clear()
	var root := _content_tree.create_item()
	var ns_filter := _content_filter.text.strip_edges()
	var ids := VML.list_ids()
	for ns in ids:
		if not ns_filter.is_empty() and not str(ns).begins_with(ns_filter):
			continue
		for path in ids[ns]:
			var full: String = str(ns) + ":" + str(path)
			var info: Dictionary = VML.get_id_info(full)
			var item := _content_tree.create_item(root)
			item.set_text(0, full)
			item.set_text(1, info.get("path", ""))
			item.set_text(2, info.get("provider_mod", ""))
			item.set_text(3, info.get("data_type", ""))


func _on_mod_selected() -> void:
	var item := _mod_tree.get_selected()
	_selected_mod = item.get_meta("mod_id", "") if item else ""
	_refresh_detail()


func _on_mods_reordered(order: PackedStringArray) -> void:
	if not Engine.has_singleton("VML"):
		return
	if VML.set_mod_order(order):
		print("VML: mod order updated: ", ", ".join(order))
		refresh()
	else:
		_status.text = "order rejected (dependency order must be respected)"
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
	_status.text = "install (legacy zip): %s" % error_string(err)
	print("VML: install_mod_from_zip(%s) -> %s" % [path, err])
	refresh()


func _on_install_pck() -> void:
	if not Engine.has_singleton("VML"):
		return
	var fd := FileDialog.new()
	fd.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	fd.access = FileDialog.ACCESS_FILESYSTEM
	fd.exclusive = false # avoid "exclusive child window" clash with other dialogs
	fd.add_filter("*.pck", "Vortaris Mod Pack (pck)")
	add_child(fd)
	fd.file_selected.connect(func(p: String):
		_on_pck_selected(p)
		fd.queue_free())
	fd.canceled.connect(func(): fd.queue_free())
	fd.popup_centered_ratio(0.5)


func _on_pck_selected(path: String) -> void:
	if not Engine.has_singleton("VML"):
		return
	var root := VML.install_root()
	var err := _copy_file_into(path, root)
	if err != OK:
		_status.text = "install: %s" % error_string(err)
		return
	VML.rescan()
	# Packs mount via load_resource_pack at runtime; in the editor they are staged
	# into the writable root and picked up on the next game run.
	_status.text = "installed %s -> %s (mounted on next run)" % [path.get_file(), root]
	print("VML: installed pack %s -> %s" % [path, root])
	refresh()


func _copy_file_into(src: String, root: String) -> Error:
	if DirAccess.make_dir_recursive_absolute(root) != OK:
		return ERR_CANT_CREATE
	var dest := root + "/" + src.get_file()
	if src == dest:
		return OK
	if FileAccess.file_exists(dest):
		DirAccess.remove_absolute(dest)
	if DirAccess.copy_absolute(src, dest) != OK:
		return ERR_CANT_CREATE
	return OK


func _on_create_mod() -> void:
	_wizard.show_create()


func _on_mod_created(mod_id: String) -> void:
	print("VML: mod created: ", mod_id)
	if Engine.has_singleton("VML"):
		VML.rescan()
	_selected_mod = mod_id
	refresh()


func _on_toggle() -> void:
	var id := _selected_mod
	if id.is_empty():
		_status.text = "select a mod first"
		return
	if not Engine.has_singleton("VML"):
		return
	if VML.is_mod_enabled(id):
		var dependents := VML.get_mod_dependents(id)
		if dependents.size() > 0:
			_ask_confirm("Disable", id, "This will also disable", dependents)
			return
		_do_disable(id)
	else:
		var deps := VML.get_mod_dependencies(id)
		var missing: Array = []
		var to_enable: Array = []
		for dep_id in deps:
			if not deps[dep_id]["exists"]:
				missing.append(dep_id)
			elif not deps[dep_id]["enabled"]:
				to_enable.append(dep_id)
		if missing.size() > 0:
			_status.text = "cannot enable %s — missing: %s" % [id, " · ".join(missing)]
			return
		if to_enable.size() > 0:
			_ask_confirm("Enable", id, "This will also enable", to_enable)
			return
		_do_enable(id)


func _on_uninstall() -> void:
	var id := _selected_mod
	if id.is_empty():
		return
	if not Engine.has_singleton("VML"):
		return
	if VML.is_mod_enabled(id):
		_ask_confirm("Uninstall", id, "The mod is enabled — disabling it first. This will also disable", VML.get_mod_dependents(id))
	else:
		_confirm_uninstall(id)


func _build_confirm_dialog() -> void:
	_confirm_dlg = ConfirmationDialog.new()
	_confirm_dlg.ok_button_text = "OK"
	_confirm_dlg.cancel_button_text = "Cancel"
	add_child(_confirm_dlg)
	_confirm_dlg.confirmed.connect(_on_confirm)


func _ask_confirm(action: String, id: String, note: String, list: Array) -> void:
	_pending_target = id
	if action == "Uninstall":
		_pending_action = "uninstall"
	elif action == "Disable":
		_pending_action = "disable"
	else:
		_pending_action = "enable"
	var list_text := ""
	if list.size() > 0:
		list_text = "\n%s:\n%s" % [note, "\n".join(list)]
	elif not note.is_empty():
		list_text = "\n" + note
	_confirm_dlg.title = action + " Mod"
	_confirm_dlg.dialog_text = "%s '%s'?%s" % [action, id, list_text]
	_confirm_dlg.ok_button_text = action
	_confirm_dlg.popup_centered(Vector2(460, 260))


func _confirm_uninstall(id: String) -> void:
	_pending_target = id
	_pending_action = "uninstall"
	_confirm_dlg.title = "Uninstall Mod"
	_confirm_dlg.dialog_text = "Uninstall '%s'? Its files will be removed." % id
	_confirm_dlg.ok_button_text = "Uninstall"
	_confirm_dlg.popup_centered(Vector2(460, 220))


func _on_confirm() -> void:
	if _pending_action == "disable":
		_do_disable(_pending_target)
	elif _pending_action == "enable":
		_do_enable(_pending_target)
	elif _pending_action == "uninstall":
		_do_uninstall(_pending_target)
	_pending_target = ""
	_pending_action = ""


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


func _do_uninstall(id: String) -> void:
	var err := VML.uninstall_mod(id)
	print("VML: uninstall_mod(", id, ") -> ", err)
	_status.text = "uninstall %s: %s" % [id, error_string(err)]
	_selected_mod = ""
	refresh()


func _build_config_dialog() -> void:
	_config_dlg = ConfirmationDialog.new()
	_config_dlg.title = "Mod Config"
	_config_dlg.ok_button_text = "Save"
	_config_dlg.cancel_button_text = "Cancel"
	var vbox := VBoxContainer.new()
	vbox.custom_minimum_size = Vector2(460, 380)
	_config_dlg.add_child(vbox)
	var status := Label.new()
	vbox.add_child(status)
	_config_status = status
	var form := VBoxContainer.new()
	form.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(form)
	_config_form = form
	var text := TextEdit.new()
	text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(text)
	_config_text = text
	var err := Label.new()
	err.add_theme_color_override("font_color", Color(1, 0.3, 0.3))
	err.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(err)
	_config_error = err
	add_child(_config_dlg)
	_config_dlg.confirmed.connect(_on_config_save)


func _on_config() -> void:
	var id := _selected_mod
	if id.is_empty() or not Engine.has_singleton("VML"):
		return
	var schema: Dictionary = VML.get_config_schema(id)
	_config_status.text = "mod: %s  ·  config_schema: %s" % [id, "declared" if not schema.is_empty() else "none"]
	var values: Dictionary = VML.get_config(id)
	if _build_schema_form(schema, values):
		_config_text.visible = false
		_config_form.visible = true
	else:
		_config_form.visible = false
		_config_text.visible = true
		_config_text.text = JSON.stringify(values, "  ")
	_config_error.text = ""
	_config_dlg.popup_centered(Vector2(480, 420))


# Builds per-key form controls from a JSON-Schema style config_schema.
# Returns false (fall back to JSON TextEdit) when no usable schema.
func _build_schema_form(schema: Dictionary, values: Dictionary) -> bool:
	for child in _config_form.get_children():
		child.queue_free()
	_config_form_controls.clear()
	var props: Variant = schema.get("properties")
	if props is not Dictionary or (props as Dictionary).is_empty():
		return false
	for key in props:
		var p: Variant = props[key]
		if p is not Dictionary:
			continue
		var type := String((p as Dictionary).get("type", "string"))
		var label := Label.new()
		label.text = str(key)
		_config_form.add_child(label)
		var ctl: Control
		if type == "number" or type == "integer":
			var spin := SpinBox.new()
			spin.min_value = -1000000
			spin.max_value = 1000000
			spin.value = float(values.get(key, 0.0))
			ctl = spin
		elif type == "boolean":
			var cb := CheckBox.new()
			cb.text = str(key)
			cb.button_pressed = bool(values.get(key, false))
			ctl = cb
		elif (p as Dictionary).has("enum"):
			var ob := OptionButton.new()
			for e in (p as Dictionary)["enum"]:
				ob.add_item(str(e))
			var cur := str(values.get(key, ""))
			var idx := 0
			for i in ob.item_count:
				if ob.get_item_text(i) == cur:
					idx = i
					break
			ob.select(idx)
			ctl = ob
		else:
			var le := LineEdit.new()
			le.text = str(values.get(key, ""))
			ctl = le
		_config_form.add_child(ctl)
		_config_form_controls[str(key)] = ctl
	return true


func _on_config_save() -> void:
	var values: Dictionary
	if _config_form.visible and not _config_form_controls.is_empty():
		for key in _config_form_controls:
			var ctl = _config_form_controls[key]
			if ctl is SpinBox:
				values[key] = ctl.value
			elif ctl is CheckBox:
				values[key] = ctl.button_pressed
			elif ctl is OptionButton:
				values[key] = ctl.get_item_text(ctl.selected)
			elif ctl is LineEdit:
				values[key] = ctl.text
	else:
		var parsed = JSON.parse_string(_config_text.text)
		if not (parsed is Dictionary):
			_config_error.text = "invalid JSON — not saved; fix the text and Save again"
			_config_dlg.popup_centered(Vector2(480, 420))
			return
		values = parsed
	if VML.set_config(_selected_mod, values):
		_status.text = "config saved for %s" % _selected_mod
		print("VML: config saved for ", _selected_mod)
	else:
		_config_error.text = "failed to save config"
		_config_dlg.popup_centered(Vector2(480, 420))


# --- Export PCK (G6) --------------------------------------------------------

func _on_export_pck() -> void:
	var id := _selected_mod
	if id.is_empty() or not Engine.has_singleton("VML"):
		_status.text = "select a mod first"
		return
	var root := VML.get_mod_path(id)
	if root.is_empty():
		_status.text = "cannot find mod root for " + id
		return
	var fd := FileDialog.new()
	fd.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	fd.access = FileDialog.ACCESS_FILESYSTEM
	fd.exclusive = false
	fd.add_filter("*.pck", "Vortaris Mod Pack (pck)")
	fd.current_file = id + ".pck"
	add_child(fd)
	fd.file_selected.connect(func(p: String):
		_export_to_pck(id, root, p)
		fd.queue_free())
	fd.canceled.connect(func(): fd.queue_free())
	fd.popup_centered_ratio(0.5)


func _export_to_pck(mod_id: String, root: String, out_path: String) -> void:
	var err := build_mod_pck(mod_id, root, out_path)
	if err == OK:
		_status.text = "exported %s -> %s" % [mod_id, out_path]
		print("VML: exported ", mod_id, " -> ", out_path)
	else:
		_status.text = "export failed (%s) for %s" % [error_string(err), mod_id]


## Packs a mod's root directory into a .pck whose internal paths are namespaced
## under res://mods/<mod_id>/. Dropped into a configured mod root (res://mods in
## dev, or a user path in exports) the pack mounts read-only at startup and its
## content is isolated under the id layer — it never collides with the game's own
## res:// files. Excludes .import/.uid and hidden files.
static func build_mod_pck(mod_id: String, root: String, out_path: String) -> Error:
	var packer := PCKPacker.new()
	var err := packer.pck_start(out_path)
	if err != OK:
		return err
	err = _add_dir_to_pck(packer, root, "res://mods/" + mod_id)
	if err != OK:
		packer.flush() # release the file handle so callers can clean up
		return err
	return packer.flush()


static func _add_dir_to_pck(packer: PCKPacker, dir: String, pck_prefix: String) -> Error:
	var d := DirAccess.open(dir)
	if d == null:
		return ERR_CANT_OPEN
	d.list_dir_begin() # 4.7: skips . and .. automatically; hidden files are NOT skipped
	var e := d.get_next()
	var err := OK
	while e != "":
		# Exclude Godot's import metadata / uid files and hidden files (.godot/, .git/).
		if not e.begins_with(".") and not e.ends_with(".import") and not e.ends_with(".uid"):
			var rel := pck_prefix + "/" + e
			var full := dir + "/" + e
			if d.current_is_dir():
				err = _add_dir_to_pck(packer, full, rel)
			else:
				err = packer.add_file(rel, full)
			if err != OK:
				break
		e = d.get_next()
	d.list_dir_end()
	return err
