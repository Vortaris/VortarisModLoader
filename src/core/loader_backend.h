#pragma once

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace vortarismodloader {

// The one bridge that turns a resolved physical path into a Godot value.
// Data-ish files (.json/.csv) parse to Dictionary/Array; everything else loads
// as a Resource through ResourceLoader. Raw image/audio/font construction for
// user:// paths (no import cache) is added in a later milestone.
class LoaderBackend {
public:
	static godot::Variant load_data(const godot::String &p_path);
	static godot::Ref<godot::Resource> load_resource(const godot::String &p_path,
			godot::ResourceLoader::CacheMode p_mode = godot::ResourceLoader::CACHE_MODE_REUSE);
};

} // namespace vortarismodloader
