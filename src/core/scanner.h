#pragma once

#include <map>

#include <godot_cpp/variant/string.hpp>

#include "registry_index.h"

namespace vortarismodloader {

// Walks a content root (`<root>/assets/`, `<root>/data/`) and registers implicit
// ids from the file layout: `assets/<ns>/<path>.<ext>` -> id `ns:path` (the
// extension never participates in the id). Zero declaration — drop a file and
// it gets an id.
class Scanner {
public:
	/// Scan one content dir (e.g. "res://assets" or "user://vml/mods/x/assets").
	static void scan_implicit_dir(const godot::String &p_base_dir,
			const godot::String &p_mod_id, int p_priority, RegistryIndex &p_idx);

	/// Scan one mod content dir with explicit id_overrides applied.
	/// `p_abs_dir` is the absolute content dir (`mod_root/<rel_dir>`);
	/// `p_rel_dir` is that dir relative to the mod root (e.g. "data"), used to
	/// match id_override keys. A file whose full relative path is an id_override
	/// key registers under that explicit id (beating path inference); several
	/// files may map to the same id and are arbitrated as normal providers.
	static void scan_dir_with_overrides(const godot::String &p_abs_dir, const godot::String &p_rel_dir,
			const godot::String &p_mod_id, int p_priority,
			const std::map<godot::String, godot::String> &p_id_overrides, RegistryIndex &p_idx);
};

} // namespace vortarismodloader
