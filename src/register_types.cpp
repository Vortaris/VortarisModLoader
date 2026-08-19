#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "gdscript/vml_hot_reloader.h"
#include "gdscript/vml_mod_loader.h"
#include "gdscript/vml_resource_router.h"

using namespace godot;

static Ref<VMLResourceRouter> g_router;

// Project settings, organized under tiered categories (general / paths / export)
// matching the VortarisCSV/VortarisECS layout:
//
//   vortarismodloader/general/{verbose,show_error_dialogs,debug_output,
//                              auto_finish_startup,validate_on_startup,database_mode}
//   vortarismodloader/paths/{mod_dir,unpacked_dir,registry_path,scan_user_mods}
//   vortarismodloader/export/{export_mods}
//
// 0.3.2: the old PackedStringArray `paths/mod_paths` is gone from the editor — it
// was a poor editing experience AND its prose hint_string was parsed by the editor
// as the array element TYPE (an unknown name fell back to PROPERTY_HINT_RESOURCE_TYPE,
// so the array editor tried `ClassDB::get_parent_class("root directories scanned for
// mods and .pck packs")` → "Cannot get class '<prose>'."). It is replaced by two
// separate directory settings:
//
//   paths/mod_dir        (default "res://mods")        — dev mod main directory
//   paths/unpacked_dir   (default "res://mods-unpacked") — legacy unpacked directory
//
// VMLModLoader::mod_roots() composes the two into the scan roots and still merges
// the legacy `paths/mod_paths` array (tiered or flat `vortarismodloader/mod_paths`)
// as a backward-compatible fallback (see vml_mod_loader.cpp). add_mod_root /
// remove_mod_root / get_mod_roots are unchanged.
//
// NOTE on descriptions (Z4): Godot 4.7's `PropertyInfo` has NO tooltip/description
// field and `add_property_info` cannot attach one. The Project Settings editor
// tooltip is generated from the property *name* only. `hint_string` is parsed
// SEMANTICALLY per hint (enum options, path filter, resource class) and must never
// hold prose — that is exactly the "Cannot get class '<hint_string>'" bug above.
// Setting descriptions therefore live in the README / RELEASE_NOTES (see the
// settings table there), and every hint_string below is kept strictly semantic.
//
// Registered so they show up in the Project Settings editor with a sensible
// default; get_ml_setting() (core/vml_settings.h) supplies the default when unset
// and falls back to the legacy flat path vortarismodloader/<name> (0.3.0) for
// backward compatibility. `set_setting` must come first: add_property_info only
// attaches editor metadata to an existing setting.
static void register_vml_project_settings() {
	ProjectSettings *ps = ProjectSettings::get_singleton();

	struct VMLSettingDef {
		const char *category;
		const char *name;
		Variant::Type type;
		PropertyHint hint;
		const char *hint_string; // semantic ONLY (see note above) — never prose
		Variant default_value;
	};
	const VMLSettingDef settings[] = {
		{ "general", "verbose", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "", false },
		{ "general", "show_error_dialogs", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "", false },
		{ "general", "debug_output", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "", false },
		{ "general", "auto_finish_startup", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "", false },
		{ "general", "validate_on_startup", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "", true },
		{ "general", "database_mode", Variant::STRING, PropertyHint::PROPERTY_HINT_ENUM, "data,all,off", "data" },
		{ "paths", "mod_dir", Variant::STRING, PropertyHint::PROPERTY_HINT_DIR, "", "res://mods" },
		// Deprecated since 0.4.0 (#11): the single mod_dir replaces the dual
		// mods/mods-unpacked layout. Default EMPTY so new projects never scan a
		// second directory; projects that still set it explicitly keep working
		// (mod_roots merges it with a one-time migration notice).
		{ "paths", "unpacked_dir", Variant::STRING, PropertyHint::PROPERTY_HINT_DIR, "", "" },
		{ "paths", "registry_path", Variant::STRING, PropertyHint::PROPERTY_HINT_FILE_PATH, "*.json", "res://vml/registry.json" },
		{ "paths", "scan_user_mods", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "", true },
		{ "paths", "scan_adjacent_mods", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "", true },
		{ "export", "export_mods", Variant::STRING, PropertyHint::PROPERTY_HINT_ENUM, "embedded,external,none", "embedded" },
	};
	for (const VMLSettingDef &s : settings) {
		const String new_path = String("vortarismodloader/") + s.category + String("/") + s.name;
		const String old_path = String("vortarismodloader/") + s.name;
		// Only write when the new tiered key is absent. Writing unconditionally reset
		// a user's project.godot value back to the default on every startup (F4 fix);
		// when a legacy flat value exists, migrate it so the editor shows the real
		// value and behavior is preserved without relying on runtime fallback.
		if (!ps->has_setting(new_path)) {
			if (ps->has_setting(old_path)) {
				ps->set_setting(new_path, ps->get_setting(old_path));
			} else {
				ps->set_setting(new_path, s.default_value);
			}
		}
		// 0.3.2: drop the legacy flat key once the tiered path is ensured. Leaving it
		// in place lets get_ml_setting's "tiered==default && flat exists" fallback
		// keep shadowing runtime writes to the tiered path (set_export_policy /
		// set_database_mode would silently stop taking effect for upgraded projects).
		if (ps->has_setting(old_path)) {
			ps->clear(old_path);
		}
		Dictionary pi;
		pi["name"] = new_path;
		pi["type"] = s.type;
		pi["hint"] = s.hint;
		pi["hint_string"] = String(s.hint_string);
		ps->add_property_info(pi);
	}

	// 0.4.0 array settings (defaults are constructed at runtime, so they cannot
	// live in the literal table above). Same "write only when absent" rule.
	struct VMLArraySettingDef {
		const char *path;
		PackedStringArray default_value;
	};
	PackedStringArray default_base_dirs;
	default_base_dirs.push_back("res://assets");
	default_base_dirs.push_back("res://data");
	PackedStringArray default_exclude_exts;
	default_exclude_exts.push_back(".import");
	default_exclude_exts.push_back(".uid");
	default_exclude_exts.push_back(".tmp");
	default_exclude_exts.push_back(".bak");
	const VMLArraySettingDef array_settings[] = {
		// Issue #5: base-layer auto-scan directories (empty = disabled).
		{ "vortarismodloader/paths/base_dirs", default_base_dirs },
		// Issue #6: extension blacklist for implicit scans.
		{ "vortarismodloader/paths/scan_exclude_extensions", default_exclude_exts },
		// Issue #6: optional whitelist; when non-empty ONLY these extensions are
		// registered (blacklist ignored).
		{ "vortarismodloader/paths/scan_extensions", PackedStringArray() },
	};
	for (const VMLArraySettingDef &s : array_settings) {
		if (!ps->has_setting(s.path)) {
			ps->set_setting(s.path, s.default_value);
		}
		Dictionary pi;
		pi["name"] = String(s.path);
		pi["type"] = int(Variant::PACKED_STRING_ARRAY);
		ps->add_property_info(pi);
	}

	// 0.3.2 cleanup: the legacy PackedStringArray mod-path settings (tiered
	// `paths/mod_paths` and flat `vortarismodloader/mod_paths`) are gone from the
	// editor — the UI now uses the dir settings. Migrate mod_paths[0] into
	// mod_dir when it is still at its default, then clear the arrays so they stop
	// showing in Project Settings / lingering in project.godot. 0.4.0 (#11):
	// mod_paths[1..] all become extra_roots — the old second slot fed the
	// deprecated unpacked_dir, which is no longer part of the default layout.
	const String default_mod_dir = "res://mods";
	const char *legacy_arrays[] = {
		"vortarismodloader/paths/mod_paths",
		"vortarismodloader/mod_paths",
	};
	for (const char *legacy : legacy_arrays) {
		if (!ps->has_setting(legacy)) {
			continue;
		}
		const godot::Array arr = ps->get_setting(legacy);
		if (arr.size() > 0) {
			if (ps->get_setting("vortarismodloader/paths/mod_dir") == Variant(default_mod_dir)) {
				ps->set_setting("vortarismodloader/paths/mod_dir", arr[0]);
			}
			// Preserve any extra roots (index >= 1) — e.g. custom roots added via
			// add_mod_root or the deprecated unpacked slot — by appending them to
			// the runtime extra_roots list, otherwise they'd be silently lost when
			// the array is cleared below.
			for (int i = 1; i < arr.size(); i++) {
				// extra_roots is runtime-only (not registered); collect existing
				// entries if any, then append the legacy array's extra roots.
				const godot::Variant ev = ps->get_setting("vortarismodloader/paths/extra_roots");
				godot::Array extra;
				if (ev.get_type() == Variant::ARRAY) {
					extra = ev;
				} else if (ev.get_type() == Variant::PACKED_STRING_ARRAY) {
					const godot::PackedStringArray psa = ev;
					for (int k = 0; k < psa.size(); k++) {
						extra.push_back(psa[k]);
					}
				}
				extra.push_back(arr[i]);
				ps->set_setting("vortarismodloader/paths/extra_roots", extra);
			}
		}
		ps->clear(legacy);
	}
}

void initialize_vortarismodloader_module(ModuleInitializationLevel p_level) {
	// The whole plugin is registered at SCENE level: the VML singleton must
	// exist before any autoload's _init/_ready, and before the main scene.
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(VMLModLoader);
	GDREGISTER_CLASS(VMLResourceRouter);
	GDREGISTER_CLASS(VMLHotReloader);

	register_vml_project_settings();

	VMLModLoader::create_singleton();

	// Native ResourceFormatLoader so `load("vml://ns:path")` works in exports.
	g_router = Ref<VMLResourceRouter>(memnew(VMLResourceRouter));
	ResourceLoader::get_singleton()->add_resource_format_loader(g_router);
}

void uninitialize_vortarismodloader_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	if (g_router.is_valid()) {
		ResourceLoader::get_singleton()->remove_resource_format_loader(g_router);
		g_router = Ref<VMLResourceRouter>();
	}

	VMLModLoader::free_singleton();
}

extern "C" {
GDExtensionBool GDE_EXPORT vortarismodloader_library_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_vortarismodloader_module);
	init_obj.register_terminator(uninitialize_vortarismodloader_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
