#pragma once

#include <unordered_map>
#include <vector>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "resource_id.h"

namespace vortarismodloader {

// A provider is one source that maps an id to a concrete physical file (or, for
// a value provider, directly to a Variant value — see `physical_path` empty).
struct ProviderEntry {
	godot::String mod_id; // "base" or the mod's id
	godot::String physical_path; // res:// or user:// path with extension
	int priority = 0; // higher wins
	bool explicit_ = false; // explicit register() outranks implicit at equal priority
	godot::Variant value; // value provider: used when physical_path.is_empty()
	// Singleton markers (`__registry__`/`__reroute__`/`__explicit__`): when true,
	// re-registration replaces any prior provider from the same marker mod instead
	// of stacking, so edits/reroutes/placeholders/removals take effect immediately
	// with no stale provider left behind. Real mod content files (id_overrides)
	// keep `singleton = false` so a mod may map several files onto one id.
	bool singleton = false;
	// Insertion-order sequence number. Used as the final deterministic tiebreak in
	// provider_higher so equal providers (same priority/explicit/mod_id) resolve to
	// a strict total order — never an unspecified std::sort permutation.
	uint64_t seq = 0;
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
	/// All providers for an id, in provider_higher order (priority desc, explicit
	/// desc, mod_id asc). Empty when the id is absent.
	std::vector<ProviderEntry> providers_for(const ResourceId &p_id) const;
	godot::String resolve(const ResourceId &p_id) const; // "" when absent
	/// Sorted (deterministic) id listing.
	std::vector<ResourceId> all_ids() const;
	std::vector<godot::String> namespaces() const;
	int provider_count() const;
	void clear();

private:
	struct Entry {
		std::vector<ProviderEntry> providers;
		// `p_seq_counter` supplies the insertion-order sequence number (deterministic
		// tiebreak; see ProviderEntry::seq).
		void insert_sorted(const ProviderEntry &p_e, uint64_t &p_seq_counter);
		const ProviderEntry *best() const;
	};
	std::unordered_map<ResourceIdKey, Entry, ResourceIdKeyHash> map_;
	uint64_t next_seq_ = 0; // monotonic insertion counter, never reset
};

} // namespace vortarismodloader
