#pragma once

#include <unordered_map>
#include <vector>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "resource_id.h"

namespace vortarismodloader {

// In-memory content repository. Ids resolve to concrete values (parsed data or
// loaded resources) plus source metadata. This is the "unified load" backing
// store: after a preload pass, id lookups are O(1) hashes with zero file I/O.
//
// The database is mutable by design: set_data()/delete_data() let the game or a
// hot-reload pass rewrite entries in place, so "reload the resource pointer"
// applies to live content too.
struct DbEntry {
	godot::Variant value;
	godot::String physical_path;
	godot::String mod_id;
};

class ContentDatabase {
public:
	void set(const ResourceId &p_id, const godot::Variant &p_value, const godot::String &p_path,
			const godot::String &p_mod_id);
	/// Loaded-entry lookup; false when not present.
	bool get(const ResourceId &p_id, godot::Variant &p_out) const;
	bool has(const ResourceId &p_id) const;
	void erase(const ResourceId &p_id);
	void erase_mod(const godot::String &p_mod_id);
	void clear();
	/// Sorted ids of loaded entries.
	std::vector<ResourceId> loaded_ids() const;
	int size() const;

private:
	std::unordered_map<ResourceIdKey, DbEntry, ResourceIdKeyHash> map_;
};

} // namespace vortarismodloader
