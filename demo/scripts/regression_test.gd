extends SceneTree
## Headless regression suite (M2–M4 + 0.2.x + 0.3.0 A/B: routing layer, override,
## content database, hooks, lifecycle, registry, placeholders, mod order).
## Run: godot --headless --path demo --script res://scripts/regression_test.gd
## Exit code 0 = all tests pass.
##
## NOTE: signals from the VML engine singleton must be connected to named
## methods, not lambdas — a lambda held across engine shutdown crashes at exit.

var _fired_ok := false
var _test_event_fired := false
var _mod_loaded_ids: Array = []
var _mod_unloaded := false
var _mod_reloaded := false
var _async_progress_seen := false
var _database_loaded_async := false


func _on_preload_progress(_current: int, _total: int) -> void:
	_async_progress_seen = true


func _on_database_loaded_async() -> void:
	_database_loaded_async = true

func _on_db_entry_changed(_id: String) -> void:
	_fired_ok = true


func _on_test_event(_msg: String) -> void:
	_test_event_fired = true


func _on_mod_loaded(mod_id: String) -> void:
	_mod_loaded_ids.append(mod_id)


func _on_mod_unloaded(_mod_id: String) -> void:
	_mod_unloaded = true


func _on_mod_reloaded(_mod_id: String) -> void:
	_mod_reloaded = true


func _on_ctx_hook(ctx: Dictionary, amount: int) -> Dictionary:
	ctx["amount"] = ctx.get("amount", 0) + amount
	return ctx


# Recursive directory delete (used to clean pck test staging under user://).
func _rmtree(path: String) -> void:
	if DirAccess.dir_exists_absolute(path):
		var d := DirAccess.open(path)
		if d == null:
			return
		d.list_dir_begin()
		var e := d.get_next()
		while e != "":
			if e != "." and e != "..":
				var child := path + "/" + e
				if d.current_is_dir():
					_rmtree(child)
				else:
					DirAccess.remove_absolute(child)
			e = d.get_next()
		d.list_dir_end()
		DirAccess.remove_absolute(path)
	elif FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)

