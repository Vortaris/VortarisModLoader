#pragma once

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace vortarismodloader {

// Read a project setting by its tiered (0.3.1+) key
//   "vortarismodloader/<category>/<name>"
// with backward-compatible fallback to the legacy flat key
//   "vortarismodloader/<name>"
// used by 0.3.0 and earlier project.godot files.
//
// The tiered keys are auto-registered with their defaults at extension load so
// they show up in the Project Settings editor (see register_vml_project_settings).
// To keep existing projects AND runtime writes to the legacy flat path working,
// the flat value wins when the tiered value is still exactly the registered
// default — i.e. the user has not explicitly configured the tiered key.
inline godot::Variant get_ml_setting(const char *p_category, const char *p_name, const godot::Variant &p_default) {
	godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
	if (ps == nullptr) {
		return p_default;
	}
	const godot::String tiered = godot::String("vortarismodloader/") + godot::String(p_category) + godot::String("/") + godot::String(p_name);
	const godot::String flat = godot::String("vortarismodloader/") + godot::String(p_name);
	if (ps->has_setting(tiered)) {
		if (ps->get_setting(tiered) == p_default && ps->has_setting(flat)) {
			return ps->get_setting(flat);
		}
		return ps->get_setting(tiered);
	}
	if (ps->has_setting(flat)) {
		return ps->get_setting(flat);
	}
	return p_default;
}

} // namespace vortarismodloader
