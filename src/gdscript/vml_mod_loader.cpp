#include "vml_mod_loader.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/print_string.hpp>

namespace godot {

VMLModLoader *VMLModLoader::singleton = nullptr;

VMLModLoader::VMLModLoader() {
	print_line("VML: VortarisModLoader engine singleton initialized");
}

VMLModLoader::~VMLModLoader() {
}

void VMLModLoader::create_singleton() {
	ERR_FAIL_COND_MSG(singleton != nullptr, "VMLModLoader singleton already created.");
	singleton = memnew(VMLModLoader);
	Engine::get_singleton()->register_singleton("VML", singleton);
}

void VMLModLoader::free_singleton() {
	if (singleton) {
		Engine::get_singleton()->unregister_singleton("VML");
		memdelete(singleton);
		singleton = nullptr;
	}
}

VMLModLoader *VMLModLoader::get_singleton() {
	return singleton;
}

bool VMLModLoader::is_initialized() const {
	return singleton != nullptr;
}

void VMLModLoader::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_initialized"), &VMLModLoader::is_initialized);
}

} // namespace godot
