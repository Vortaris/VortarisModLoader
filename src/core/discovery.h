#pragma once

#include <vector>

#include <godot_cpp/variant/string.hpp>

namespace vortarismodloader {

struct DiscoveredMod {
	godot::String root; // e.g. "res://mods-unpacked/sample_mod" or "user://vml/mods/x"
	godot::String manifest_path; // root + "/manifest.json"
};

// Scans a directory of mod folders: every direct child that contains a
// manifest.json is a mod.
class DiscoveryScanner {
public:
	static void scan_mod_dirs(const godot::String &p_mods_root, std::vector<DiscoveredMod> &p_out);
	/// Recursively collect every `*.pck` under a directory (for pack mounting).
	static void scan_pck_files(const godot::String &p_root, std::vector<godot::String> &p_out);
};

} // namespace vortarismodloader
