#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include "gdscript/vml_mod_loader.h"

using namespace godot;

void initialize_vortarismodloader_module(ModuleInitializationLevel p_level) {
	// The whole plugin is registered at SCENE level: the VML singleton must
	// exist before any autoload's _init/_ready, and before the main scene.
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(VMLModLoader);

	VMLModLoader::create_singleton();
}

void uninitialize_vortarismodloader_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
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