func _initialize() -> void:
	var failed := false
	VML.mod_loaded.connect(_on_mod_loaded)

	# Clean up any zip mod left over from a previous run (user:// persists).
	if VML.get_mod_ids().has("archerpack"):
		VML.uninstall_mod("archerpack")
	# Remove a stale .pck test artifact from a previous (crashed) run so the suite
	# is repeatable; the pack file is re-generated later by the T65 block. The OS
	# (globalized) path is used because a mounted pack makes res:// read-only.
	var stale_pck := ProjectSettings.globalize_path("res://mods/sample_pck_mod")
	if FileAccess.file_exists("res://mods/sample_pck_mod/sample_mod.pck"):
		DirAccess.remove_absolute(ProjectSettings.globalize_path("res://mods/sample_pck_mod/sample_mod.pck"))
	_rmtree(stale_pck)
	# A wizard-created mod left behind by a crashed run would be discovered at boot
	# and re-created by T30; drop it so the suite is repeatable.
	var stale_wiz := ProjectSettings.globalize_path("res://mods-unpacked/test_wiz")
	if DirAccess.dir_exists_absolute(stale_wiz):
		_rmtree(stale_wiz)
	# Reset persisted enable-state from previous runs so the suite is repeatable.
	DirAccess.remove_absolute("user://vml/profile.json")
	# A user-defined mod order (drag-to-reorder, B5) would skew priority tests.
	DirAccess.remove_absolute("user://vml/load_order.json")
	VML.rescan()

	# --- T0: singleton ---
	if not VMLTestUtil.expect(Engine.has_singleton("VML"), "T0 VML singleton exists"):
		failed = true

	# --- T1: id lookup by implicit base scan ---
	if not VMLTestUtil.expect(VML.has("game:units.peasant"), "T1 base id has game:units.peasant"):
		failed = true
	if not VMLTestUtil.expect(VML.has("game:recipes"), "T1 base id has game:recipes"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("nope:missing"), "T1 unknown id reports false"):
		failed = true

	# --- T2: get_data returns parsed Dictionary ---
	var peasant = VML.get_data("game:units.peasant")
	if not VMLTestUtil.expect(peasant is Dictionary, "T2 get_data returns Dictionary"):
		failed = true
	elif not VMLTestUtil.expect_eq(peasant.get("name"), "Peasant", "T2 unit name"):
		failed = true
	elif not VMLTestUtil.expect_eq(peasant.get("health"), 50, "T2 unit health"):
		failed = true

	# --- T3: explicit register/unregister ---
	if not VMLTestUtil.expect(VML.register("mymod:custom", "res://data/game/units/peasant.json"),
			"T3 register explicit id"):
		failed = true
	if not VMLTestUtil.expect(VML.has("mymod:custom"), "T3 registered id is visible"):
		failed = true
	if not VMLTestUtil.expect(VML.unregister("mymod:custom"), "T3 unregister"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("mymod:custom"), "T3 unregistered id gone"):
		failed = true

	# --- T3b: resolve gives the physical path (peasant is not overridden) ---
	if not VMLTestUtil.expect_eq(VML.resolve("game:units.peasant"), "res://data/game/units/peasant.json",
			"T3b resolve physical path"):
		failed = true

	# --- T3c/d: listing ---
	if not VMLTestUtil.expect(VML.list_namespaces().has("game"), "T3c namespaces include game"):
		failed = true
	var ids = VML.list_ids("game:units.")
	if not VMLTestUtil.expect(ids.has("game") and ids["game"].has("units.knight"),
			"T3d list_ids prefix filter"):
		failed = true

	# --- M3: mod discovery / override / ordering ---
	var load_order = VML.get_load_order()
	var mylib_idx := load_order.find("mylib")
	var mymod_idx := load_order.find("mymod")
	if not VMLTestUtil.expect(mylib_idx >= 0 and mymod_idx > mylib_idx,
			"T4 dependency load order (mylib before mymod)"):
		failed = true
	if not VMLTestUtil.expect(load_order.has("cycle_a") == false,
			"T6 cyclic mod excluded from load order"):
		failed = true
	if not VMLTestUtil.expect(load_order.has("BadMod") == false,
			"T14 invalid manifest mod excluded"):
		failed = true
	if not VMLTestUtil.expect(load_order.has("incompat_mod") == false,
			"T51 incompat mod excluded from load order"):
		failed = true

	# T5: mod overrides base, and the later-loaded mod wins.
	var knight: Dictionary = VML.get_data("game:units.knight")
	if not VMLTestUtil.expect_eq(knight.get("attack"), 15,
			"T5 mod overrides base knight (sample_mod wins over lib_mod)"):
		failed = true
	if not VMLTestUtil.expect(VML.resolve("game:units.knight").begins_with(
			"res://mods-unpacked/sample_mod/"), "T5 override resolves to sample_mod"):
		failed = true

	# New content from a mod namespace.
	if not VMLTestUtil.expect(VML.has("mymod:units.archer"), "T4 mod adds new id"):
		failed = true

	# Error surfacing.
	if not VMLTestUtil.expect(VML.get_mod_errors("cycle_a").size() > 0,
			"T6 cycle error surfaced"):
		failed = true
	if not VMLTestUtil.expect(VML.get_mod_errors("BadMod").size() > 0,
			"T14 manifest error surfaced"):
		failed = true

	# T16: listing by namespace.
	var by_ns = VML.list_ids()
	if not VMLTestUtil.expect(by_ns.has("mymod") and by_ns["mymod"].has("units.archer"),
			"T16 list_ids grouped by namespace includes mod content"):
		failed = true

	# --- M4: unified content database ---
	if not VMLTestUtil.expect_eq(VML.get_database_mode(), "data", "T19 database_mode default data"):
		failed = true
	# Data was preloaded into memory at startup: get_all sees it without loading.
	var all_units = VML.get_all("game:units.")
	if not VMLTestUtil.expect(all_units.size() == 2, "T19 get_all prefetched data resident"):
		failed = true

	# set_data overwrites live, get_data reflects it, and the signal fires.
	_fired_ok = false
	VML.database_entry_changed.connect(_on_db_entry_changed)
	if not VMLTestUtil.expect(VML.set_data("game:units.peasant", {"name": "Modded Peasant"}),
			"T20 set_data returns true"):
		failed = true
	var modified = VML.get_data("game:units.peasant")
	if not VMLTestUtil.expect_eq(modified.get("name"), "Modded Peasant",
			"T20 get_data reflects set_data"):
		failed = true
	if not VMLTestUtil.expect(_fired_ok, "T20 database_entry_changed emitted"):
		failed = true
	VML.database_entry_changed.disconnect(_on_db_entry_changed)

	# delete_data removes the override; get_data falls back to the file.
	if not VMLTestUtil.expect(VML.delete_data("game:units.peasant"), "T21 delete_data"):
		failed = true
	var restored = VML.get_data("game:units.peasant")
	if not VMLTestUtil.expect_eq(restored.get("name"), "Peasant", "T21 get_data falls back to file"):
		failed = true

	# Prefix query over the resident database.
	var units = VML.get_all("game:units.")
	if not VMLTestUtil.expect(units.has("game:units.peasant") and units.has("game:units.knight"),
			"T22 get_all prefix filter"):
		failed = true

	# Mode switching round-trips and triggers a reload.
	if not VMLTestUtil.expect(VML.set_database_mode("off") and VML.get_database_mode() == "off",
			"T23 set_database_mode off"):
		failed = true
	if not VMLTestUtil.expect(VML.set_database_mode("data") and VML.get_database_mode() == "data",
			"T23 set_database_mode back to data"):
		failed = true

	# --- convenience sugar ---
	if not VMLTestUtil.expect_eq((VML.get("game:units.peasant") as Dictionary).get("name"),
			"Peasant", "T31 get() alias"):
		failed = true
	if not VMLTestUtil.expect(VML.exists("game:units.peasant"), "T31 exists() alias"):
		failed = true
	if not VMLTestUtil.expect(VML.load("game:scenes.camp") is PackedScene, "T31 load() alias"):
		failed = true
	if not VMLTestUtil.expect_eq(VML.get_mod_path("mymod"), "res://mods-unpacked/sample_mod",
			"T31 get_mod_path"):
		failed = true

	# --- id metadata & operations ---
	var info: Dictionary = VML.get_id_info("game:units.peasant")
	if not VMLTestUtil.expect(info.get("valid") and info.get("resolved"), "T33 get_id_info resolved"):
		failed = true
	if not VMLTestUtil.expect_eq(info.get("provider_mod"), "base", "T33 id provider is base"):
		failed = true
	if not VMLTestUtil.expect_eq(VML.get_id_data_type("game:units.peasant"), "data", "T33 id data type"):
		failed = true
	if not VMLTestUtil.expect(VML.set_id_type("game:units.peasant", "unit"), "T33 set_id_type"):
		failed = true
	if not VMLTestUtil.expect_eq(VML.get_id_type("game:units.peasant"), "unit", "T33 get_id_type"):
		failed = true
	if not VMLTestUtil.expect(VML.list_ids_by_type("unit").has("game:units.peasant"),
			"T33 list_ids_by_type"):
		failed = true
	if not VMLTestUtil.expect(VML.reserve("mygame:future.item"), "T33 reserve id"):
		failed = true
	if not VMLTestUtil.expect(VML.has("mygame:future.item"), "T33 reserved id is visible"):
		failed = true
	if not VMLTestUtil.expect(VML.unreserve("mygame:future.item"), "T33 unreserve id"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("mygame:future.item"), "T33 unreserved id gone"):
		failed = true

	# --- M5: declarative hooks + mod_main entry ---
	VML.finish_startup() # instantiates sample_mod's mod_main, which registers hooks

	# T25: invoke chain rewrites the value and returns it.
	var dmg = VML.invoke_hook("game:modify_damage", [10, "sword"], 10)
	if not VMLTestUtil.expect_eq(dmg, 20.0, "T25 invoke_hook chain doubles damage"):
		failed = true

	# T26: check predicate intercepted by a mod handler.
	if not VMLTestUtil.expect(VML.check_hook("game:can_open_door", ["gate"]) == false,
			"T26 check_hook intercepted by mod"):
		failed = true

	# T27: registered hooks visible and attributed to the declaring mod.
	var hooks = VML.list_hooks("game:")
	if not VMLTestUtil.expect(hooks.has("game:modify_damage")
			and hooks["game:modify_damage"]["mods"].has("mymod"),
			"T27 hook registered and attributed to mymod"):
		failed = true

	# T24: emit broadcast reaches a handler, then remove_hook cleans up.
	_test_event_fired = false
	if not VMLTestUtil.expect(VML.add_hook("test:event", _on_test_event), "T24 add_hook"):
		failed = true
	VML.emit_hook("test:event", ["hello"])
	if not VMLTestUtil.expect(_test_event_fired, "T24 emit_hook fires handler"):
		failed = true
	if not VMLTestUtil.expect(VML.remove_hook("test:event", _on_test_event), "T24 remove_hook"):
		failed = true
	if not VMLTestUtil.expect(VML.list_hooks("test:").is_empty(), "T24 hooks cleaned after remove"):
		failed = true

	# T28: mod_loaded fired when mod_main was instantiated.
	if not VMLTestUtil.expect(_mod_loaded_ids.has("mymod"), "T28 mod_loaded fired for mymod"):
		failed = true

	# --- M6: mod lifecycle (zip install + dynamic load/unload) ---
	# T11: install a zip mod at runtime (extracted into user://vml/mods).
	if not VMLTestUtil.expect(VML.install_mod_from_zip("res://mods/archer_pack.zip") == OK,
			"T11 install_mod_from_zip returns OK"):
		failed = true
	if not VMLTestUtil.expect(VML.has("archerpack:units.ranger"), "T11 zip mod ids indexed"):
		failed = true
	if not VMLTestUtil.expect(VML.get_mod_ids().has("archerpack"), "T11 zip mod discovered"):
		failed = true

	# T15: enable/disable state transitions.
	if not VMLTestUtil.expect(VML.disable_mod("mymod"), "T15 disable_mod"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("mymod:units.archer"), "T15 disabled content removed"):
		failed = true
	if not VMLTestUtil.expect(VML.enable_mod("mymod"), "T15 re-enable_mod"):
		failed = true
	if not VMLTestUtil.expect(VML.has("mymod:units.archer"), "T15 re-enabled content back"):
		failed = true

	# T12: dynamic unload removes ids (base fallback), reload restores.
	if not VMLTestUtil.expect(VML.unload_mod("mymod"), "T12 unload_mod"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("mymod:units.archer"), "T12 unload removes ids"):
		failed = true
	if not VMLTestUtil.expect(VML.load_mod("mymod"), "T12 load_mod"):
		failed = true
	if not VMLTestUtil.expect(VML.has("mymod:units.archer"), "T12 reload restores ids"):
		failed = true

	# T17: mod_main lifecycle (loaded/unloaded signals).
	_mod_unloaded = false
	VML.mod_unloaded.connect(_on_mod_unloaded)
	VML.unload_mod("mymod")
	if not VMLTestUtil.expect(_mod_unloaded, "T17 mod_unloaded signal"):
		failed = true
	VML.load_mod("mymod")
	if not VMLTestUtil.expect(VML.is_mod_loaded("mymod"), "T17 load re-instantiates mod_main"):
		failed = true
	if not VMLTestUtil.expect(VML.is_mod_enabled("mymod"), "T17 is_mod_enabled"):
		failed = true

	# T11b: uninstall the test zip mod so it never leaks into the next run.
	if not VMLTestUtil.expect(VML.uninstall_mod("archerpack") == OK, "T11b uninstall zip mod"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("archerpack:units.ranger"), "T11b zip mod removed"):
		failed = true

	# --- M7: raw assets + vml:// router + hot reload ---
	# T8: instantiate a scene by id.
	var camp = VML.instantiate("game:scenes.camp")
	if not VMLTestUtil.expect(camp is Node, "T8 instantiate scene by id"):
		failed = true
	if camp is Node:
		camp.free()

	# T9: native vml:// resource loader returns a PackedScene.
	var via_router = load("vml://game:scenes.camp")
	if not VMLTestUtil.expect(via_router is PackedScene, "T9 load('vml://...') returns PackedScene"):
		failed = true

	# T10: raw image under user:// (no import cache) -> ImageTexture.
	var img_path := "user://vml/test_icon.png"
	var img := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	img.fill(Color(1, 0, 0, 1))
	img.save_png(img_path)
	VML.register("test:icon", img_path)
	var tex = VML.get_resource("test:icon")
	if not VMLTestUtil.expect(tex is ImageTexture, "T10 user:// png loads as ImageTexture"):
		failed = true
	VML.unregister("test:icon")

	# T13: hot reload — rewrite a mod data file, reload, observe the new value.
	var archer_path := "res://mods-unpacked/sample_mod/data/mymod/units/archer.json"
	var saved_archer: String = FileAccess.get_file_as_string(archer_path)
	FileAccess.open(archer_path, FileAccess.WRITE).store_string(
			'{"id":"mymod:units.archer","name":"Archer X","health":99,"attack":8,"speed":6}')
	VML.reload_resources([archer_path])
	var reloaded: Dictionary = VML.get_data("mymod:units.archer")
	if not VMLTestUtil.expect_eq(reloaded.get("name"), "Archer X", "T13 hot reload reflects file change"):
		failed = true
	FileAccess.open(archer_path, FileAccess.WRITE).store_string(saved_archer)
	VML.reload_resources([archer_path])
	var restored_archer: Dictionary = VML.get_data("mymod:units.archer")
	if not VMLTestUtil.expect_eq(restored_archer.get("name"), "Archer", "T13 reload restores original"):
		failed = true

	# --- M8: editor plugin panel + wizard ---
	# T29: the manager panel script instantiates (editor dock wiring).
	var panel_script = load("res://addons/vortarismodloader/mod_manager_panel.gd")
	var panel = panel_script.new() if panel_script else null
	if not VMLTestUtil.expect(panel != null, "T29 mod_manager_panel instantiates"):
		failed = true
	if panel:
		panel.free()

	# T30: the wizard's skeleton layout produces a loadable mod after rescan.
	var wiz_id := "test_wiz"
	var wiz_base := "res://mods-unpacked/" + wiz_id
	DirAccess.make_dir_recursive_absolute(wiz_base + "/data/" + wiz_id)
	FileAccess.open(wiz_base + "/manifest.json", FileAccess.WRITE).store_string(
			'{"name":"Test Wizard","namespace":"%s","version_number":"1.0.0","extra":{"godot":{}}}' % wiz_id)
	FileAccess.open(wiz_base + "/data/%s/unit.json" % wiz_id, FileAccess.WRITE).store_string(
			'{"id":"%s:unit","name":"Wiz Unit","health":10}' % wiz_id)
	VML.rescan()
	if not VMLTestUtil.expect(VML.get_mod_ids().has(wiz_id), "T30 rescan finds wizard mod"):
		failed = true
	if not VMLTestUtil.expect(VML.has(wiz_id + ":unit"), "T30 wizard mod content indexed"):
		failed = true

	# Cleanup the wizard test mod (res:// mods can't be uninstall_mod'd). The OS
	# (globalized) path is used for directory removal — res:// DirAccess directory
	# deletion is unreliable, and the wizard folder must not survive the run.
	_rmtree(ProjectSettings.globalize_path(wiz_base + "/data/%s" % wiz_id))
	DirAccess.remove_absolute(ProjectSettings.globalize_path(wiz_base + "/manifest.json"))
	_rmtree(ProjectSettings.globalize_path(wiz_base))
	VML.rescan()
	if not VMLTestUtil.expect(not VML.get_mod_ids().has(wiz_id), "T30 wizard mod removed cleanly"):
		failed = true

	# --- T32: async preload (batched across frames, progress + done signals) ---
	_async_progress_seen = false
	_database_loaded_async = false
	VML.preload_progress.connect(_on_preload_progress)
	VML.database_loaded.connect(_on_database_loaded_async)
	if not VMLTestUtil.expect(VML.preload_database_async(), "T32 async preload starts"):
		failed = true
	await process_frame
	await process_frame
	if not VMLTestUtil.expect(_async_progress_seen, "T32 preload_progress emitted"):
		failed = true
	if not VMLTestUtil.expect(_database_loaded_async, "T32 database_loaded after async"):
		failed = true
	VML.preload_progress.disconnect(_on_preload_progress)
	VML.database_loaded.disconnect(_on_database_loaded_async)

	# --- T34: hot reloader (H3 fix) is attached to the scene tree and auto-applies ---
	VML.start_hot_reload(0.05)
	if not VMLTestUtil.expect(get_root().has_node("VMLHotReloader"),
			"T34 hot reloader attached to scene root"):
		failed = true
	var hr_path := "res://mods-unpacked/sample_mod/data/mymod/units/archer.json"
	var saved34: String = FileAccess.get_file_as_string(hr_path)
	FileAccess.open(hr_path, FileAccess.WRITE).store_string(
			'{"id":"mymod:units.archer","name":"Archer Auto","health":77,"attack":8,"speed":6}')
	await create_timer(0.3).timeout
	var auto: Dictionary = VML.get_data("mymod:units.archer")
	if not VMLTestUtil.expect_eq(auto.get("name"), "Archer Auto",
			"T34 auto hot reload applies file change"):
		failed = true
	FileAccess.open(hr_path, FileAccess.WRITE).store_string(saved34)
	VML.reload_resources([hr_path])
	await create_timer(0.2).timeout

	# --- 0.2.0: persisted content registry ---
	if not VMLTestUtil.expect(VML.set_registry_entry("mygame:mainmenu.bg",
			"res://assets/game/icons/peasant.png", "image", "menu background"),
			"T35 set_registry_entry"):
		failed = true
	var rentry: Dictionary = VML.get_registry_entry("mygame:mainmenu.bg")
	if not VMLTestUtil.expect_eq(rentry.get("type"), "image", "T35 get_registry_entry type"):
		failed = true
	if not VMLTestUtil.expect(VML.has("mygame:mainmenu.bg"), "T35 registry entry resolvable"):
		failed = true
	if not VMLTestUtil.expect(VML.save_registry("user://vml/test_registry.json") == OK,
			"T35 save_registry"):
		failed = true
	if not VMLTestUtil.expect(VML.remove_registry_entry("mygame:mainmenu.bg"), "T35 remove_registry_entry"):
		failed = true
	if not VMLTestUtil.expect(VML.load_registry("user://vml/test_registry.json") == OK,
			"T35 load_registry"):
		failed = true
	if not VMLTestUtil.expect(VML.has("mygame:mainmenu.bg"), "T35 registry reloaded"):
		failed = true
	VML.remove_registry_entry("mygame:mainmenu.bg")
	DirAccess.remove_absolute("user://vml/test_registry.json")

	# --- 0.2.0: runtime reroute ---
	VML.register("mygame:switcher", "res://data/game/units/peasant.json")
	if not VMLTestUtil.expect_eq((VML.get("mygame:switcher") as Dictionary).get("name"),
			"Peasant", "T36 base route"):
		failed = true
	if not VMLTestUtil.expect(VML.reroute("mygame:switcher",
			"res://mods-unpacked/sample_mod/data/game/units/knight.json"), "T36 reroute"):
		failed = true
	if not VMLTestUtil.expect_eq((VML.get("mygame:switcher") as Dictionary).get("name"),
			"Knight", "T36 reroute applied"):
		failed = true
	if not VMLTestUtil.expect(VML.clear_reroute("mygame:switcher"), "T36 clear_reroute"):
		failed = true
	if not VMLTestUtil.expect_eq((VML.get("mygame:switcher") as Dictionary).get("name"),
			"Peasant", "T36 reroute cleared"):
		failed = true
	VML.unregister("mygame:switcher")

	# --- 0.2.0: per-mod config ---
	if not VMLTestUtil.expect(VML.set_config("mymod", {"speed": 2.0}), "T37 set_config"):
		failed = true
	var cfg: Dictionary = VML.get_config("mymod")
	if not VMLTestUtil.expect_eq(cfg.get("speed"), 2.0, "T37 get_config"):
		failed = true

	# --- 0.2.0: data-driven scene building ---
	var built: Node = VML.build_node("game:build_test")
	if not VMLTestUtil.expect(built != null and built is Node2D, "T38 build_node root Node2D"):
		failed = true
	if built != null:
		if not VMLTestUtil.expect(built.get_child_count() == 1 and built.get_child(0) is Label,
				"T38 build_node child Label"):
			failed = true
		built.free()

	# --- 0.2.0: id-reference validation ---
	var vres: Dictionary = VML.validate()
	if not VMLTestUtil.expect(vres.get("valid") == true, "T39 validate finds no missing ids"):
		failed = true

	# --- 0.2.0: ID editor panel instantiates ---
	var id_script = load("res://addons/vortarismodloader/id_editor_panel.gd")
	var id_panel = id_script.new() if id_script else null
	if not VMLTestUtil.expect(id_panel != null, "T40 id_editor_panel instantiates"):
		failed = true
	if id_panel:
		id_panel.free()

	# --- 0.2.1: base-layer hot reload ---
	var base_path := "res://data/game/units/peasant.json"
	var saved_base: String = FileAccess.get_file_as_string(base_path)
	FileAccess.open(base_path, FileAccess.WRITE).store_string(
			'{"id":"game:units.peasant","name":"Peasant Z","health":1,"attack":1,"speed":1}')
	VML.reload_resources([base_path])
	var base_reloaded: Dictionary = VML.get_data("game:units.peasant")
	if not VMLTestUtil.expect_eq(base_reloaded.get("name"), "Peasant Z", "T41 base hot reload"):
		failed = true
	FileAccess.open(base_path, FileAccess.WRITE).store_string(saved_base)
	VML.reload_resources([base_path])
	var base_restored: Dictionary = VML.get_data("game:units.peasant")
	if not VMLTestUtil.expect_eq(base_restored.get("name"), "Peasant", "T41 base reload restores"):
		failed = true

	# --- 0.2.1: cascade enable (deps auto-enabled) ---
	VML.disable_mod("mymod")
	VML.disable_mod("mylib") # mymod disabled, so mylib (its dep) can disable
	if not VMLTestUtil.expect(not VML.is_mod_enabled("mylib"), "T42 mylib disabled"):
		failed = true
	# Enabling mymod must cascade-enable its dependency mylib.
	if not VMLTestUtil.expect(VML.enable_mod("mymod"), "T42 enable mymod cascades deps"):
		failed = true
	if not VMLTestUtil.expect(VML.is_mod_enabled("mylib"), "T42 dependency auto-enabled"):
		failed = true
	VML.enable_mod("mylib")

	# --- 0.2.1: cascade disable (dependents go off too) ---
	VML.enable_mod("mymod")
	VML.enable_mod("mylib")
	if not VMLTestUtil.expect(VML.disable_mod("mylib"), "T43 cascade disable mylib"):
		failed = true
	if not VMLTestUtil.expect(not VML.is_mod_enabled("mylib") and not VML.is_mod_enabled("mymod"),
			"T43 dependent mymod also disabled"):
		failed = true
	if not VMLTestUtil.expect(VML.enable_mod("mylib") and VML.is_mod_enabled("mylib"),
			"T43 mylib re-enable after cascade"):
		failed = true
	VML.enable_mod("mymod")

	# --- 0.2.1: dependency status query (enable-confirmation UI) ---
	VML.disable_mod("mylib")
	VML.disable_mod("mymod")
	var deps := VML.get_mod_dependencies("mymod")
	if not VMLTestUtil.expect(deps.has("mylib") and deps["mylib"].get("exists") == true,
			"T44 mymod deps reported"):
		failed = true
	if not VMLTestUtil.expect(deps["mylib"].get("enabled") == false, "T44 dep flagged disabled"):
		failed = true
	if not VMLTestUtil.expect(VML.get_mod_dependencies("nonexistent_mod").is_empty(),
			"T44 unknown mod has no deps"):
		failed = true
	VML.enable_mod("mymod")

	# --- 0.2.1: pure-data mod must not report spurious errors (no "(!)" in the UI) ---
	VML.enable_mod("mylib")
	if not VMLTestUtil.expect(VML.get_mod_errors("mylib").is_empty(),
			"T45 pure-data mod has no errors"):
		failed = true

	# --- 0.2.2: has() unifies the DB; has_data() queries it ---
	VML.set_data("game:units.peasant", {"name": "DB Peasant"})
	if not VMLTestUtil.expect(VML.has("game:units.peasant"), "T46 has() true for DB entry"):
		failed = true
	if not VMLTestUtil.expect(VML.has_data("game:units.peasant"), "T46 has_data() true after set_data"):
		failed = true
	VML.delete_data("game:units.peasant")
	if not VMLTestUtil.expect(not VML.has_data("game:units.peasant"), "T46 has_data() false after delete_data"):
		failed = true
	if not VMLTestUtil.expect(VML.has("game:units.peasant"), "T46 has() still true (file provider)"):
		failed = true

	# --- 0.2.2: register_id(id, value, priority) + unregister ---
	if not VMLTestUtil.expect(VML.register_id("test:val", {"k": "v1"}, 0), "T47 register_id value"):
		failed = true
	if not VMLTestUtil.expect(VML.has("test:val"), "T47 value id visible"):
		failed = true
	var v47: Dictionary = VML.get_data("test:val")
	if not VMLTestUtil.expect_eq(v47.get("k"), "v1", "T47 get_data returns value provider"):
		failed = true
	if not VMLTestUtil.expect_eq(VML.get_id_info("test:val").get("data_type"), "value",
			"T47 value provider data_type"):
		failed = true
	# Higher priority overrides the lower one.
	if not VMLTestUtil.expect(VML.register_id("test:val", {"k": "v2"}, 10), "T47 register_id priority"):
		failed = true
	var v47b: Dictionary = VML.get_data("test:val")
	if not VMLTestUtil.expect_eq(v47b.get("k"), "v2", "T47 higher priority wins"):
		failed = true
	if not VMLTestUtil.expect(VML.unregister("test:val"), "T47 unregister value provider"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("test:val"), "T47 value id gone after unregister"):
		failed = true

	# --- 0.2.2: invoke_hook_ctx mutates a context Dictionary ---
	VML.add_hook("test:ctx", _on_ctx_hook, 0)
	var ctx: Dictionary = VML.invoke_hook_ctx("test:ctx", {"amount": 1}, [10])
	if not VMLTestUtil.expect_eq(ctx.get("amount"), 11, "T48 invoke_hook_ctx mutates ctx"):
		failed = true
	if not VMLTestUtil.expect(VML.remove_hook("test:ctx", _on_ctx_hook), "T48 remove ctx hook"):
		failed = true

	# --- 0.2.2: finish_startup_auto is idempotent ---
	var before_count: int = 0
	for mid in _mod_loaded_ids:
		if mid == "mymod":
			before_count += 1
	VML.finish_startup_auto()
	await process_frame
	await process_frame
	if not VMLTestUtil.expect(VML.is_startup_done(), "T49 is_startup_done true"):
		failed = true
	var after_count: int = 0
	for mid in _mod_loaded_ids:
		if mid == "mymod":
			after_count += 1
	if not VMLTestUtil.expect_eq(after_count, before_count, "T49 finish_startup_auto no double init"):
		failed = true

	# --- 0.2.2: list_ids_in_namespace / count_ids ---
	var ns_ids := VML.list_ids_in_namespace("game")
	if not VMLTestUtil.expect(ns_ids.has("game:units.knight") and ns_ids.has("game:units.peasant"),
			"T50 list_ids_in_namespace game"):
		failed = true
	if not VMLTestUtil.expect_eq(VML.count_ids("game:units."), 2, "T50 count_ids prefix"):
		failed = true
	if not VMLTestUtil.expect(VML.count_ids() > 0, "T50 count_ids all > 0"):
		failed = true

	# --- 0.2.2: runtime dependency/incompat re-check ---
	if not VMLTestUtil.expect(VML.get_mod_ids().has("incompat_mod"),
			"T51 incompat_mod discovered"):
		failed = true
	if not VMLTestUtil.expect(not VML.is_mod_enabled("incompat_mod"), "T51 incompat_mod boot-disabled"):
		failed = true
	if not VMLTestUtil.expect(VML.get_mod_errors("incompat_mod").size() > 0,
			"T51 incompat_mod boot error surfaced"):
		failed = true
	if not VMLTestUtil.expect(VML.enable_mod("incompat_mod") == false,
			"T51 enable incompat_mod fails at runtime"):
		failed = true
	var inc_errs := VML.get_mod_errors("incompat_mod")
	if not VMLTestUtil.expect(inc_errs.has("incompatible with enabled mod 'mymod'"),
			"T51 incompat reason reported"):
		failed = true

	# --- 0.2.2: validate_mod on a mod with a bad JSON data file ---
	var v52: Dictionary = VML.validate_mod("badjson_mod")
	if not VMLTestUtil.expect(v52.get("valid") == false, "T52 badjson_mod validate invalid"):
		failed = true
	if not VMLTestUtil.expect((v52.get("errors") as Array).size() > 0,
			"T52 badjson_mod validate errors"):
		failed = true
	if not VMLTestUtil.expect(int(v52.get("checked")) >= 1, "T52 badjson_mod data checked"):
		failed = true
	if not VMLTestUtil.expect((VML.get_mod_report("badjson_mod").get("errors") as Array).size() > 0,
			"T52 get_mod_report includes badjson errors"):
		failed = true

	# --- 0.2.2: error/warning aggregation ---
	var summary: Dictionary = VML.get_errors_summary()
	if not VMLTestUtil.expect(summary.has("incompat_mod") and summary.has("badjson_mod")
			and summary.has("cycle_a"),
			"T53 get_errors_summary includes problem mods"):
		failed = true
	if not VMLTestUtil.expect(not summary.has("mylib"), "T53 mylib absent from summary"):
		failed = true
	if not VMLTestUtil.expect(not summary.has("mymod"), "T53 mymod absent from summary"):
		failed = true
	if not VMLTestUtil.expect(VML.get_mod_report("mylib").get("errors").is_empty(),
			"T53 mylib report clean"):
		failed = true

	# --- 0.2.2: get_startup_report ---
	var sr: Dictionary = VML.get_startup_report()
	if not VMLTestUtil.expect((sr.get("broken_mods") as PackedStringArray).has("badjson_mod"),
			"T54 startup report lists badjson_mod broken"):
		failed = true
	if not VMLTestUtil.expect((sr.get("broken_mods") as PackedStringArray).has("incompat_mod"),
			"T54 startup report lists incompat_mod broken"):
		failed = true
	if not VMLTestUtil.expect((sr.get("errors") as Array).size() > 0, "T54 startup report errors"):
		failed = true

	# --- 0.2.2: reload_mod re-scans + re-instantiates without duplicating hooks ---
	_mod_reloaded = false
	VML.mod_reloaded.connect(_on_mod_reloaded)
	if not VMLTestUtil.expect(VML.reload_mod("mymod"), "T55 reload_mod returns true"):
		failed = true
	if not VMLTestUtil.expect(_mod_reloaded, "T55 mod_reloaded signal fired"):
		failed = true
	if not VMLTestUtil.expect(VML.is_mod_loaded("mymod"), "T55 mod_main re-instantiated"):
		failed = true
	var hook_info55: Dictionary = VML.list_hooks("game:modify_damage")
	if not VMLTestUtil.expect_eq(hook_info55.get("game:modify_damage", {}).get("count", -1), 1,
			"T55 hook count not duplicated after reload"):
		failed = true
	VML.mod_reloaded.disconnect(_on_mod_reloaded)

	# --- 0.2.2: persist set_data → save/load round-trip at the project-level res:// path ---
	var persist_val := {"k": "persisted-value", "n": 42}
	if not VMLTestUtil.expect(VML.set_data("test:persist", persist_val, true), "T56 persist set_data"):
		failed = true
	var got56: Dictionary = VML.get_data("test:persist")
	if not VMLTestUtil.expect_eq(got56.get("k"), "persisted-value", "T56 persisted value readable"):
		failed = true
	if not VMLTestUtil.expect(FileAccess.file_exists("res://vml/registry.json"),
			"T56 project-level registry file written"):
		failed = true
	if not VMLTestUtil.expect(VML.remove_registry_entry("test:persist"), "T56 remove persisted entry"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("test:persist"), "T56 entry gone after remove"):
		failed = true
	if not VMLTestUtil.expect(VML.load_registry() == OK, "T56 load_registry from project path"):
		failed = true
	if not VMLTestUtil.expect(VML.has("test:persist"), "T56 entry restored after load"):
		failed = true
	var got56b: Dictionary = VML.get_data("test:persist")
	if not VMLTestUtil.expect_eq(got56b.get("k"), "persisted-value", "T56 value restored from disk"):
		failed = true
	VML.remove_registry_entry("test:persist")
	DirAccess.remove_absolute("res://vml/registry.json")
	if not VMLTestUtil.expect(not VML.has("test:persist"), "T56 cleanup removed entry"):
		failed = true

	# --- 0.2.2: export policy read/write + zip install unaffected + custom mod root ---
	if not VMLTestUtil.expect(VML.set_export_policy("external", false), "T57 set_export_policy"):
		failed = true
	var plan57: Dictionary = VML.get_mod_package_plan()
	if not VMLTestUtil.expect(plan57.get("embedded") == false and plan57.get("external") == true
			and plan57.get("scan_user_mods") == false, "T57 package plan reflects policy"):
		failed = true
	# Zip install works regardless of the scan switch (direct insertion).
	if not VMLTestUtil.expect(VML.install_mod_from_zip("res://mods/archer_pack.zip") == OK,
			"T57 zip install unaffected by scan switch"):
		failed = true
	if not VMLTestUtil.expect(VML.has("archerpack:units.ranger"), "T57 zip mod content present"):
		failed = true
	if not VMLTestUtil.expect(VML.uninstall_mod("archerpack") == OK, "T57 uninstall zip mod"):
		failed = true
	if not VMLTestUtil.expect(VML.set_export_policy("embedded", true), "T57 restore export policy"):
		failed = true
	# Custom mod root discovered after rescan.
	var extra_root := "user://vml/extra_mods"
	DirAccess.make_dir_recursive_absolute(extra_root + "/root_test/data/root_test")
	FileAccess.open(extra_root + "/root_test/manifest.json", FileAccess.WRITE).store_string(
			'{"name":"Root Test","namespace":"root_test","version_number":"1.0.0","extra":{"godot":{}}}')
	FileAccess.open(extra_root + "/root_test/data/root_test/sample.json", FileAccess.WRITE).store_string(
			'{"id":"root_test:sample","name":"Sample"}')
	if not VMLTestUtil.expect(VML.add_mod_root(extra_root), "T57 add_mod_root"):
		failed = true
	if not VMLTestUtil.expect(VML.get_mod_roots().has(extra_root), "T57 get_mod_roots includes new root"):
		failed = true
	VML.rescan()
	if not VMLTestUtil.expect(VML.get_mod_ids().has("root_test"), "T57 rescan finds custom root mod"):
		failed = true
	if not VMLTestUtil.expect(VML.has("root_test:sample"), "T57 custom root content indexed"):
		failed = true
	if not VMLTestUtil.expect(VML.remove_mod_root(extra_root), "T57 remove_mod_root"):
		failed = true
	VML.rescan()
	if not VMLTestUtil.expect(not VML.get_mod_ids().has("root_test"),
			"T57 custom root mod gone after rescan"):
		failed = true
	DirAccess.remove_absolute(extra_root)

	# --- 0.2.3: headless CLI debugging entry ---
	# T58: the thin bootstrap + impl scripts parse and load with the extension present.
	var cli_entry_script = load("res://scripts/cli_entry.gd")
	if not VMLTestUtil.expect(cli_entry_script != null, "T58 cli_entry.gd loads"):
		failed = true
	var cli_impl_script = load("res://scripts/cli_impl.gd")
	if not VMLTestUtil.expect(cli_impl_script != null, "T58 cli_impl.gd loads"):
		failed = true

	# T58b: the fresh-clone invariant — cli_entry.gd must contain no engine singleton
	# identifier. On a fresh clone without .godot/extension_list.cfg an unresolved
	# identifier is a hard parse error that would mask the guard's clear error message.
	var cli_src: String = FileAccess.get_file_as_string("res://scripts/cli_entry.gd")
	var vml_re := RegEx.new()
	vml_re.compile("\\bVML\\b")
	if not VMLTestUtil.expect(vml_re.search(cli_src) == null,
			"T58b cli_entry.gd has no standalone engine singleton identifier"):
		failed = true

	# T59: get_startup_report shape + badjson_mod listed broken.
	var sr59: Dictionary = VML.get_startup_report()
	if not VMLTestUtil.expect(sr59.has("broken_mods") and sr59.has("errors") and sr59.has("warnings"),
			"T59 startup report has broken_mods/errors/warnings"):
		failed = true
	if not VMLTestUtil.expect((sr59.get("broken_mods") as PackedStringArray).has("badjson_mod"),
			"T59 startup report lists badjson_mod broken"):
		failed = true

	# T60: validate_mod on a known good mod returns the full structure.
	var v60: Dictionary = VML.validate_mod("mymod")
	if not VMLTestUtil.expect(v60.has("valid") and v60.has("errors") and v60.has("warnings") and v60.has("checked"),
			"T60 validate_mod returns valid/errors/warnings/checked"):
		failed = true
	if not VMLTestUtil.expect(v60.get("valid") == true, "T60 mymod validates"):
		failed = true
	if not VMLTestUtil.expect(int(v60.get("checked")) >= 1, "T60 mymod data checked"):
		failed = true

	# T61: validate_mod on the broken fixture is invalid with errors.
	var v61: Dictionary = VML.validate_mod("badjson_mod")
	if not VMLTestUtil.expect(v61.get("valid") == false, "T61 badjson_mod invalid"):
		failed = true
	if not VMLTestUtil.expect((v61.get("errors") as Array).size() > 0, "T61 badjson_mod errors"):
		failed = true

	# T62: the CLI command functions (static, exercised via direct API) return the
	# documented exit codes without simulating a full CLI subprocess.
	if not VMLTestUtil.expect(cli_impl_script.cmd_report() == 0, "T62 cmd_report exit 0"):
		failed = true
	if not VMLTestUtil.expect(cli_impl_script.cmd_list() == 0, "T62 cmd_list exit 0"):
		failed = true
	if not VMLTestUtil.expect(cli_impl_script.cmd_validate("mymod") == 0, "T62 cmd_validate mymod exit 0"):
		failed = true
	if not VMLTestUtil.expect(cli_impl_script.cmd_validate("badjson_mod") == 1, "T62 cmd_validate badjson exit 1"):
		failed = true
	if not VMLTestUtil.expect(cli_impl_script.cmd_validate("nonexistent_mod") == 1,
			"T62 cmd_validate unknown exit 1"):
		failed = true
	if not VMLTestUtil.expect(cli_impl_script.cmd_get("game:units.knight") == 0, "T62 cmd_get found exit 0"):
		failed = true
	if not VMLTestUtil.expect(cli_impl_script.cmd_get("nope:missing") == 1, "T62 cmd_get missing exit 1"):
		failed = true
	if not VMLTestUtil.expect(cli_impl_script.cmd_install("res://mods/missing.zip") == 1,
			"T62 cmd_install bad zip exit 1"):
		failed = true
	if not VMLTestUtil.expect(cli_impl_script.cmd_install("res://mods/archer_pack.zip") == 0,
			"T62 cmd_install archerpack exit 0"):
		failed = true
	if not VMLTestUtil.expect(VML.uninstall_mod("archerpack") == OK, "T62 cleanup uninstall archerpack"):
		failed = true

	# --- 0.3.0 A1: explicit id_overrides (manifest extra.godot.id_overrides) ---
	if not VMLTestUtil.expect(VML.has("game:elite.archer"), "T63 id_override indexed"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("override_mod:units.archer"),
			"T63 id_override replaces path inference for the file"):
		failed = true
	var info63: Dictionary = VML.get_id_info("game:elite.archer")
	if not VMLTestUtil.expect_eq(info63.get("provider_mod"), "override_mod",
			"T63 override provider is override_mod"):
		failed = true
	if not VMLTestUtil.expect((info63.get("path") as String).begins_with("res://mods-unpacked/override_mod/"),
			"T63 override resolves to the override_mod file"):
		failed = true
	if not VMLTestUtil.expect_eq(info63.get("explicit"), true, "T63 override provider is explicit"):
		failed = true
	var provs63: Array = VML.list_providers("game:elite.archer").get("providers", [])
	if not VMLTestUtil.expect_eq(provs63.size(), 2, "T63 two files map to the same id (arbitrated)"):
		failed = true
	var elite63: Dictionary = VML.get_data("game:elite.archer")
	if not VMLTestUtil.expect_eq(elite63.get("name"), "Elite Archer", "T63 override data readable"):
		failed = true

	# --- 0.3.0 A2: show_error_dialogs setting + console error summary ---
	if not VMLTestUtil.expect(
			ProjectSettings.get_setting("vortarismodloader/show_error_dialogs", false) == false,
			"T66 show_error_dialogs default false"):
		failed = true
	ProjectSettings.set_setting("vortarismodloader/show_error_dialogs", true)
	VML.rescan() # headless: no dialog, but the console + get_error_summary carry the errors
	if not VMLTestUtil.expect(VML.get_error_summary().contains("badjson_mod"),
			"T66 error summary lists badjson_mod"):
		failed = true
	if not VMLTestUtil.expect(VML.get_error_summary().contains("incompat_mod"),
			"T66 error summary lists incompat_mod"):
		failed = true
	if not VMLTestUtil.expect(ProjectSettings.get_setting("vortarismodloader/show_error_dialogs", false) == true,
			"T66 show_error_dialogs reads true after set"):
		failed = true
	ProjectSettings.set_setting("vortarismodloader/show_error_dialogs", false)

	# --- 0.3.0 A3: advanced debug output (vortarismodloader/debug_output) ---
	ProjectSettings.set_setting("vortarismodloader/debug_output", true)
	VML.clear_debug_log()
	VML.rescan() # scan + registry + db + hooks all log [vortarismodloader][dbg]
	var dlog: PackedStringArray = VML.get_debug_log()
	var has_dbg := false
	for line in dlog:
		if line.begins_with("[vortarismodloader][dbg]"):
			has_dbg = true
			break
	if not VMLTestUtil.expect(dlog.size() > 0 and has_dbg, "T64 debug log has [vortarismodloader][dbg] lines"):
		failed = true
	ProjectSettings.set_setting("vortarismodloader/debug_output", false)
	VML.clear_debug_log()
	VML.get_data("game:units.knight")
	if not VMLTestUtil.expect(VML.get_debug_log().is_empty(), "T64 no debug log when debug_output=false"):
		failed = true

	# --- 0.3.0 A4: .pck pack mounting (content under mods/<mod_id>/) ---
	var pck_path := "res://mods/sample_pck_mod/sample_mod.pck"
	# Clean a stale pack from a previously-crashed run so this run is repeatable.
	if FileAccess.file_exists(pck_path):
		DirAccess.remove_absolute(pck_path)
	_rmtree("res://mods/sample_pck_mod")
	var pck_src := "user://vml/test_pck"
	_rmtree(pck_src)
	DirAccess.make_dir_recursive_absolute(pck_src + "/mods/sample_pck/data/sample_pck")
	FileAccess.open(pck_src + "/mods/sample_pck/manifest.json", FileAccess.WRITE).store_string(
			'{"name":"Sample Pck","namespace":"sample_pck","version_number":"1.0.0","extra":{"godot":{}}}')
	FileAccess.open(pck_src + "/mods/sample_pck/data/sample_pck/unit.json", FileAccess.WRITE).store_string(
			'{"id":"sample_pck:unit","name":"Pck Unit","health":5}')
	DirAccess.make_dir_recursive_absolute(pck_path.get_base_dir())
	var pk := PCKPacker.new()
	var pk_ok: bool = pk.pck_start(pck_path) == OK
	if pk_ok:
		pk_ok = pk.add_file("res://mods/sample_pck/manifest.json",
				pck_src + "/mods/sample_pck/manifest.json") == OK
		pk_ok = pk.add_file("res://mods/sample_pck/data/sample_pck/unit.json",
				pck_src + "/mods/sample_pck/data/sample_pck/unit.json") == OK and pk_ok
		pk_ok = pk.flush() == OK and pk_ok
	if not VMLTestUtil.expect(pk_ok, "T65 PCKPacker builds the sample pck"):
		failed = true
	VML.rescan() # mounts res://mods/sample_pck_mod/sample_mod.pck and discovers sample_pck
	if not VMLTestUtil.expect(VML.get_mod_ids().has("sample_pck"), "T65 pck mod discovered"):
		failed = true
	if not VMLTestUtil.expect(VML.has("sample_pck:unit"), "T65 pck content indexed"):
		failed = true
	var pck_unit: Dictionary = VML.get_data("sample_pck:unit")
	if not VMLTestUtil.expect_eq(pck_unit.get("name"), "Pck Unit", "T65 pck data readable"):
		failed = true
	# Cleanup: remove the pack file + staging so the next run starts clean. The
	# mounted content stays in this process (load_resource_pack cannot unmount),
	# but deleting the file via the OS path (globalize) works even though res://
	# is now read-only, so the next run won't re-mount it.
	if FileAccess.file_exists(pck_path):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(pck_path))
	_rmtree(ProjectSettings.globalize_path("res://mods/sample_pck_mod"))
	_rmtree(pck_src)

	# --- 0.3.0 A5: is_mod_loaded reflects enable/disable immediately (no rescan) ---
	# mylib is a pure-data mod (no mod_main) — its "loaded" state must flip on
	# enable/disable via content_scanned, not via mod_main_instantiated.
	VML.disable_mod("mylib") # cascade-disables mymod (mymod depends on mylib)
	if not VMLTestUtil.expect(not VML.is_mod_loaded("mylib"), "T67 disabled mod not loaded"):
		failed = true
	if not VMLTestUtil.expect(VML.enable_mod("mylib"), "T67 enable mylib"):
		failed = true
	if not VMLTestUtil.expect(VML.is_mod_loaded("mylib"), "T67 pure-data mod loaded immediately after enable"):
		failed = true
	if not VMLTestUtil.expect(VML.disable_mod("mylib"), "T67 disable mylib"):
		failed = true
	if not VMLTestUtil.expect(not VML.is_mod_loaded("mylib"), "T67 unloaded immediately after disable"):
		failed = true
	if not VMLTestUtil.expect(VML.enable_mod("mymod"), "T67 re-enable mymod (cascades mylib)"):
		failed = true
	if not VMLTestUtil.expect(VML.is_mod_loaded("mymod") and VML.is_mod_loaded("mylib"),
			"T67 both re-enabled and loaded"):
		failed = true

	# --- 0.3.0 B5: user-defined mod order (main-screen drag-to-reorder) ---
	# mylib is a required dep of mymod, so it must precede mymod in any valid order.
	var lib_prio := VML.get_mod_priority("mylib")
	var mod_prio := VML.get_mod_priority("mymod")
	if not VMLTestUtil.expect(lib_prio >= 0 and mod_prio > lib_prio,
			"T68 load-order priorities (mylib before mymod)"):
		failed = true
	# Reordering a mod before its own dependency is rejected.
	if not VMLTestUtil.expect(VML.set_mod_order(["mymod", "mylib"]) == false,
			"T68 set_mod_order rejects dep-after-dependent"):
		failed = true
	if not VMLTestUtil.expect(VML.set_mod_order(["mylib", "mymod"]), "T68 set_mod_order valid order"):
		failed = true
	if not VMLTestUtil.expect(VML.get_mod_order().has("mylib") and VML.get_mod_order().has("mymod"),
			"T68 get_mod_order returns persisted order"):
		failed = true
	VML.rescan() # the persisted order must survive a full re-discovery
	if not VMLTestUtil.expect(VML.get_load_order().find("mylib") < VML.get_load_order().find("mymod"),
			"T68 custom order re-applied across rescan"):
		failed = true
	# Reset so later runs (and the priority-sensitive T5/T67 checks) stay deterministic.
	DirAccess.remove_absolute("user://vml/load_order.json")
	VML.rescan()
	if not VMLTestUtil.expect(VML.get_load_order().find("mylib") < VML.get_load_order().find("mymod"),
			"T68 default order restored after clearing custom order"):
		failed = true

	# --- 0.3.0 B6: ID placeholder system ---
	# Resource placeholder -> load("vml://...") / get_resource resolve the default.
	var ph_img := "user://vml/ph_icon.png"
	var ph_img_obj := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	ph_img_obj.fill(Color(0, 1, 0, 1))
	ph_img_obj.save_png(ph_img)
	if not VMLTestUtil.expect(VML.set_placeholder("mygame:ph.icon", "image", ph_img, "placeholder icon"),
			"T71 set_placeholder image"):
		failed = true
	if not VMLTestUtil.expect(VML.get_resource("mygame:ph.icon") is ImageTexture,
			"T71 placeholder get_resource -> ImageTexture"):
		failed = true
	if not VMLTestUtil.expect(load("vml://mygame:ph.icon") is ImageTexture,
			"T71 load('vml://placeholder') resolves the default"):
		failed = true
	if not VMLTestUtil.expect(VML.has("mygame:ph.icon"), "T71 placeholder id visible"):
		failed = true
	# Data placeholder -> get_data returns the constant default.
	if not VMLTestUtil.expect(VML.set_placeholder("mygame:ph.const", "data", {"k": 7}, "constant"),
			"T71 set_placeholder data"):
		failed = true
	if not VMLTestUtil.expect_eq((VML.get_data("mygame:ph.const") as Dictionary).get("k"), 7,
			"T71 placeholder data get_data returns constant"):
		failed = true
	# get_placeholder_ids + type filter.
	var ph_ids: PackedStringArray = VML.get_placeholder_ids()
	if not VMLTestUtil.expect(ph_ids.has("mygame:ph.icon") and ph_ids.has("mygame:ph.const"),
			"T72 get_placeholder_ids lists placeholders"):
		failed = true
	if not VMLTestUtil.expect(VML.get_placeholder_ids("image").has("mygame:ph.icon"),
			"T72 placeholder type filter image"):
		failed = true
	if not VMLTestUtil.expect(not VML.get_placeholder_ids("image").has("mygame:ph.const"),
			"T72 placeholder type filter excludes data"):
		failed = true
	# Save / load round-trip preserves the placeholder flag and default.
	if not VMLTestUtil.expect(VML.save_registry("user://vml/test_ph_registry.json") == OK,
			"T73 placeholder save_registry"):
		failed = true
	if not VMLTestUtil.expect(VML.remove_registry_entry("mygame:ph.icon"), "T73 remove placeholder"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("mygame:ph.icon"), "T73 placeholder removed"):
		failed = true
	if not VMLTestUtil.expect(VML.load_registry("user://vml/test_ph_registry.json") == OK,
			"T73 placeholder load_registry"):
		failed = true
	if not VMLTestUtil.expect(VML.get_placeholder_ids().has("mygame:ph.icon"),
			"T73 placeholder flag restored after load"):
		failed = true
	if not VMLTestUtil.expect(VML.has("mygame:ph.icon"), "T73 placeholder re-resolvable after load"):
		failed = true
	# Cleanup.
	VML.remove_registry_entry("mygame:ph.icon")
	VML.remove_registry_entry("mygame:ph.const")
	DirAccess.remove_absolute("user://vml/test_ph_registry.json")
	if not VMLTestUtil.expect(not VML.has("mygame:ph.const"), "T73 placeholder cleanup"):
		failed = true

	# --- 0.3.0 F1: registry re-registration / reroute / placeholder / remove must
	# be immediate with no stale provider residue ---
	if not VMLTestUtil.expect(VML.set_registry_entry("f1:switcher", "res://data/game/units/peasant.json"),
			"F1 set_registry_entry initial"):
		failed = true
	if not VMLTestUtil.expect_eq((VML.get("f1:switcher") as Dictionary).get("name"), "Peasant",
			"F1 initial registry route"):
		failed = true
	if not VMLTestUtil.expect(VML.set_registry_entry("f1:switcher",
			"res://mods-unpacked/sample_mod/data/game/units/knight.json"),
			"F1 set_registry_entry re-register new path"):
		failed = true
	if not VMLTestUtil.expect_eq((VML.get("f1:switcher") as Dictionary).get("name"), "Knight",
			"F1 re-registered path wins immediately (no stale provider)"):
		failed = true
	if not VMLTestUtil.expect_eq(VML.get_id_info("f1:switcher").get("path"),
			"res://mods-unpacked/sample_mod/data/game/units/knight.json",
			"F1 get_id_info resolves the new path"):
		failed = true
	if not VMLTestUtil.expect(VML.remove_registry_entry("f1:switcher"), "F1 remove_registry_entry"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("f1:switcher"),
			"F1 has() false after remove (no stale provider)"):
		failed = true
	# Reroute twice: the second must win (old __reroute__ replaced, not stacked).
	if not VMLTestUtil.expect(VML.register("f1:rt", "res://data/game/units/peasant.json"),
			"F1 register for reroute"):
		failed = true
	if not VMLTestUtil.expect(VML.reroute("f1:rt",
			"res://mods-unpacked/sample_mod/data/game/units/knight.json"), "F1 reroute 1"):
		failed = true
	if not VMLTestUtil.expect(VML.reroute("f1:rt",
			"res://mods-unpacked/override_mod/data/override_mod/units/archer.json"), "F1 reroute 2"):
		failed = true
	if not VMLTestUtil.expect_eq((VML.get("f1:rt") as Dictionary).get("name"), "Elite Archer",
			"F1 second reroute wins (no stacked reroute)"):
		failed = true
	if not VMLTestUtil.expect(VML.clear_reroute("f1:rt"), "F1 clear_reroute"):
		failed = true
	if not VMLTestUtil.expect_eq((VML.get("f1:rt") as Dictionary).get("name"), "Peasant",
			"F1 clear_reroute restores original"):
		failed = true
	VML.unregister("f1:rt")
	# Placeholder type change: data -> image must replace the old provider.
	if not VMLTestUtil.expect(VML.set_placeholder("f1:ph", "data", {"k": 1}, "p1"),
			"F1 set_placeholder data"):
		failed = true
	if not VMLTestUtil.expect_eq((VML.get_data("f1:ph") as Dictionary).get("k"), 1,
			"F1 data placeholder"):
		failed = true
	var f1_ph_img := "user://vml/f1_ph.png"
	var f1_img := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	f1_img.fill(Color(0, 0, 1, 1))
	f1_img.save_png(f1_ph_img)
	if not VMLTestUtil.expect(VML.set_placeholder("f1:ph", "image", f1_ph_img, "p2"),
			"F1 set_placeholder change type to image"):
		failed = true
	if not VMLTestUtil.expect(VML.get_resource("f1:ph") is ImageTexture,
			"F1 placeholder type change takes effect (old data provider gone)"):
		failed = true
	VML.remove_registry_entry("f1:ph")
	DirAccess.remove_absolute(f1_ph_img)

	# --- 0.3.0 F2: manifestless pck mod must honour the export_mods policy ---
	var f2_root := "user://vml/f2_pck_root"
	_rmtree(f2_root)
	DirAccess.make_dir_recursive_absolute(f2_root + "/stage/mods/f2_nomani/data/f2_nomani")
	FileAccess.open(f2_root + "/stage/mods/f2_nomani/data/f2_nomani/unit.json", FileAccess.WRITE).store_string(
			'{"id":"f2_nomani:unit","name":"F2 NoManifest","health":3}')
	var f2_pck := f2_root + "/f2_nomani.pck"
	if FileAccess.file_exists(f2_pck):
		DirAccess.remove_absolute(f2_pck)
	var f2_packer := PCKPacker.new()
	var f2_ok: bool = f2_packer.pck_start(f2_pck) == OK
	if f2_ok:
		f2_ok = f2_packer.add_file("res://mods/f2_nomani/data/f2_nomani/unit.json",
				f2_root + "/stage/mods/f2_nomani/data/f2_nomani/unit.json") == OK and f2_ok
		f2_ok = f2_packer.flush() == OK and f2_ok
	if not VMLTestUtil.expect(f2_ok, "F2 PCKPacker builds manifestless pck"):
		failed = true
	if not VMLTestUtil.expect(VML.add_mod_root(f2_root), "F2 add pck staging root"):
		failed = true
	VML.set_export_policy("none", true)
	VML.rescan()
	if not VMLTestUtil.expect(not VML.get_mod_ids().has("f2_nomani"),
			"F2 export_mods=none skips manifestless pck mod"):
		failed = true
	VML.set_export_policy("external", true)
	VML.rescan()
	if not VMLTestUtil.expect(not VML.get_mod_ids().has("f2_nomani"),
			"F2 export_mods=external skips embedded res:// pck root"):
		failed = true
	VML.set_export_policy("embedded", true)
	VML.rescan()
	if not VMLTestUtil.expect(VML.get_mod_ids().has("f2_nomani"),
			"F2 export_mods=embedded registers manifestless pck mod"):
		failed = true
	if not VMLTestUtil.expect(VML.has("f2_nomani:unit"), "F2 manifestless pck content indexed"):
		failed = true
	VML.remove_mod_root(f2_root)
	# Keep f2_root (and the pck file) on disk for the rest of this session: the pack
	# is already mounted in the VFS and cannot be unmounted, and deleting the file
	# would make its virtual content unreadable on later rescans. Final cleanup runs
	# at the end of the suite.

	# --- 0.3.0 F3: set_mod_order partial list must not violate dependencies ---
	if not VMLTestUtil.expect(VML.set_mod_order(["mymod"]) == false,
			"F3 set_mod_order rejects partial list (dep mylib unlisted)"):
		failed = true
	if not VMLTestUtil.expect(VML.get_mod_order().is_empty(), "F3 rejected order not persisted"):
		failed = true
	if not VMLTestUtil.expect(VML.get_load_order().find("mylib") < VML.get_load_order().find("mymod"),
			"F3 load order unchanged after rejection"):
		failed = true
	if not VMLTestUtil.expect(VML.set_mod_order(["mylib"]), "F3 set_mod_order partial dep-only list"):
		failed = true
	if not VMLTestUtil.expect(VML.get_load_order().find("mylib") < VML.get_load_order().find("mymod"),
			"F3 dep-only custom order still keeps mylib before mymod"):
		failed = true
	# A persisted bad file (written directly) falls back to the default order.
	DirAccess.remove_absolute("user://vml/load_order.json")
	var f3_bad := ["mymod"] # mymod before its dep mylib — invalid
	FileAccess.open("user://vml/load_order.json", FileAccess.WRITE).store_string(JSON.stringify(f3_bad))
	VML.rescan()
	if not VMLTestUtil.expect(VML.get_load_order().find("mylib") < VML.get_load_order().find("mymod"),
			"F3 persisted bad load_order falls back to default"):
		failed = true
	if not VMLTestUtil.expect(VML.get_mod_order().is_empty(),
			"F3 bad persisted order ignored (custom_load_order empty)"):
		failed = true
	DirAccess.remove_absolute("user://vml/load_order.json")
	VML.rescan()

	# --- 0.3.0 F4: project-settings defaults must not clobber user values at startup ---
	var f4_old_dialogs: Variant = ProjectSettings.get_setting("vortarismodloader/show_error_dialogs", false)
	ProjectSettings.set_setting("vortarismodloader/show_error_dialogs", true)
	if not VMLTestUtil.expect(ProjectSettings.has_setting("vortarismodloader/show_error_dialogs"),
			"F4 show_error_dialogs registered as a project setting"):
		failed = true
	if not VMLTestUtil.expect(ProjectSettings.get_setting("vortarismodloader/show_error_dialogs", false) == true,
			"F4 a set true is not re-defaulted to false (has_setting guard)"):
		failed = true
	ProjectSettings.set_setting("vortarismodloader/show_error_dialogs", f4_old_dialogs)

	# --- 0.3.0 F5: legacy user://vml/mods mods get a one-time migration notice ---
	DirAccess.remove_absolute("user://vml/.legacy_mods_migration_notified")
	var f5_legacy := "user://vml/mods/f5_legacy"
	_rmtree(f5_legacy)
	DirAccess.make_dir_recursive_absolute(f5_legacy + "/data/f5_legacy")
	FileAccess.open(f5_legacy + "/manifest.json", FileAccess.WRITE).store_string(
			'{"name":"F5 Legacy","namespace":"f5_legacy","version_number":"1.0.0","extra":{"godot":{}}}')
	FileAccess.open(f5_legacy + "/data/f5_legacy/unit.json", FileAccess.WRITE).store_string(
			'{"id":"f5_legacy:unit","name":"Legacy","health":1}')
	VML.rescan()
	if not VMLTestUtil.expect(VML.get_legacy_mod_migration_notice().length() > 0,
			"F5 legacy user://vml/mods mod triggers migration notice"):
		failed = true
	if not VMLTestUtil.expect(not VML.get_mod_ids().has("f5_legacy"),
			"F5 legacy user:// mod not auto-discovered (default roots unchanged)"):
		failed = true
	VML.rescan()
	if not VMLTestUtil.expect(VML.get_legacy_mod_migration_notice().is_empty(),
			"F5 migration notice is one-time per session"):
		failed = true
	VML.add_mod_root("user://vml/mods")
	VML.rescan()
	if not VMLTestUtil.expect(VML.get_mod_ids().has("f5_legacy"),
			"F5 legacy mod discovered after add_mod_root(user://vml/mods)"):
		failed = true
	if not VMLTestUtil.expect(VML.has("f5_legacy:unit"), "F5 legacy mod content indexed"):
		failed = true
	VML.remove_mod_root("user://vml/mods")
	_rmtree(f5_legacy)
	DirAccess.remove_absolute("user://vml/.legacy_mods_migration_notified")
	VML.rescan()

	# --- 0.3.0 F6: install_root() picks a writable root, never a read-only res:// ---
	# A pck mounted earlier in this suite makes res:// read-only — the exported-build
	# condition this fix targets. Add a writable custom absolute-path root and make
	# sure a zip install lands there (not in read-only res://mods).
	var f6_abs := ProjectSettings.globalize_path("user://vml/f6_abs_root")
	_rmtree(f6_abs)
	DirAccess.make_dir_recursive_absolute(f6_abs)
	var f6_res_probe := "res://mods/.vml_f6_probe"
	DirAccess.make_dir_recursive_absolute(f6_res_probe)
	var f6_res_writable := DirAccess.dir_exists_absolute(f6_res_probe)
	if f6_res_writable:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(f6_res_probe))
	if not VMLTestUtil.expect(VML.add_mod_root(f6_abs), "F6 add custom absolute-path root"):
		failed = true
	if not VMLTestUtil.expect(VML.install_mod_from_zip("res://mods/archer_pack.zip") == OK,
			"F6 zip install succeeds via a writable root"):
		failed = true
	var f6_root: String = VML.get_mod_path("archerpack")
	if f6_res_writable:
		if not VMLTestUtil.expect(f6_root.begins_with("res://mods/"),
				"F6 writable res:// root selected first"):
			failed = true
	else:
		if not VMLTestUtil.expect(f6_root.begins_with(f6_abs + "/"),
				"F6 read-only res:// skipped; writable custom root selected (got " + f6_root + ")"):
			failed = true
	if not VMLTestUtil.expect(VML.uninstall_mod("archerpack") == OK, "F6 cleanup uninstall archerpack"):
		failed = true
	if not VMLTestUtil.expect(VML.remove_mod_root(f6_abs), "F6 cleanup remove custom root"):
		failed = true
	_rmtree(f6_abs)

	# --- 0.3.0 F7: deterministic winner when a mod maps two files to one id ---
	var f7_best: String = VML.get_id_info("game:elite.archer").get("path", "")
	var f7_is_override := f7_best == "res://mods-unpacked/override_mod/data/override_mod/units/archer.json" \
			or f7_best == "res://mods-unpacked/override_mod/data/override_mod/units/archer_b.json"
	if not VMLTestUtil.expect(f7_is_override, "F7 same-mod multi-file winner is one of the mapped files"):
		failed = true
	var f7_list1: Array = VML.list_providers("game:elite.archer").get("providers", [])
	if not VMLTestUtil.expect_eq(f7_list1.size(), 2, "F7 two providers (multi-file id_override)"):
		failed = true
	# A re-scan re-sorts the same providers; the winning path must stay deterministic
	# (a stable total order, never an unspecified std::sort permutation).
	VML.reload_mod("override_mod")
	var f7_list2: Array = VML.list_providers("game:elite.archer").get("providers", [])
	if not VMLTestUtil.expect_eq(f7_list2.size(), 2, "F7 providers stable after reload_mod"):
		failed = true
	var f7_best2: String = VML.get_id_info("game:elite.archer").get("path", "")
	if not VMLTestUtil.expect_eq(f7_best2, f7_best, "F7 winner deterministic across re-scan"):
		failed = true

	# --- 0.3.0 F8: error dialog lifecycle (headless-safe, debounced) ---
	ProjectSettings.set_setting("vortarismodloader/show_error_dialogs", true)
	VML.rescan()
	VML.rescan() # repeated rescans with the same errors must not stack dialogs
	var f8_dlg_count := 0
	for child in get_root().get_children():
		if child is AcceptDialog:
			f8_dlg_count += 1
	if not VMLTestUtil.expect_eq(f8_dlg_count, 0, "F8 no error dialogs accumulate headless"):
		failed = true
	if not VMLTestUtil.expect(VML.get_error_summary().length() > 0, "F8 error summary still available"):
		failed = true
	ProjectSettings.set_setting("vortarismodloader/show_error_dialogs", false)

	# Final F2 cleanup (the manifestless pck file was kept readable above).
	_rmtree("user://vml/f2_pck_root")

	quit(1 if failed else 0)
