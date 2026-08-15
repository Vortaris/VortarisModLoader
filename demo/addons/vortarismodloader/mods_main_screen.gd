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
	# The mod dragged in the most recent _drop_data, so the main screen can tell the
	# user when it cannot be reordered (L3).
	var last_dragged_mod := ""

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
		last_dragged_mod = str(item.get_meta("mod_id", ""))
		return {"vml_drag_mod": last_dragged_mod}

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
var _config_status: Label
var _config_form: VBoxContainer
var _config_text: TextEdit
var _config_error: Label
var _config_form_controls := {}
var _status_pending := false

# Dialogs.
var _config_dlg: ConfirmationDialog
var _confirm_dlg: ConfirmationDialog
var _pending_target := ""
var _pending_action := "" # "disable" | "enable" | "uninstall"


func _ready() -> void:
	name = "VML Mods"
	# Fill the editor main-screen area: this root is added to the editor's
	# main-screen VBoxContainer, so it needs EXPAND_FILL to take the full height
	# (otherwise it collapses to its minimum and the whole screen looks "short").
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	var vbox := VBoxContainer.new()
	vbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(vbox)

	# Toolbar (top): actions + an app title on the right.
	var toolbar_panel := PanelContainer.new()
	vbox.add_child(toolbar_panel)
	var toolbar := HBoxContainer.new()
	toolbar_panel.add_child(toolbar)
	_add_btn(toolbar, "Rescan", _on_rescan)
	_add_btn(toolbar, "Install PCK", _on_install_pck)
	_add_btn(toolbar, "Install Zip", _on_install_zip) # legacy, optional dev flow
	_add_btn(toolbar, "Create Mod", _on_create_mod)
	_add_btn(toolbar, "Reload DB", _on_reload)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toolbar.add_child(spacer)
	var title := Label.new()
	title.text = "VortarisModLoader"
	title.add_theme_color_override("font_color", Color(0.55, 0.55, 0.6))
	toolbar.add_child(title)

	vbox.add_child(_make_sep())

	# Split: mod list | details.
	var split := HSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(split)

	# Left: mod list, grouped in a panel with its own header.
	var left_panel := PanelContainer.new()
	left_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.add_child(left_panel)
	var left_vbox := VBoxContainer.new()
	left_panel.add_child(left_vbox)
	left_vbox.add_child(_section_title("Mods"))
	left_vbox.add_child(_make_sep())
	_mod_tree = _ModListTree.new()
	_setup_columns(_mod_tree, ["Mod", "Namespace", "Enabled", "Loaded", "Priority", "Deps"],
			[140, 110, 60, 60, 50, 120])
	_mod_tree.item_selected.connect(_on_mod_selected)
	_mod_tree.mods_reordered.connect(_on_mods_reordered)
	_mod_tree.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_mod_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_mod_tree.custom_minimum_size.y = 150
	left_vbox.add_child(_mod_tree)

	# Right: details + hooks + content. A VSeparator between the two panes gives a
	# clear visual boundary (X1); HSplitContainer keeps the drag handle between the
	# mod list and the separator, so both stay independently sized.
	split.add_child(VSeparator.new())
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.add_child(right)

	# Right column: Details + Hooks/Content tabs live in a draggable VSplitContainer
	# so the user can manually adjust how much height each section gets; the details
	# panel is shrink-begin (and its error list is capped in a scroll) so it can
	# never squeeze the tabs out of view.
	var v_split := VSplitContainer.new()
	v_split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(v_split)

	# Details header panel.
	var detail_panel := PanelContainer.new()
	# SIZE_FILL (not SHRINK_BEGIN): the panel grows/shrinks with the VSplitContainer
	# divider when the user drags it, so the Details section can be resized freely.
	# The ScrollContainer inside caps the *content* so it scrolls when the panel is
	# small and shows more when the panel is enlarged.
	detail_panel.size_flags_vertical = Control.SIZE_FILL
	v_split.add_child(detail_panel)
	var detail_scroll := ScrollContainer.new()
	detail_scroll.custom_minimum_size.y = 240
	detail_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	detail_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	detail_panel.add_child(detail_scroll)
	var detail_vbox := VBoxContainer.new()
	detail_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	detail_scroll.add_child(detail_vbox)
	detail_vbox.add_child(_section_title("Details"))
	detail_vbox.add_child(_make_sep())
	_detail_name = Label.new()
	_detail_name.add_theme_font_size_override("font_size", 18)
	_detail_name.text = "(no mod selected)"
	detail_vbox.add_child(_detail_name)
	_detail_meta = Label.new()
	_detail_meta.add_theme_color_override("font_color", Color(0.6, 0.6, 0.65))
	detail_vbox.add_child(_detail_meta)
	_detail_desc = Label.new()
	_detail_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	detail_vbox.add_child(_detail_desc)
	_detail_root = Label.new()
	_detail_root.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_detail_root.add_theme_color_override("font_color", Color(0.55, 0.55, 0.6))
	detail_vbox.add_child(_detail_root)
	_detail_deps = Label.new()
	_detail_deps.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	detail_vbox.add_child(_detail_deps)
	# Error list is capped in a scroll container so many errors can never blow up
	# the Details panel height (which would squeeze the Hooks/Content tabs out).
	var errors_scroll := ScrollContainer.new()
	errors_scroll.custom_minimum_size.y = 48
	errors_scroll.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	errors_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	detail_vbox.add_child(errors_scroll)
	_detail_errors = Label.new()
	_detail_errors.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_detail_errors.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	errors_scroll.add_child(_detail_errors)

	var actions := HBoxContainer.new()
	detail_vbox.add_child(actions)
	_enable_btn = _add_btn(actions, "Enable", _on_toggle)
	_export_btn = _add_btn(actions, "Export PCK", _on_export_pck)
	_uninstall_btn = _add_btn(actions, "Uninstall", _on_uninstall)
	_add_btn(actions, "Config", _on_config)

	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	tabs.custom_minimum_size.y = 120
	v_split.add_child(tabs)

	# Hooks tab. (The old Config *tab* was removed in 0.3.1 — configuration is
	# edited through the modal "Config" dialog, `_build_config_dialog`, so a
	# duplicate blank tab was dead UI / M2.)
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
	filter_row.add_child(_lbl("namespace:"))
	_content_filter = LineEdit.new()
	_content_filter.placeholder_text = "filter within mod (optional)"
	_content_filter.text_changed.connect(func(_t: String): _refresh_content())
	filter_row.add_child(_content_filter)
	_content_tree = VMLResizableTree.new()
	_setup_columns(_content_tree, ["ID", "Path", "Provider", "Type"], [140, 220, 90, 60])
	_content_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	content_vbox.add_child(_content_tree)

	# Refresh hooks/content when switching tabs (the selected mod's data may have
	# changed since the tab was last shown).
	tabs.tab_changed.connect(_on_tab_changed)

	_wizard = ModWizard.new()
	add_child(_wizard)
	_wizard.mod_created.connect(_on_mod_created)

	_build_config_dialog()
	_build_confirm_dialog()

	# Bottom status bar, visually separated from the content area. Anchored at the
	# bottom of the screen: the bar takes only its minimum height (SIZE_SHRINK_BEGIN)
	# so the content above fills normally, and the label is single-line + ellipsized
	# so a long message can never stretch the row over the content (X2).
	vbox.add_child(_make_sep())
	var status_bar := HBoxContainer.new()
	status_bar.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	status_bar.custom_minimum_size.y = 24
	vbox.add_child(status_bar)
	_status = Label.new()
	_status.clip_text = true
	_status.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	_status.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_status.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_bar.add_child(_status)

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
	l.custom_minimum_size = Vector2(84, 0)
	l.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	return l


