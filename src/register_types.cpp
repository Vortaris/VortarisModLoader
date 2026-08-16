#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "gdscript/vml_hot_reloader.h"
#include "gdscript/vml_mod_loader.h"
#include "gdscript/vml_resource_router.h"

using namespace godot;

static Ref<VMLResourceRouter> g_router;

// 0.3.1 project settings, organized under tiered categories (general / paths /
// export) matching the VortarisCSV/VortarisECS layout:
//
//   vortarismodloader/general/{verbose,show_error_dialogs,debug_output,
//                              auto_finish_startup,validate_on_startup,database_mode}
//   vortarismodloader/paths/{mod_paths,registry_path,scan_user_mods}
//   vortarismodloader/export/{export_mods}
//
// Registered so they show up in the Project Settings editor with a sensible
// default; get_ml_setting() (core/vml_settings.h) supplies the default when unset
// and falls back to the legacy flat path vortarismodloader/<name> (0.3.0) for
// backward compatibility. `set_setting` must come first: add_property_info only
// attaches editor metadata to an existing setting.
static void register_vml_project_settings() {
	ProjectSettings *ps = ProjectSettings::get_singleton();

	PackedStringArray default_mod_paths;
	default_mod_paths.push_back("res://mods");
	default_mod_paths.push_back("res://mods-unpacked");

	struct VMLSettingDef {
		const char *category;
		const char *name;
		Variant::Type type;
		PropertyHint hint;
		const char *hint_string;
		Variant default_value;
	};
	const VMLSettingDef settings[] = {
		{ "general", "verbose", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "detailed load logging", false },
		{ "general", "show_error_dialogs", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "show a modal dialog listing mod errors at startup/rescan (non-headless only)", false },
		{ "general", "debug_output", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "advanced [vortarismodloader][dbg] debug logging (scan, registry, hooks, data, packs)", false },
		{ "general", "auto_finish_startup", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "auto-run finish_startup() once the scene tree is ready", false },
		{ "general", "validate_on_startup", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "validate mods at startup; problems are marked but never refuse to boot", true },
		{ "general", "database_mode", Variant::STRING, PropertyHint::PROPERTY_HINT_ENUM, "data,all,off", "data" },
		{ "paths", "mod_paths", Variant::PACKED_STRING_ARRAY, PropertyHint::PROPERTY_HINT_NONE, "root directories scanned for mods and .pck packs", default_mod_paths },
		{ "paths", "registry_path", Variant::STRING, PropertyHint::PROPERTY_HINT_NONE, "project-level registry file (res://vml/registry.json; falls back to user://vml/registry.json in read-only exports)", "res://vml/registry.json" },
		{ "paths", "scan_user_mods", Variant::BOOL, PropertyHint::PROPERTY_HINT_NONE, "scan non-res:// mod roots at boot", true },
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
		Dictionary pi;
		pi["name"] = new_path;
		pi["type"] = s.type;
		pi["hint"] = s.hint;
		pi["hint_string"] = String(s.hint_string);
		ps->add_property_info(pi);
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
