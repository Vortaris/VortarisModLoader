#include "scanner.h"

#include <godot_cpp/classes/dir_access.hpp>

#include "registry_index.h"
#include "resource_id.h"

namespace vortarismodloader {

namespace {

// Recursively register every file under `p_abs_dir`. `p_rel_path` is the path
// below the namespace segment (id path, before extension stripping).
void collect(const godot::String &p_abs_dir, const godot::String &p_rel_path,
		const godot::String &p_ns, const godot::String &p_mod_id, int p_priority,
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
			const godot::String child_rel = p_rel_path.is_empty() ? e : p_rel_path + godot::String("/") + e;
			collect(abs, child_rel, p_ns, p_mod_id, p_priority, p_idx);
		} else {
			// Strip the extension -> id path.
			godot::String id_path = p_rel_path.is_empty() ? e : p_rel_path + godot::String("/") + e;
			const int dot = id_path.rfind(".");
			if (dot > 0) {
				id_path = id_path.substr(0, dot);
			}
			if (!ResourceId::is_valid_path(id_path)) {
				continue;
			}
			ResourceId id;
			id.ns = p_ns;
			id.path = id_path;
			p_idx.add(id, ProviderEntry{ p_mod_id, abs, p_priority, false });
		}
	}
	dir->list_dir_end();
}

} // namespace

void Scanner::scan_implicit_dir(const godot::String &p_base_dir,
		const godot::String &p_mod_id, int p_priority, RegistryIndex &p_idx) {
	godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(p_base_dir);
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
			collect(p_base_dir + godot::String("/") + e, godot::String(), e, p_mod_id, p_priority, p_idx);
		}
	}
	dir->list_dir_end();
}

} // namespace vortarismodloader
