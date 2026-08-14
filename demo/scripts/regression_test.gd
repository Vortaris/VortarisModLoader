extends SceneTree
## Headless regression suite (M2–M4: routing layer + override + content database).
## Run: godot --headless --path demo --script res://scripts/regression_test.gd
## Exit code 0 = all tests pass.
##
## NOTE: signals from the VML engine singleton must be connected to named
## methods, not lambdas — a lambda held across engine shutdown crashes at exit.

var _fired_ok := false
var _test_event_fired := false
var _mod_loaded_ids: Array = []
var _mod_unloaded := false
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

func _initialize() -> void:
	var failed := false
	VML.mod_loaded.connect(_on_mod_loaded)

	# Clean up any zip mod left over from a previous run (user:// persists).
	if VML.get_mod_ids().has("archerpack"):
		VML.uninstall_mod("archerpack")

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
	if not VMLTestUtil.expect(Array(load_order) == ["mylib", "mymod"],
			"T4 dependency load order (mylib before mymod)"):
		failed = true
	if not VMLTestUtil.expect(load_order.has("cycle_a") == false,
			"T6 cyclic mod excluded from load order"):
		failed = true
	if not VMLTestUtil.expect(load_order.has("BadMod") == false,
			"T14 invalid manifest mod excluded"):
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

	# Cleanup the wizard test mod (res:// mods can't be uninstall_mod'd).
	DirAccess.remove_absolute(wiz_base + "/data/%s/unit.json" % wiz_id)
	DirAccess.remove_absolute(wiz_base + "/data/%s" % wiz_id)
	DirAccess.remove_absolute(wiz_base + "/manifest.json")
	DirAccess.remove_absolute(wiz_base)
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

	quit(1 if failed else 0)
