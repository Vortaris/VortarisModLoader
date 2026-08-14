#pragma once

#include <cstdint>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace vortarismodloader {

// A namespaced content identifier: `namespace.path` (Minecraft ResourceLocation
// style). The namespace is usually the mod id; the path is an extension-free,
// dotted slug (slashes are NOT allowed — ids are compact, never file-path-like).
//
// Examples: `game:units.knight`, `mymod:icons.archer`.
struct ResourceId {
	godot::String ns;
	godot::String path;

	/// "ns:path"
	godot::String canonical() const;
	/// Parse "ns:path"; false on any validation failure.
	static bool parse(const godot::String &p_s, ResourceId &p_out);
	static bool is_valid_namespace(const godot::String &p_ns); // ^[a-z0-9_]{1,32}$
	static bool is_valid_path(const godot::String &p_path);    // [a-zA-Z0-9_./-]+, no leading/trailing '/'
	bool operator==(const ResourceId &p_o) const;
	bool operator!=(const ResourceId &p_o) const;
	uint32_t hash() const;
};

// Hashable key for std::unordered_map.
struct ResourceIdKey {
	godot::StringName ns;
	godot::StringName path;
	bool operator==(const ResourceIdKey &p_o) const;
};

struct ResourceIdKeyHash {
	size_t operator()(const ResourceIdKey &p_k) const;
};

} // namespace vortarismodloader
