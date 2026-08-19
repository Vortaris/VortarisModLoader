#include "scanner.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "debug_log.h"
#include "resource_id.h"
#include "vml_settings.h"

namespace vortarismodloader {

namespace {

// Issue #6: extension filtering for implicit scans. Godot metadata files
// (*.import, *.uid, temp/backup files) used to be auto-registered as bogus ids
// ("foo.csv.import" -> id "foo.csv" pointing at the metadata file). Two
// settings control this:
//   paths/scan_exclude_extensions  blacklist, default [".import",".uid",".tmp",".bak"]
//   paths/scan_extensions          whitelist; when non-empty ONLY these pass
// Both are lowercase, dot-prefixed. The filter is built once per top-level scan
// (not per file) and passed down the recursion.
struct ExtFilter {
	std::vector<godot::String> exclude;
	std::vector<godot::String> include; // empty = whitelist disabled
};

std::vector<godot::String> read_ext_list(const char *p_name, const godot::PackedStringArray &p_default) {
	std::vector<godot::String> out;
	const godot::Variant v = get_ml_setting("paths", p_name, p_default);
	if (v.get_type() == godot::Variant::PACKED_STRING_ARRAY) {
		const godot::PackedStringArray arr = v;
		for (int64_t i = 0; i < arr.size(); ++i) {
			godot::String e = arr[i].strip_edges().to_lower();
			if (e.is_empty()) {
				continue;
			}
			if (!e.begins_with(".")) {
				e = godot::String(".") + e;
			}
			out.push_back(e);
		}
	}
	return out;
}

ExtFilter build_ext_filter() {
	godot::PackedStringArray default_exclude;
	default_exclude.push_back(".import");
	default_exclude.push_back(".uid");
	default_exclude.push_back(".tmp");
	default_exclude.push_back(".bak");
	ExtFilter f;
	f.exclude = read_ext_list("scan_exclude_extensions", default_exclude);
	f.include = read_ext_list("scan_extensions", godot::PackedStringArray());
	return f;
}

bool ext_allowed(const ExtFilter &p_f, const godot::String &p_filename) {
	const int dot = p_filename.rfind(".");
	const godot::String ext = (dot >= 0) ? p_filename.substr(dot).to_lower() : godot::String();
	if (!p_f.include.empty()) {
		for (const godot::String &inc : p_f.include) {
			if (ext == inc) {
				return true;
			}
		}
		return false;
	}
	for (const godot::String &exc : p_f.exclude) {
		if (ext == exc) {
			return false;
		}
	}
	return true;
}

// Recursively register every file under `p_abs_dir`. `p_id_rel_path` is the path
// below the namespace segment (id path, before extension stripping);
// `p_root_rel_path` is the full path relative to the mod root (used to match
// id_overrides keys). `p_id_overrides` may be null (base-layer / no overrides).
void collect(const godot::String &p_abs_dir, const godot::String &p_id_rel_path,
		const godot::String &p_root_rel_path, const godot::String &p_ns,
		const godot::String &p_mod_id, int p_priority,
		const std::map<godot::String, godot::String> *p_id_overrides, const ExtFilter &p_filter,
		RegistryIndex &p_idx) {
	godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(p_abs_dir);
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	godot::String e;
	while ((e = dir->get_next()) != godot::String()) {
		if (e == "." || e == "..") {
			continue;
		}
		const godot::String abs = p_abs_dir + godot::String("/") + e;
		if (dir->current_is_dir()) {
			const godot::String child_id_rel = p_id_rel_path.is_empty() ? e : p_id_rel_path + godot::String("/") + e;
			const godot::String child_root_rel =
					p_root_rel_path.is_empty() ? e : p_root_rel_path + godot::String("/") + e;
			collect(abs, child_id_rel, child_root_rel, p_ns, p_mod_id, p_priority, p_id_overrides, p_filter, p_idx);
		} else {
			// Issue #6: skip filtered extensions (metadata/temp files) unless an
			// explicit id_override names this exact file.
			const godot::String root_rel =
					p_root_rel_path.is_empty() ? e : p_root_rel_path + godot::String("/") + e;
			// Explicit id_override beats path inference for this file.
			if (p_id_overrides != nullptr) {
				const auto it = p_id_overrides->find(root_rel);
				if (it != p_id_overrides->end()) {
					ResourceId rid;
					if (ResourceId::parse(it->second, rid)) {
						p_idx.add(rid, ProviderEntry{ p_mod_id, abs, p_priority, true, godot::Variant() });
						log_debug(godot::String("scan: id '") + rid.canonical() +
								godot::String("' (override) <- ") + abs +
								godot::String(" (mod=") + p_mod_id +
								godot::String(", pri=") + godot::String::num_int64(p_priority) +
								godot::String(")"));
					}
					continue; // consumed by the override — not also path-inferred
				}
			}
			if (!ext_allowed(p_filter, e)) {
				continue;
			}
			// Strip the extension -> id path, then map filesystem separators to
			// dots so ids are compact (`units.knight`), never file-path-like.
			godot::String id_path = p_id_rel_path.is_empty() ? e : p_id_rel_path + godot::String("/") + e;
			const int dot = id_path.rfind(".");
			if (dot > 0) {
				id_path = id_path.substr(0, dot);
			}
			id_path = id_path.replace("/", ".");
			if (!ResourceId::is_valid_path(id_path)) {
				continue;
			}
			ResourceId id;
			id.ns = p_ns;
			id.path = id_path;
			p_idx.add(id, ProviderEntry{ p_mod_id, abs, p_priority, false, godot::Variant() });
			log_debug(godot::String("scan: id '") + id.canonical() + godot::String("' <- ") + abs +
					godot::String(" (mod=") + p_mod_id +
					godot::String(", pri=") + godot::String::num_int64(p_priority) + godot::String(")"));
		}
	}
	dir->list_dir_end();
}

void scan_namespace_dirs(const godot::String &p_abs_dir, const godot::String &p_rel_dir,
		const godot::String &p_mod_id, int p_priority,
		const std::map<godot::String, godot::String> *p_id_overrides, const ExtFilter &p_filter,
		RegistryIndex &p_idx) {
	godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(p_abs_dir);
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	godot::String e;
	while ((e = dir->get_next()) != godot::String()) {
		if (e == "." || e == "..") {
			continue;
		}
		if (dir->current_is_dir() && ResourceId::is_valid_namespace(e)) {
			collect(p_abs_dir + godot::String("/") + e, godot::String(), p_rel_dir + godot::String("/") + e,
					e, p_mod_id, p_priority, p_id_overrides, p_filter, p_idx);
		}
	}
	dir->list_dir_end();
}

} // namespace

void Scanner::scan_implicit_dir(const godot::String &p_base_dir,
		const godot::String &p_mod_id, int p_priority, RegistryIndex &p_idx) {
	log_debug(godot::String("scan: dir ") + p_base_dir + godot::String(" (implicit, mod=") +
			p_mod_id + godot::String(")"));
	const ExtFilter filter = build_ext_filter();
	scan_namespace_dirs(p_base_dir, godot::String(), p_mod_id, p_priority, nullptr, filter, p_idx);
}

void Scanner::scan_dir_with_overrides(const godot::String &p_abs_dir, const godot::String &p_rel_dir,
		const godot::String &p_mod_id, int p_priority,
		const std::map<godot::String, godot::String> &p_id_overrides, RegistryIndex &p_idx) {
	log_debug(godot::String("scan: dir ") + p_abs_dir + godot::String(" (overrides, mod=") +
			p_mod_id + godot::String(", count=") + godot::String::num_int64((int64_t)p_id_overrides.size()) +
			godot::String(")"));
	const ExtFilter filter = build_ext_filter();
	scan_namespace_dirs(p_abs_dir, p_rel_dir, p_mod_id, p_priority, &p_id_overrides, filter, p_idx);
}

} // namespace vortarismodloader
