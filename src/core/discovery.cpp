#include "discovery.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

namespace vortarismodloader {

void DiscoveryScanner::scan_mod_dirs(const godot::String &p_mods_root, std::vector<DiscoveredMod> &p_out) {
	godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(p_mods_root);
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	godot::String e;
	while ((e = dir->get_next()) != godot::String()) {
		if (e == "." || e == ".." || !dir->current_is_dir()) {
			continue;
		}
		const godot::String root = p_mods_root + godot::String("/") + e;
		const godot::String manifest = root + godot::String("/manifest.json");
		if (godot::FileAccess::file_exists(manifest)) {
			p_out.push_back(DiscoveredMod{ root, manifest });
		}
	}
	dir->list_dir_end();
}

} // namespace vortarismodloader
