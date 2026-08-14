#pragma once

#include <godot_cpp/variant/string.hpp>

namespace vortarismodloader {

class RegistryIndex;

// Walks a content root (`<root>/assets/`, `<root>/data/`) and registers implicit
// ids from the file layout: `assets/<ns>/<path>.<ext>` -> id `ns:path` (the
// extension never participates in the id). Zero declaration — drop a file and
// it gets an id.
class Scanner {
public:
	/// Scan one content dir (e.g. "res://assets" or "user://vml/mods/x/assets").
	static void scan_implicit_dir(const godot::String &p_base_dir,
			const godot::String &p_mod_id, int p_priority, RegistryIndex &p_idx);
};

} // namespace vortarismodloader
