#include "zip_installer.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/zip_reader.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace vortarismodloader {

using namespace godot;

void ZipInstaller::remove_recursive(const godot::String &p_path) {
	godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(p_path);
	if (dir.is_null()) {
		godot::DirAccess::remove_absolute(p_path); // plain file
		return;
	}
	dir->list_dir_begin();
	godot::String e;
	while ((e = dir->get_next()) != godot::String()) {
		if (e == "." || e == "..") {
			continue;
		}
		const godot::String child = p_path + godot::String("/") + e;
		if (dir->current_is_dir()) {
			remove_recursive(child);
		} else {
			godot::DirAccess::remove_absolute(child);
		}
	}
	dir->list_dir_end();
	godot::DirAccess::remove_absolute(p_path);
}

Error ZipInstaller::install(const godot::String &p_zip_path, const godot::String &p_dest_base,
		ModManifest &p_out) {
	godot::Ref<godot::ZIPReader> zip;
	zip.instantiate();
	Error err = zip->open(p_zip_path);
	if (err != OK) {
		return err;
	}
	const godot::PackedStringArray files = zip->get_files();

	// The mod root is the folder that directly contains manifest.json (either the
	// zip root or a single top folder like <mod_id>/).
	bool has_manifest = false;
	godot::String prefix;
	for (const godot::String &f : files) {
		if (f.ends_with("manifest.json")) {
			has_manifest = true;
			const int idx = f.rfind("/");
			prefix = idx > 0 ? f.substr(0, idx + 1) : godot::String();
			break;
		}
	}
	if (!has_manifest) {
		zip->close();
		return ERR_INVALID_DATA;
	}

	// Unique temp dir under the same parent (user://vml/tmp_...).
	const godot::String tmp = p_dest_base + godot::String("/tmp_install_") +
			godot::String::num_int64(godot::Time::get_singleton()->get_ticks_usec());
	godot::DirAccess::make_dir_recursive_absolute(tmp);

	// Extract.
	bool extract_ok = true;
	for (const godot::String &f : files) {
		if (f.ends_with("/")) {
			continue; // directory entry
		}
		if (!prefix.is_empty() && !f.begins_with(prefix)) {
			continue;
		}
		const godot::String rel = prefix.is_empty() ? f : f.substr(prefix.length());
		const godot::String out_path = tmp + godot::String("/") + rel;
		godot::DirAccess::make_dir_recursive_absolute(out_path.get_base_dir());
		godot::Ref<godot::FileAccess> fout = godot::FileAccess::open(out_path, godot::FileAccess::WRITE);
		if (fout.is_null()) {
			extract_ok = false;
			break;
		}
		fout->store_buffer(zip->read_file(f));
		fout->close();
	}
	zip->close();
	if (!extract_ok) {
		remove_recursive(tmp);
		return ERR_CANT_OPEN;
	}

	// Validate manifest before activation.
	if (!ManifestParser::load(tmp + godot::String("/manifest.json"), p_out) || !p_out.valid()) {
		remove_recursive(tmp);
		return ERR_INVALID_DATA;
	}

	// Activate: replace any existing copy, then rename temp into dest_base/<id>.
	const godot::String dest_root = p_dest_base + godot::String("/") + p_out.id;
	if (godot::DirAccess::dir_exists_absolute(dest_root)) {
		remove_recursive(dest_root);
	}
	const Error rename_err = godot::DirAccess::rename_absolute(tmp, dest_root);
	if (rename_err != OK) {
		remove_recursive(tmp);
		return rename_err;
	}
	return OK;
}

Error ZipInstaller::uninstall(const godot::String &p_mod_dir, const godot::String &p_backup_root) {
	if (!godot::DirAccess::dir_exists_absolute(p_mod_dir)) {
		return ERR_DOES_NOT_EXIST;
	}
	godot::DirAccess::make_dir_recursive_absolute(p_backup_root);
	// Move to a timestamped backup first, then delete it.
	const godot::String backup = p_backup_root + godot::String("/") +
			godot::String::num_int64(godot::Time::get_singleton()->get_ticks_usec());
	const Error err = godot::DirAccess::rename_absolute(p_mod_dir, backup);
	if (err != OK) {
		return err;
	}
	remove_recursive(backup);
	return OK;
}

} // namespace vortarismodloader
