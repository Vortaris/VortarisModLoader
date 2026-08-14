#include "registry_index.h"

#include <algorithm>

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
	// Replace-in-place if the same mod already provides this id.
	for (auto it = providers.begin(); it != providers.end(); ++it) {
		if (it->mod_id == p_e.mod_id) {
			*it = p_e;
			// Keep the list sorted after the replace.
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
}

bool RegistryIndex::has(const ResourceId &p_id) const {
	const auto it = map_.find(ResourceIdKey{ p_id.ns, p_id.path });
	return it != map_.end() && !it->second.providers.empty();
}

const ProviderEntry *RegistryIndex::lookup(const ResourceId &p_id) const {
	const auto it = map_.find(ResourceIdKey{ p_id.ns, p_id.path });
	return it == map_.end() ? nullptr : it->second.best();
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
