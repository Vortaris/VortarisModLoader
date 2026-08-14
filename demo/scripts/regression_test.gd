extends SceneTree
## Headless regression suite (M2: routing layer). Run:
##   godot --headless --path demo --script res://scripts/regression_test.gd
## Exit code 0 = all tests pass.

func _initialize() -> void:
	var failed := false

	# --- T0: singleton ---
	failed = VMLTestUtil.expect(Engine.has_singleton("VML"), "T0 VML singleton exists") or failed

	# --- T1: id lookup by implicit base scan ---
	failed = VMLTestUtil.expect(VML.has("game:units/peasant"), "T1 base id has game:units/peasant") or failed
	failed = VMLTestUtil.expect(VML.has("game:recipes"), "T1 base id has game:recipes") or failed
	failed = VMLTestUtil.expect(not VML.has("nope:missing"), "T1 unknown id reports false") or failed

	# --- T2: get_data returns parsed Dictionary ---
	var peasant = VML.get_data("game:units/peasant")
	failed = VMLTestUtil.expect(peasant is Dictionary, "T2 get_data returns Dictionary") or failed
	if peasant is Dictionary:
		failed = VMLTestUtil.expect_eq(peasant.get("name"), "Peasant", "T2 unit name") or failed
		failed = VMLTestUtil.expect_eq(peasant.get("health"), 50, "T2 unit health") or failed

	# --- T3: explicit register/unregister ---
	failed = VMLTestUtil.expect(VML.register("mymod:custom", "res://data/game/units/peasant.json"),
			"T3 register explicit id") or failed
	failed = VMLTestUtil.expect(VML.has("mymod:custom"), "T3 registered id is visible") or failed
	failed = VMLTestUtil.expect(VML.unregister("mymod:custom"), "T3 unregister") or failed
	failed = VMLTestUtil.expect(not VML.has("mymod:custom"), "T3 unregistered id gone") or failed

	# --- T3b: resolve gives the physical path ---
	failed = VMLTestUtil.expect_eq(VML.resolve("game:units/knight"), "res://data/game/units/knight.json",
			"T3b resolve physical path") or failed

	# --- T3c: listing ---
	failed = VMLTestUtil.expect(VML.list_namespaces().has("game"), "T3c namespaces include game") or failed
	var ids = VML.list_ids("game:units/")
	failed = VMLTestUtil.expect(ids.has("game") and ids["game"].has("units/knight"),
			"T3d list_ids prefix filter") or failed

	quit(1 if failed else 0)
