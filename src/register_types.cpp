#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "gdscript/vml_hot_reloader.h"
#include "gdscript/vml_mod_loader.h"
#include "gdscript/vml_resource_router.h"

using namespace godot;

static Ref<VMLResourceRouter> g_router;

// 0.3.0 project settings. Registered so they show up in the Project Settings
// editor with a sensible default; get_setting(name, default) supplies the default
// when unset. `set_setting` must come first: add_property_info only attaches
// editor metadata to an existing setting.
static void register_vml_project_settings() {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	const char *settings[][3] = {
		{ "vortarismodloader/show_error_dialogs", "show a modal dialog listing mod errors at startup/rescan (non-headless only)", "false" },
		{ "vortarismodloader/debug_output", "advanced [vortarismodloader][dbg] debug logging (scan, registry, hooks, data, packs)", "false" },
	};
	for (const auto &s : settings) {
		// Only write the default when the setting is absent. Writing unconditionally
		// reset a user's project.godot value (e.g. show_error_dialogs=true) back to
		// the default on every startup.
		if (!ps->has_setting(StringName(s[0]))) {
			ps->set_setting(StringName(s[0]), s[2][0] == 't' ? Variant(true) : Variant(false));
		}
		Dictionary pi;
		pi["name"] = StringName(s[0]);
		pi["type"] = Variant::BOOL;
		pi["hint"] = PropertyHint::PROPERTY_HINT_NONE;
		pi["hint_string"] = String(s[1]);
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
