extends SceneTree

# Headless CLI entry point for AI / automation / CI. It loads the same
# GDExtension as the game, so every engine singleton registered by the plugin
# is available once the extension is present.
#
# One-time prerequisite (fresh clone): the extension cache
# .godot/extension_list.cfg is gitignored, so it does not exist until Godot
# scans the project once. Until then, --script mode skips
# vortarismodloader.gdextension and the plugin's classes are missing. Generate
# the cache with:
#   godot --headless --editor --import --quit --path demo
# (or open the project in the editor once).
#
# IMPORTANT: this file must never reference the engine singleton by name
# directly. On a fresh clone that identifier does not exist, and an unresolved
# identifier is a hard GDScript parse error that aborts before _init() runs —
# which would mask the guard below with a cryptic "Identifier not declared".
# The actual CLI logic lives in cli_impl.gd and is only loaded after the guard
# confirms the extension exists.
#
# Run:
#   godot --headless --path demo --script res://scripts/cli_entry.gd \
#       -- --vortaris-vml-report
#   godot --headless --path demo --script res://scripts/cli_entry.gd \
#       -- --vortaris-vml-validate mymod
#
# All VortarisModLoader arguments must come after `--` (OS.get_cmdline_user_args()).
# Output is prefixed with [vortarismodloader] so it is easy to grep / parse.

const IMPL_PATH := "res://scripts/cli_impl.gd"


func _init() -> void:
	# Fresh clones have no .godot/extension_list.cfg (gitignored), so Godot skips
	# loading vortarismodloader.gdextension and the plugin classes are missing.
	# Fail with a clear, actionable error instead of a cryptic "Identifier not
	# declared" parse error (see the one-time prerequisite note at the top).
	if not ClassDB.class_exists("VMLModLoader"):
		print("[vortarismodloader] ERROR: GDExtension not loaded (class 'VMLModLoader' not found).")
		print("[vortarismodloader]   This is expected on a fresh clone: the extension cache")
		print("[vortarismodloader]   (.godot/extension_list.cfg) has not been generated yet, so")
		print("[vortarismodloader]   --script mode does not load vortarismodloader.gdextension.")
		print("[vortarismodloader]   Run this once to generate the cache:")
		print("[vortarismodloader]     godot --headless --editor --import --quit --path demo")
		print("[vortarismodloader]   (or open the project in the editor once), then re-run this CLI.")
		quit(1)
		return

	var impl = load(IMPL_PATH)
	if impl == null:
		printerr("[vortarismodloader] ERROR: failed to load ", IMPL_PATH)
		quit(1)
		return

	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		print(impl.USAGE)
		quit(1)
		return
	match args[0]:
		"--vortaris-vml-report":
			quit(impl.cmd_report())
		"--vortaris-vml-validate":
			if args.size() < 2:
				print(impl.USAGE)
				quit(1)
				return
			quit(impl.cmd_validate(args[1]))
		"--vortaris-vml-list":
			quit(impl.cmd_list())
		"--vortaris-vml-get":
			if args.size() < 2:
				print(impl.USAGE)
				quit(1)
				return
			quit(impl.cmd_get(args[1]))
		"--vortaris-vml-install":
			if args.size() < 2:
				print(impl.USAGE)
				quit(1)
				return
			quit(impl.cmd_install(args[1]))
		_:
			print("[vortarismodloader] unknown argument: ", args[0])
			print(impl.USAGE)
			quit(1)
