#pragma once

#include <unordered_map>
#include <vector>

#include <godot_cpp/variant/string.hpp>

#include "resource_id.h"

namespace vortarismodloader {

// A provider is one source that maps an id to a concrete physical file.
struct ProviderEntry {
	godot::String mod_id; // "base" or the mod's id
	godot::String physical_path; // res:// or user:// path with extension
	int priority = 0; // higher wins
	bool explicit_ = false; // explicit register() outranks implicit at equal priority
};

// The heart of id-indexing: `id -> sorted provider list`. The best provider for
// an id is the highest (priority, explicit_) entry; ties break by mod id
// (lexicographic) for determinism when the load-order graph leaves order
// ambiguous.
class RegistryIndex {
public:
	bool add(const ResourceId &p_id, const ProviderEntry &p_entry);
	/// Remove every provider contributed by a mod (unload path).
	bool remove_mod(const godot::String &p_mod_id);
	/// Remove one specific provider (used by unregister).
	bool remove_provider(const ResourceId &p_id, const godot::String &p_mod_id, const godot::String &p_physical_path);
	/// Remove every provider of a mod for one id (used by clear_reroute).
	bool remove_mod_provider(const ResourceId &p_id, const godot::String &p_mod_id);
	/// Re-assign priority of all providers from one mod (reorder pass).
	void set_mod_priority(const godot::String &p_mod_id, int p_priority);
	bool has(const ResourceId &p_id) const;
	/// Best provider or nullptr.
	const ProviderEntry *lookup(const ResourceId &p_id) const;
	godot::String resolve(const ResourceId &p_id) const; // "" when absent
	/// Sorted (deterministic) id listing.
	std::vector<ResourceId> all_ids() const;
	std::vector<godot::String> namespaces() const;
	int provider_count() const;
	void clear();

private:
	struct Entry {
		std::vector<ProviderEntry> providers;
		void insert_sorted(const ProviderEntry &p_e);
		const ProviderEntry *best() const;
	};
	std::unordered_map<ResourceIdKey, Entry, ResourceIdKeyHash> map_;
};

} // namespace vortarismodloader
