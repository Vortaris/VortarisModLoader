#pragma once

#include <vector>

#include <godot_cpp/variant/string.hpp>

namespace vortarismodloader {

// Ordered list of content sources (base layer + each enabled mod). A source's
// priority is its position: base is 0, each mod (in dependency load order) is
// 1..n. Later sources override earlier ones — Minecraft resource-pack semantics.
struct Source {
	godot::String mod_id;
	bool is_base = false;
};

class OverlayStack {
public:
	void clear();
	/// Appends a source and returns its assigned priority. Duplicate mod_id is a no-op.
	int add_source(const godot::String &p_mod_id, bool p_is_base);
	bool remove_source(const godot::String &p_mod_id);
	/// Rebuild priorities from a given mod load order (base stays 0).
	void reorder(const std::vector<godot::String> &p_ordered_mod_ids);
	std::vector<Source> sources() const;
	/// -1 when absent.
	int priority_of(const godot::String &p_mod_id) const;

private:
	std::vector<Source> sources_;
};

} // namespace vortarismodloader
