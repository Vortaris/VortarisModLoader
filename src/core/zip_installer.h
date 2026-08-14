#pragma once

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/string.hpp>

#include "manifest.h"

namespace vortarismodloader {

// Transactional zip mod installer. Extracts to a temp dir, validates the
// manifest, then renames into place (temp + rename + rollback). Never leaves a
// half-written mod behind.
class ZipInstaller {
public:
	/// Extracts into `p_dest_base/<mod_id>/` and fills `p_out_manifest`; OK on success.
	static godot::Error install(const godot::String &p_zip_path, const godot::String &p_dest_base, ModManifest &p_out);
	/// Move to a backup dir then delete (safe uninstall).
	static godot::Error uninstall(const godot::String &p_mod_dir, const godot::String &p_backup_root);
	/// Recursively remove a directory.
	static void remove_recursive(const godot::String &p_path);
};

} // namespace vortarismodloader
