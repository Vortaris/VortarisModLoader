#include "scanner.h"

#include <godot_cpp/classes/dir_access.hpp>

#include "debug_log.h"
#include "resource_id.h"

namespace vortarismodloader {

namespace {

// Recursively register every file under `p_abs_dir`. `p_id_rel_path` is the path
// below the namespace segment (id path, before extension stripping);
// `p_root_rel_path` is the full path relative to the mod root (used to match
// id_overrides keys). `p_id_overrides` may be null (base-layer / no overrides).
void collect(const godot::String &p_abs_dir, const godot::String &p_id_rel_path,
		const godot::String &p_root_rel_path, const godot::String &p_ns,
		const godot::String &p_mod_id, int p_priority,
		const std::map<godot::String, godot::String> *p_id_overrides, RegistryIndex &p_idx) {
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
			collect(abs, child_id_rel, child_root_rel, p_ns, p_mod_id, p_priority, p_id_overrides, p_idx);
		} else {
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
		const std::map<godot::String, godot::String> *p_id_overrides, RegistryIndex &p_idx) {
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
					e, p_mod_id, p_priority, p_id_overrides, p_idx);
		}
	}
	dir->list_dir_end();
}

} // namespace

void Scanner::scan_implicit_dir(const godot::String &p_base_dir,
		const godot::String &p_mod_id, int p_priority, RegistryIndex &p_idx) {
	log_debug(godot::String("scan: dir ") + p_base_dir + godot::String(" (implicit, mod=") +
			p_mod_id + godot::String(")"));
	scan_namespace_dirs(p_base_dir, godot::String(), p_mod_id, p_priority, nullptr, p_idx);
}

void Scanner::scan_dir_with_overrides(const godot::String &p_abs_dir, const godot::String &p_rel_dir,
		const godot::String &p_mod_id, int p_priority,
		const std::map<godot::String, godot::String> &p_id_overrides, RegistryIndex &p_idx) {
	log_debug(godot::String("scan: dir ") + p_abs_dir + godot::String(" (overrides, mod=") +
			p_mod_id + godot::String(", count=") + godot::String::num_int64((int64_t)p_id_overrides.size()) +
			godot::String(")"));
	scan_namespace_dirs(p_abs_dir, p_rel_dir, p_mod_id, p_priority, &p_id_overrides, p_idx);
}

} // namespace vortarismodloader
