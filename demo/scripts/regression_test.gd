extends SceneTree
## Headless regression suite (M2–M3: routing layer + override arbitration). Run:
##   godot --headless --path demo --script res://scripts/regression_test.gd
## Exit code 0 = all tests pass.

func _initialize() -> void:
	var failed := false

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

	quit(1 if failed else 0)
