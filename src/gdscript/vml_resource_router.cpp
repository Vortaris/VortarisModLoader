#include "vml_resource_router.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "vml_mod_loader.h"

namespace godot {

VMLResourceRouter::VMLResourceRouter() {
}

Variant VMLResourceRouter::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads,
		int32_t p_cache_mode) const {
	if (!p_path.begins_with("vml://")) {
		return Variant();
	}
	const String id = p_path.substr(6); // strlen("vml://")
	VMLModLoader *vml = VMLModLoader::get_singleton();
	if (vml == nullptr) {
		return Variant();
	}
	return vml->get_resource(id);
}

bool VMLResourceRouter::_recognize_path(const String &p_path, const StringName &p_type) const {
	return p_path.begins_with("vml://");
}

bool VMLResourceRouter::_handles_type(const StringName &p_type) const {
	return false; // we only intercept the vml:// path scheme, never a type
}

PackedStringArray VMLResourceRouter::_get_recognized_extensions() const {
	PackedStringArray a;
	a.push_back("vml");
	return a;
}

bool VMLResourceRouter::_exists(const String &p_path) const {
	if (!p_path.begins_with("vml://")) {
		return false;
	}
	VMLModLoader *vml = VMLModLoader::get_singleton();
	return vml != nullptr && vml->has(p_path.substr(6));
}

String VMLResourceRouter::_get_resource_type(const String &p_path) const {
	return "Resource";
}

void VMLResourceRouter::_bind_methods() {
}

} // namespace godot