## Thin horizontal rule used to separate the main screen's visual regions.
func _make_sep() -> HSeparator:
	return HSeparator.new()


## Section header used inside the left/right panels.
func _section_title(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", 16)
	l.add_theme_color_override("font_color", Color(0.8, 0.8, 0.85))
	return l


func refresh() -> void:
	if not Engine.has_singleton("VML"):
		_status.text = "VML engine singleton not loaded"
		return
	# Editor preview: run startup so mod_main scripts register their hooks in
	# _init (the Hooks tab depends on them). Idempotent — a no-op once done.
	if not VML.is_startup_done():
		VML.finish_startup()
	_refresh_mods()
	_refresh_detail()
	_refresh_hooks()
	_refresh_content()
	# Don't clobber the most recent operation result (M1): the default stats are
	# only written when no operation has pinned a status message via _set_status.
	if not _status_pending:
		_show_stats()


## Writes the default "N mods, db=..." line (shown on load and whenever the user
## browses a different mod — i.e. no operation result is pending).
func _show_stats() -> void:
	if Engine.has_singleton("VML"):
		_status.text = "%d mods, db=%s" % [VML.get_mod_ids().size(), VML.get_database_mode()]


## Records an operation result in the status bar and pins it so refresh() won't
## overwrite it with the default mod/db stats. The message stays visible until the
## next operation reports a new result, or the user selects a different mod (M1).
func _set_status(text: String) -> void:
	_status.text = text
	_status_pending = true


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
	var sel := _selected_mod
	if sel.is_empty() or not Engine.has_singleton("VML"):
		var empty := _hook_tree.create_item(root)
		empty.set_text(0, "(select a mod to see its hooks)")
		return
	if not VML.is_startup_done():
		VML.finish_startup()
	var hooks: Dictionary = VML.list_hooks()
	var points: Dictionary = VML.list_hook_points()
	for hook_id in hooks:
		# Show a hook only when the selected mod registered at least one handler
		# on it (list_hooks() gives every hook from every mod — filter per mod).
		var handlers: Array = VML.list_hook_handlers(hook_id)
		var mine: Array = []
		for h in handlers:
			if str(h.get("mod_id", "")) == sel:
				mine.append(h)
		if mine.is_empty():
			continue
		var desc: String = points.get(hook_id, {}).get("description", "") if points.has(hook_id) else ""
		var item := _hook_tree.create_item(root)
		item.set_text(0, hook_id)
		item.set_text(1, "%d handler(s)" % mine.size())
		item.set_text(2, "")
		item.set_text(3, desc)
		# One row per handler: [Hook, Mod, Priority, Description].
		for h in mine:
			var row := _hook_tree.create_item(item)
			row.set_text(0, "")
			row.set_text(1, str(h.get("mod_id", "")))
			row.set_text(2, str(h.get("priority", 0)))
			row.set_text(3, desc)
		item.collapsed = true
	if root.get_child_count() == 0:
		var empty := _hook_tree.create_item(root)
		empty.set_text(0, "(no hooks registered by this mod)")


func _refresh_content() -> void:
	_content_tree.clear()
	var root := _content_tree.create_item()
	var sel := _selected_mod
	if sel.is_empty() or not Engine.has_singleton("VML"):
		var empty := _content_tree.create_item(root)
		empty.set_text(0, "(select a mod to see its content)")
		return
	var ns_filter := _content_filter.text.strip_edges()
	# The mod's id equals its content namespace: show only ids under it.
	var ids := VML.list_ids(sel + ":")
	for ns in ids:
		for path in ids[ns]:
			var full: String = str(ns) + ":" + str(path)
			if not ns_filter.is_empty() and not full.contains(ns_filter):
				continue
			var info: Dictionary = VML.get_id_info(full)
			var item := _content_tree.create_item(root)
			item.set_text(0, full)
			item.set_text(1, info.get("path", ""))
			item.set_text(2, info.get("provider_mod", ""))
			item.set_text(3, info.get("data_type", ""))
	if root.get_child_count() == 0:
		var empty := _content_tree.create_item(root)
		empty.set_text(0, "(no content in this mod's namespace)")


func _on_mod_selected() -> void:
	var item := _mod_tree.get_selected()
	var new_id := item.get_meta("mod_id", "") if item else ""
	if new_id == _selected_mod:
		return # refresh()'s programmatic re-select keeps the pending status (M1)
	_selected_mod = new_id
	_status_pending = false
	_refresh_detail()
	_refresh_hooks()
	_refresh_content()
	_show_stats()


## TabContainer.tab_changed — refresh the per-mod Hooks/Content views whenever the
## user switches to them (data may have changed since the tab was last shown).
func _on_tab_changed(_index: int) -> void:
	if not Engine.has_singleton("VML"):
		return
	_refresh_hooks()
	_refresh_content()


func _on_mods_reordered(order: PackedStringArray) -> void:
	if not Engine.has_singleton("VML"):
		return
	# Invalid / broken mods (bad namespace, dependency cycle, ...) are not part of
	# the load order, so reordering one is a silent no-op that visually "bounces
	# back" on refresh. Say so instead of pretending it moved (L3).
	var dragged := _mod_tree.last_dragged_mod
	if not dragged.is_empty() and not VML.get_load_order().has(dragged):
		_set_status("cannot reorder '%s' — it is not part of the load order (invalid or disabled)" % dragged)
		refresh()
		return
	if VML.set_mod_order(order):
		_set_status("order updated (%d mods)" % order.size())
		print("VML: mod order updated: ", ", ".join(order))
		refresh()
	else:
		_set_status("order rejected (dependency order must be respected)")
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
	_set_status("install (legacy zip): %s" % error_string(err))
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
		_set_status("install: %s" % error_string(err))
		return
	VML.rescan()
	# Packs mount via load_resource_pack at runtime; in the editor they are staged
	# into the writable root and picked up on the next game run.
	_set_status("installed %s -> %s (mounted on next run)" % [path.get_file(), root])
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
		_set_status("select a mod first")
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
			_set_status("cannot enable %s — missing: %s" % [id, " · ".join(missing)])
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
		_set_status("disabled %s" % id)
	else:
		_set_status("cannot disable %s — %s" % [id, " · ".join(VML.get_mod_errors(id))])
	refresh()


func _do_enable(id: String) -> void:
	var ok := VML.enable_mod(id)
	print("VML: enable_mod(", id, ") -> ", ok)
	if ok:
		_set_status("enabled %s" % id)
	else:
		_set_status("cannot enable %s — %s" % [id, " · ".join(VML.get_mod_errors(id))])
	refresh()


func _do_uninstall(id: String) -> void:
	var err := VML.uninstall_mod(id)
	print("VML: uninstall_mod(", id, ") -> ", err)
	_set_status("uninstall %s: %s" % [id, error_string(err)])
	_selected_mod = ""
	refresh()


func _build_config_dialog() -> void:
	_config_dlg = ConfirmationDialog.new()
	_config_dlg.title = "Mod Config"
	_config_dlg.ok_button_text = "Save"
	_config_dlg.cancel_button_text = "Cancel"
	# Bound the dialog (wrap_controls=false + explicit size) so a large schema or
	# long JSON text can never blow it up off-screen.
	_config_dlg.wrap_controls = false
	_config_dlg.min_size = Vector2(460, 420)
	_config_dlg.size = Vector2(460, 500)
	var vbox := VBoxContainer.new()
	vbox.custom_minimum_size = Vector2(460, 420)
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
	var built_any := false
	for key in props:
		var p: Variant = props[key]
		if p is not Dictionary:
			continue
		built_any = true
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
	# Every property was a non-Dictionary value — a malformed schema would render an
	# empty form that then saved a stale JSON text. Report "no usable schema" so the
	# caller falls back to JSON text editing (L4).
	return built_any


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
		_set_status("config saved for %s" % _selected_mod)
		print("VML: config saved for ", _selected_mod)
	else:
		_config_error.text = "failed to save config"
		_config_dlg.popup_centered(Vector2(480, 420))


# --- Export PCK (G6) --------------------------------------------------------

func _on_export_pck() -> void:
	var id := _selected_mod
	if id.is_empty() or not Engine.has_singleton("VML"):
		_set_status("select a mod first")
		return
	var root := VML.get_mod_path(id)
	if root.is_empty():
		_set_status("cannot find mod root for " + id)
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
		_set_status("exported %s -> %s" % [mod_id, out_path])
		print("VML: exported ", mod_id, " -> ", out_path)
	else:
		_set_status("export failed (%s) for %s" % [error_string(err), mod_id])


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
