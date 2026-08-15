#include "registry_index.h"

#include <algorithm>

#include "debug_log.h"

namespace vortarismodloader {

static bool provider_higher(const ProviderEntry &a, const ProviderEntry &b) {
	if (a.priority != b.priority) {
		return a.priority > b.priority;
	}
	if (a.explicit_ != b.explicit_) {
		return a.explicit_;
	}
	return a.mod_id < b.mod_id;
}

void RegistryIndex::Entry::insert_sorted(const ProviderEntry &p_e) {
	// Replace-in-place when the same mod already provides this id from the same
	// physical file (re-registration). A mod may legitimately map several files
	// to one id via manifest id_overrides — those stack as distinct providers so
	// arbitration still resolves them (equal priority/explicit/mod_id keeps the
	// higher-priority-over-equal rule, then provider order is deterministic only
	// by count; see providers_for).
	for (auto it = providers.begin(); it != providers.end(); ++it) {
		if (it->mod_id == p_e.mod_id && it->physical_path == p_e.physical_path) {
			*it = p_e;
			providers.erase(it);
			break;
		}
	}
	providers.push_back(p_e);
	std::sort(providers.begin(), providers.end(), provider_higher);
}

const ProviderEntry *RegistryIndex::Entry::best() const {
	return providers.empty() ? nullptr : &providers[0];
}

bool RegistryIndex::add(const ResourceId &p_id, const ProviderEntry &p_entry) {
	Entry &e = map_[ResourceIdKey{ p_id.ns, p_id.path }];
	e.insert_sorted(p_entry);
	log_debug(godot::String("registry: add '") + p_id.canonical() + godot::String("' provider mod=") +
			p_entry.mod_id + godot::String(" path=") + p_entry.physical_path +
			godot::String(" pri=") + godot::String::num_int64(p_entry.priority) +
			godot::String(" explicit=") + godot::String(p_entry.explicit_ ? "true" : "false"));
	return true;
}

bool RegistryIndex::remove_mod(const godot::String &p_mod_id) {
	bool removed_any = false;
	for (auto it = map_.begin(); it != map_.end();) {
		auto &providers = it->second.providers;
		const size_t before = providers.size();
		providers.erase(std::remove_if(providers.begin(), providers.end(),
								[&](const ProviderEntry &p_e) { return p_e.mod_id == p_mod_id; }),
				providers.end());
		if (providers.size() != before) {
			removed_any = true;
		}
		if (providers.empty()) {
			it = map_.erase(it);
		} else {
			++it;
		}
	}
	if (removed_any) {
		log_debug(godot::String("registry: remove_mod '") + p_mod_id + godot::String("'"));
	}
	return removed_any;
}

bool RegistryIndex::remove_provider(const ResourceId &p_id, const godot::String &p_mod_id,
		const godot::String &p_physical_path) {
	const auto it = map_.find(ResourceIdKey{ p_id.ns, p_id.path });
	if (it == map_.end()) {
		return false;
	}
	auto &providers = it->second.providers;
	const size_t before = providers.size();
	providers.erase(std::remove_if(providers.begin(), providers.end(),
							[&](const ProviderEntry &p_e) {
								return p_e.mod_id == p_mod_id && p_e.physical_path == p_physical_path;
							}),
			providers.end());
	// Capture the result BEFORE the entry may be destroyed by map_.erase().
	const bool removed = providers.size() != before;
	if (providers.empty()) {
		map_.erase(it);
	}
	return removed;
}

bool RegistryIndex::remove_mod_provider(const ResourceId &p_id, const godot::String &p_mod_id) {
	const auto it = map_.find(ResourceIdKey{ p_id.ns, p_id.path });
	if (it == map_.end()) {
		return false;
	}
	auto &providers = it->second.providers;
	const size_t before = providers.size();
	providers.erase(std::remove_if(providers.begin(), providers.end(),
							[&](const ProviderEntry &p_e) { return p_e.mod_id == p_mod_id; }),
			providers.end());
	const bool removed = providers.size() != before;
	if (providers.empty()) {
		map_.erase(it);
	}
	return removed;
}

void RegistryIndex::set_mod_priority(const godot::String &p_mod_id, int p_priority) {
	for (auto &kv : map_) {
		for (auto &p : kv.second.providers) {
			if (p.mod_id == p_mod_id) {
				p.priority = p_priority;
			}
		}
	}
	// Re-sort every entry touched.
	for (auto &kv : map_) {
		std::sort(kv.second.providers.begin(), kv.second.providers.end(), provider_higher);
	}
	log_debug(godot::String("registry: set_mod_priority '") + p_mod_id +
			godot::String("' pri=") + godot::String::num_int64(p_priority));
}

bool RegistryIndex::has(const ResourceId &p_id) const {
	const auto it = map_.find(ResourceIdKey{ p_id.ns, p_id.path });
	return it != map_.end() && !it->second.providers.empty();
}

const ProviderEntry *RegistryIndex::lookup(const ResourceId &p_id) const {
	const auto it = map_.find(ResourceIdKey{ p_id.ns, p_id.path });
	return it == map_.end() ? nullptr : it->second.best();
}

std::vector<ProviderEntry> RegistryIndex::providers_for(const ResourceId &p_id) const {
	const auto it = map_.find(ResourceIdKey{ p_id.ns, p_id.path });
	return it == map_.end() ? std::vector<ProviderEntry>() : it->second.providers;
}

godot::String RegistryIndex::resolve(const ResourceId &p_id) const {
	const ProviderEntry *e = lookup(p_id);
	return e ? e->physical_path : godot::String();
}

std::vector<ResourceId> RegistryIndex::all_ids() const {
	std::vector<ResourceId> out;
	out.reserve(map_.size());
	for (const auto &kv : map_) {
		out.push_back(ResourceId{ kv.first.ns, kv.first.path });
	}
	std::sort(out.begin(), out.end(), [](const ResourceId &a, const ResourceId &b) {
		if (a.ns != b.ns) {
			return a.ns < b.ns;
		}
		return a.path < b.path;
	});
	return out;
}

std::vector<godot::String> RegistryIndex::namespaces() const {
	std::vector<godot::String> out;
	for (const auto &kv : map_) {
		const godot::String &ns = kv.first.ns;
		if (std::find(out.begin(), out.end(), ns) == out.end()) {
			out.push_back(ns);
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

int RegistryIndex::provider_count() const {
	return (int)map_.size();
}

void RegistryIndex::clear() {
	map_.clear();
}

} // namespace vortarismodloader
