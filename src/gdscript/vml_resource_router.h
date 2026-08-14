#ifndef VML_RESOURCE_ROUTER_H
#define VML_RESOURCE_ROUTER_H

#include <godot_cpp/classes/resource_format_loader.hpp>

namespace godot {

// Native ResourceFormatLoader that resolves `load("vml://namespace:path")` to the
// winning provider of that id. Registered once at SCENE init; works in exported
// builds (a capability pure-GDScript loaders lack). Data ids (.json/.csv) are not
// Resources and must be read via VML.get_data() instead.
class VMLResourceRouter : public ResourceFormatLoader {
	GDCLASS(VMLResourceRouter, ResourceFormatLoader)

public:
	VMLResourceRouter();

	Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads,
			int32_t p_cache_mode) const override;
	bool _recognize_path(const String &p_path, const StringName &p_type) const override;
	bool _handles_type(const StringName &p_type) const override;
	PackedStringArray _get_recognized_extensions() const override;
	bool _exists(const String &p_path) const override;
	String _get_resource_type(const String &p_path) const override;

protected:
	static void _bind_methods();
};

} // namespace godot

#endif // VML_RESOURCE_ROUTER_H
