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
	if not VMLTestUtil.expect(VML.has("game:units/peasant"), "T1 base id has game:units/peasant"):
		failed = true
	if not VMLTestUtil.expect(VML.has("game:recipes"), "T1 base id has game:recipes"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("nope:missing"), "T1 unknown id reports false"):
		failed = true

	# --- T2: get_data returns parsed Dictionary ---
	var peasant = VML.get_data("game:units/peasant")
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
	if not VMLTestUtil.expect_eq(VML.resolve("game:units/peasant"), "res://data/game/units/peasant.json",
			"T3b resolve physical path"):
		failed = true

	# --- T3c/d: listing ---
	if not VMLTestUtil.expect(VML.list_namespaces().has("game"), "T3c namespaces include game"):
		failed = true
	var ids = VML.list_ids("game:units/")
	if not VMLTestUtil.expect(ids.has("game") and ids["game"].has("units/knight"),
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
	var knight: Dictionary = VML.get_data("game:units/knight")
	if not VMLTestUtil.expect_eq(knight.get("attack"), 15,
			"T5 mod overrides base knight (sample_mod wins over lib_mod)"):
		failed = true
	if not VMLTestUtil.expect(VML.resolve("game:units/knight").begins_with(
			"res://mods-unpacked/sample_mod/"), "T5 override resolves to sample_mod"):
		failed = true

	# New content from a mod namespace.
	if not VMLTestUtil.expect(VML.has("mymod:units/archer"), "T4 mod adds new id"):
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
	if not VMLTestUtil.expect(by_ns.has("mymod") and by_ns["mymod"].has("units/archer"),
			"T16 list_ids grouped by namespace includes mod content"):
		failed = true

	# --- M4: unified content database ---
	if not VMLTestUtil.expect_eq(VML.get_database_mode(), "data", "T19 database_mode default data"):
		failed = true
	# Data was preloaded into memory at startup: get_all sees it without loading.
	var all_units = VML.get_all("game:units/")
	if not VMLTestUtil.expect(all_units.size() == 2, "T19 get_all prefetched data resident"):
		failed = true

	# set_data overwrites live, get_data reflects it, and the signal fires.
	_fired_ok = false
	VML.database_entry_changed.connect(_on_db_entry_changed)
	if not VMLTestUtil.expect(VML.set_data("game:units/peasant", {"name": "Modded Peasant"}),
			"T20 set_data returns true"):
		failed = true
	var modified = VML.get_data("game:units/peasant")
	if not VMLTestUtil.expect_eq(modified.get("name"), "Modded Peasant",
			"T20 get_data reflects set_data"):
		failed = true
	if not VMLTestUtil.expect(_fired_ok, "T20 database_entry_changed emitted"):
		failed = true
	VML.database_entry_changed.disconnect(_on_db_entry_changed)

	# delete_data removes the override; get_data falls back to the file.
	if not VMLTestUtil.expect(VML.delete_data("game:units/peasant"), "T21 delete_data"):
		failed = true
	var restored = VML.get_data("game:units/peasant")
	if not VMLTestUtil.expect_eq(restored.get("name"), "Peasant", "T21 get_data falls back to file"):
		failed = true

	# Prefix query over the resident database.
	var units = VML.get_all("game:units/")
	if not VMLTestUtil.expect(units.has("game:units/peasant") and units.has("game:units/knight"),
			"T22 get_all prefix filter"):
		failed = true

	# Mode switching round-trips and triggers a reload.
	if not VMLTestUtil.expect(VML.set_database_mode("off") and VML.get_database_mode() == "off",
			"T23 set_database_mode off"):
		failed = true
	if not VMLTestUtil.expect(VML.set_database_mode("data") and VML.get_database_mode() == "data",
			"T23 set_database_mode back to data"):
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
	if not VMLTestUtil.expect(VML.has("archerpack:units/ranger"), "T11 zip mod ids indexed"):
		failed = true
	if not VMLTestUtil.expect(VML.get_mod_ids().has("archerpack"), "T11 zip mod discovered"):
		failed = true

	# T15: enable/disable state transitions.
	if not VMLTestUtil.expect(VML.disable_mod("mymod"), "T15 disable_mod"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("mymod:units/archer"), "T15 disabled content removed"):
		failed = true
	if not VMLTestUtil.expect(VML.enable_mod("mymod"), "T15 re-enable_mod"):
		failed = true
	if not VMLTestUtil.expect(VML.has("mymod:units/archer"), "T15 re-enabled content back"):
		failed = true

	# T12: dynamic unload removes ids (base fallback), reload restores.
	if not VMLTestUtil.expect(VML.unload_mod("mymod"), "T12 unload_mod"):
		failed = true
	if not VMLTestUtil.expect(not VML.has("mymod:units/archer"), "T12 unload removes ids"):
		failed = true
	if not VMLTestUtil.expect(VML.load_mod("mymod"), "T12 load_mod"):
		failed = true
	if not VMLTestUtil.expect(VML.has("mymod:units/archer"), "T12 reload restores ids"):
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
	if not VMLTestUtil.expect(not VML.has("archerpack:units/ranger"), "T11b zip mod removed"):
		failed = true

	# --- M7: raw assets + vml:// router + hot reload ---
	# T8: instantiate a scene by id.
	var camp = VML.instantiate("game:scenes/camp")
	if not VMLTestUtil.expect(camp is Node, "T8 instantiate scene by id"):
		failed = true
	if camp is Node:
		camp.free()

	# T9: native vml:// resource loader returns a PackedScene.
	var via_router = load("vml://game:scenes/camp")
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
			'{"id":"mymod:units/archer","name":"Archer X","health":99,"attack":8,"speed":6}')
	VML.reload_resources([archer_path])
	var reloaded: Dictionary = VML.get_data("mymod:units/archer")
	if not VMLTestUtil.expect_eq(reloaded.get("name"), "Archer X", "T13 hot reload reflects file change"):
		failed = true
	FileAccess.open(archer_path, FileAccess.WRITE).store_string(saved_archer)
	VML.reload_resources([archer_path])
	var restored_archer: Dictionary = VML.get_data("mymod:units/archer")
	if not VMLTestUtil.expect_eq(restored_archer.get("name"), "Archer", "T13 reload restores original"):
		failed = true

	quit(1 if failed else 0)
