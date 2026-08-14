#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include "gdscript/vml_hot_reloader.h"
#include "gdscript/vml_mod_loader.h"
#include "gdscript/vml_resource_router.h"

using namespace godot;

static Ref<VMLResourceRouter> g_router;

void initialize_vortarismodloader_module(ModuleInitializationLevel p_level) {
	// The whole plugin is registered at SCENE level: the VML singleton must
	// exist before any autoload's _init/_ready, and before the main scene.
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(VMLModLoader);
	GDREGISTER_CLASS(VMLResourceRouter);
	GDREGISTER_CLASS(VMLHotReloader);

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
