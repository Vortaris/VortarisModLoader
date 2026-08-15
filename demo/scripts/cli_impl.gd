extends RefCounted

# CLI implementation for cli_entry.gd. Kept in a separate file — and only loaded
# after cli_entry.gd's guard passes — so that cli_entry.gd itself never mentions
# the engine singleton at parse time. On a fresh clone the GDExtension cache
# (.godot/extension_list.cfg) does not exist, the plugin classes are absent, and
# an unresolved identifier is a hard GDScript parse error that aborts before the
# entry point's clear error message can be printed.

const USAGE := "[vortarismodloader] usage:\n" \
	+ "  --vortaris-vml-report             print the startup report (broken mods / errors /\n" \
	+ "                                    warnings) plus the discovered mod id list\n" \
	+ "  --vortaris-vml-validate <mod_id>  validate one mod; print { valid, errors, warnings,\n" \
	+ "                                    checked }; exit 0 = valid, 1 = invalid/unknown\n" \
	+ "  --vortaris-vml-list               list discovered mods (id / enabled / dependencies) +\n" \
	+ "                                    load order + namespaces\n" \
	+ "  --vortaris-vml-get <id>           print the parsed value and id info (source / priority /\n" \
	+ "                                    explicit) for one content id; exit 1 if not found\n" \
	+ "  --vortaris-vml-install <zip>      transactionally install a zip mod (dev only, into the\n" \
	+ "                                    first writable mod root, res://mods in dev) and print the result\n" \
	+ "Examples:\n" \
	+ "  godot --headless --path demo --script res://scripts/cli_entry.gd -- --vortaris-vml-report\n" \
	+ "  godot --headless --path demo --script res://scripts/cli_entry.gd -- \\\n" \
	+ "      --vortaris-vml-validate mymod"


# The engine singleton scans all mods in its constructor (SCENE init), before
# this script runs. finish_startup() loads the persisted registry, runs the
# startup data validation (which populates get_startup_report) and instantiates
# every enabled mod's mod_main — exactly what demo/scripts/main.gd does. It is
# idempotent (an internal startup_done guard), so repeated CLI invocations never
# double-scan or double-instantiate mod_mains.
static func _ensure_ready() -> void:
	if not VML.is_startup_done():
		VML.finish_startup()


# --vortaris-vml-report: startup aggregate + mod id list. Exit 0.
static func cmd_report() -> int:
	_ensure_ready()
	print("[vortarismodloader] report")
	var sr: Dictionary = VML.get_startup_report()
	var broken: PackedStringArray = sr.get("broken_mods", PackedStringArray())
	var errors: Array = sr.get("errors", [])
	var warnings: Array = sr.get("warnings", [])
	print("[vortarismodloader]   broken_mods (", broken.size(), "): ", broken)
	for e in errors:
		print("[vortarismodloader]     error: ", e)
	for w in warnings:
		print("[vortarismodloader]     warning: ", w)
	print("[vortarismodloader]   mod ids (", VML.get_mod_ids().size(), "): ", VML.get_mod_ids())
	print("[vortarismodloader]   load_order: ", VML.get_load_order())
	print("[vortarismodloader] report done")
	return 0


# --vortaris-vml-validate <mod_id>: validate one mod. Exit 0 = valid, 1 = invalid/unknown.
static func cmd_validate(mod_id: String) -> int:
	_ensure_ready()
	print("[vortarismodloader] validate ", mod_id)
	var r: Dictionary = VML.validate_mod(mod_id)
	print("[vortarismodloader]   valid: ", r.get("valid"))
	print("[vortarismodloader]   checked: ", r.get("checked"))
	var errors: Array = r.get("errors", [])
	var warnings: Array = r.get("warnings", [])
	print("[vortarismodloader]   errors (", errors.size(), "): ", errors)
	print("[vortarismodloader]   warnings (", warnings.size(), "): ", warnings)
	if r.get("valid") == true:
		print("[vortarismodloader] validate OK")
		return 0
	print("[vortarismodloader] validate FAILED")
	return 1


# --vortaris-vml-list: discovered mods with state + load order + namespaces. Exit 0.
static func cmd_list() -> int:
	_ensure_ready()
	print("[vortarismodloader] list")
	var mods: PackedStringArray = VML.get_mod_ids()
	for mod_id in mods:
		var enabled: bool = VML.is_mod_enabled(mod_id)
		var loaded: bool = VML.is_mod_loaded(mod_id)
		var deps: Dictionary = VML.get_mod_dependencies(mod_id)
		# A mod id IS its content namespace; report it explicitly for clarity.
		print("[vortarismodloader]   - ", mod_id, " namespace='", mod_id,
				"' enabled=", enabled, " loaded=", loaded, " deps=", deps.keys())
	print("[vortarismodloader]   load_order: ", VML.get_load_order())
	print("[vortarismodloader]   namespaces: ", VML.list_namespaces())
	print("[vortarismodloader] list done")
	return 0


# --vortaris-vml-get <id>: parsed value + id info. Exit 0 = found, 1 = not found.
static func cmd_get(id: String) -> int:
	_ensure_ready()
	print("[vortarismodloader] get ", id)
	var info: Dictionary = VML.get_id_info(id)
	if not VML.has(id):
		print("[vortarismodloader]   ERROR: id not found: ", id)
		print("[vortarismodloader]   id_info: ", JSON.stringify(info))
		return 1
	var data: Variant = VML.get_data(id)
	if data is Dictionary or data is Array:
		print("[vortarismodloader]   value: ", JSON.stringify(data))
	else:
		print("[vortarismodloader]   value: ", str(data))
	print("[vortarismodloader]   id_info:")
	print("[vortarismodloader]     source: ", info.get("provider_mod", ""))
	print("[vortarismodloader]     priority: ", info.get("priority", -1))
	print("[vortarismodloader]     explicit: ", info.get("explicit", false))
	print("[vortarismodloader]     path: ", info.get("path", ""))
	print("[vortarismodloader]     data_type: ", info.get("data_type", ""))
	print("[vortarismodloader]     preloaded: ", info.get("preloaded", false))
	print("[vortarismodloader] get done")
	return 0


# --vortaris-vml-install <zip>: transactional zip install. Exit 0 = installed, 1 = failure.
static func cmd_install(zip_path: String) -> int:
	_ensure_ready()
	var before: PackedStringArray = VML.get_mod_ids()
	print("[vortarismodloader] install ", zip_path)
	var err: int = VML.install_mod_from_zip(zip_path)
	if err == OK:
		var added: PackedStringArray = []
		for m in VML.get_mod_ids():
			if not before.has(m):
				added.append(m)
		print("[vortarismodloader]   OK: mod installed (new mods: ", added, ")")
		print("[vortarismodloader]   mod ids: ", VML.get_mod_ids())
		return 0
	print("[vortarismodloader]   ERROR: install failed, error code ", err)
	return 1
