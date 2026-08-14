#include "change_watcher.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

namespace vortarismodloader {

bool ChangeWatcher::stat_file(const godot::String &p_path, Stat &p_out) {
	if (!godot::FileAccess::file_exists(p_path)) {
		return false;
	}
	p_out.mtime = (uint64_t)godot::FileAccess::get_modified_time(p_path);
	godot::Ref<godot::FileAccess> f = godot::FileAccess::open(p_path, godot::FileAccess::READ);
	if (f.is_null()) {
		return false;
	}
	p_out.size = f->get_length();
	return true;
}

void ChangeWatcher::seed(const godot::String &p_path) {
	Stat s;
	if (stat_file(p_path, s)) {
		stats_[p_path] = s;
	}
}

void ChangeWatcher::seed_tree(const godot::String &p_root) {
	godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(p_root);
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	godot::String e;
	while ((e = dir->get_next()) != godot::String()) {
		if (e == "." || e == "..") {
			continue;
		}
		const godot::String child = p_root + godot::String("/") + e;
		if (dir->current_is_dir()) {
			seed_tree(child);
		} else {
			seed(child);
		}
	}
	dir->list_dir_end();
}

std::vector<godot::String> ChangeWatcher::poll() {
	std::vector<godot::String> changed;
	for (auto &kv : stats_) {
		Stat cur;
		if (!stat_file(kv.first, cur)) {
			changed.push_back(kv.first); // file gone
			continue;
		}
		if (cur.mtime != kv.second.mtime || cur.size != kv.second.size) {
			kv.second = cur;
			changed.push_back(kv.first);
		}
	}
	return changed;
}

void ChangeWatcher::clear() {
	stats_.clear();
}

} // namespace vortarismodloader
