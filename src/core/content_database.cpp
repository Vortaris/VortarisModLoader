#include "content_database.h"

#include <algorithm>

#include "debug_log.h"

namespace vortarismodloader {

void ContentDatabase::set(const ResourceId &p_id, const godot::Variant &p_value,
		const godot::String &p_path, const godot::String &p_mod_id) {
	map_[ResourceIdKey{ p_id.ns, p_id.path }] = DbEntry{ p_value, p_path, p_mod_id };
	log_debug(godot::String("db: set '") + p_id.canonical() + godot::String("' (mod=") + p_mod_id +
			godot::String(", path=") + p_path + godot::String(")"));
}

bool ContentDatabase::get(const ResourceId &p_id, godot::Variant &p_out) const {
	const auto it = map_.find(ResourceIdKey{ p_id.ns, p_id.path });
	if (it == map_.end()) {
		return false;
	}
	p_out = it->second.value;
	return true;
}

bool ContentDatabase::has(const ResourceId &p_id) const {
	return map_.find(ResourceIdKey{ p_id.ns, p_id.path }) != map_.end();
}

void ContentDatabase::erase(const ResourceId &p_id) {
	map_.erase(ResourceIdKey{ p_id.ns, p_id.path });
	log_debug(godot::String("db: erase '") + p_id.canonical() + godot::String("'"));
}

void ContentDatabase::erase_mod(const godot::String &p_mod_id) {
	size_t before = map_.size();
	for (auto it = map_.begin(); it != map_.end();) {
		if (it->second.mod_id == p_mod_id) {
			it = map_.erase(it);
		} else {
			++it;
		}
	}
	if (map_.size() != before) {
		log_debug(godot::String("db: erase_mod '") + p_mod_id + godot::String("'"));
	}
}

void ContentDatabase::clear() {
	map_.clear();
	log_debug("db: clear");
}

std::vector<ResourceId> ContentDatabase::loaded_ids() const {
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

int ContentDatabase::size() const {
	return (int)map_.size();
}

} // namespace vortarismodloader
