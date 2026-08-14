#include "vml_mod_loader.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "../core/loader_backend.h"
#include "../core/scanner.h"

namespace godot {

VMLModLoader *VMLModLoader::singleton = nullptr;

VMLModLoader::VMLModLoader() {
	scan_base_layer();
	initialized_ = true;
	print_line(String("VML: VortarisModLoader initialized (") + String::num_int64(registry_.provider_count()) +
			String(" ids indexed)"));
}

VMLModLoader::~VMLModLoader() {
}

void VMLModLoader::create_singleton() {
	ERR_FAIL_COND_MSG(singleton != nullptr, "VMLModLoader singleton already created.");
	singleton = memnew(VMLModLoader);
	Engine::get_singleton()->register_singleton("VML", singleton);
}

void VMLModLoader::free_singleton() {
	if (singleton) {
		Engine::get_singleton()->unregister_singleton("VML");
		memdelete(singleton);
		singleton = nullptr;
	}
}

VMLModLoader *VMLModLoader::get_singleton() {
	return singleton;
}

void VMLModLoader::scan_base_layer() {
	registry_.clear();
	overlays_.clear();
	// Base layer sits at priority 0; every enabled mod (later milestones) stacks above.
	overlays_.add_source("base", true);
	vortarismodloader::Scanner::scan_implicit_dir("res://assets", "base", 0, registry_);
	vortarismodloader::Scanner::scan_implicit_dir("res://data", "base", 0, registry_);
}

bool VMLModLoader::is_initialized() const {
	return initialized_;
}

bool VMLModLoader::has(const String &p_id) const {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	return registry_.has(rid);
}

String VMLModLoader::resolve(const String &p_id) const {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return String();
	}
	return registry_.resolve(rid);
}

Variant VMLModLoader::get_data(const String &p_id) const {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		ERR_PRINT(String("VML: invalid id '") + p_id + String("'"));
		return Variant();
	}
	const vortarismodloader::ProviderEntry *e = registry_.lookup(rid);
	if (e == nullptr) {
		ERR_PRINT(String("VML: unknown id '") + p_id + String("'"));
		return Variant();
	}
	const String ext = e->physical_path.get_extension().to_lower();
	if (ext == "json" || ext == "csv") {
		return vortarismodloader::LoaderBackend::load_data(e->physical_path);
	}
	return vortarismodloader::LoaderBackend::load_resource(e->physical_path);
}

Ref<Resource> VMLModLoader::get_resource(const String &p_id) const {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		ERR_PRINT(String("VML: invalid id '") + p_id + String("'"));
		return Ref<Resource>();
	}
	const vortarismodloader::ProviderEntry *e = registry_.lookup(rid);
	if (e == nullptr) {
		ERR_PRINT(String("VML: unknown id '") + p_id + String("'"));
		return Ref<Resource>();
	}
	return vortarismodloader::LoaderBackend::load_resource(e->physical_path);
}

bool VMLModLoader::register_id(const String &p_id, const String &p_path) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		ERR_PRINT(String("VML: invalid id '") + p_id + String("'"));
		return false;
	}
	// Explicit registration beats implicit providers at the same priority.
	registry_.add(rid, vortarismodloader::ProviderEntry{ "__explicit__", p_path, 0, true });
	explicit_paths_[{ rid.ns, rid.path }] = p_path;
	return true;
}

bool VMLModLoader::unregister_id(const String &p_id) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	const auto it = explicit_paths_.find({ rid.ns, rid.path });
	if (it == explicit_paths_.end()) {
		return false;
	}
	const bool ok = registry_.remove_provider(rid, "__explicit__", it->second);
	explicit_paths_.erase(it);
	return ok;
}

Dictionary VMLModLoader::list_ids(const String &p_prefix) const {
	Dictionary out;
	for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
		const String canonical = id.canonical();
		if (!p_prefix.is_empty() && !canonical.begins_with(p_prefix)) {
			continue;
		}
		if (!out.has(id.ns)) {
			out[id.ns] = PackedStringArray();
		}
		PackedStringArray arr = out[id.ns];
		arr.push_back(id.path);
		out[id.ns] = arr;
	}
	return out;
}

PackedStringArray VMLModLoader::list_namespaces() const {
	PackedStringArray out;
	for (const String &ns : registry_.namespaces()) {
		out.push_back(ns);
	}
	return out;
}

void VMLModLoader::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_initialized"), &VMLModLoader::is_initialized);
	ClassDB::bind_method(D_METHOD("has", "id"), &VMLModLoader::has);
	ClassDB::bind_method(D_METHOD("resolve", "id"), &VMLModLoader::resolve);
	ClassDB::bind_method(D_METHOD("get_data", "id"), &VMLModLoader::get_data);
	ClassDB::bind_method(D_METHOD("get_resource", "id"), &VMLModLoader::get_resource);
	ClassDB::bind_method(D_METHOD("register", "id", "path"), &VMLModLoader::register_id);
	ClassDB::bind_method(D_METHOD("unregister", "id"), &VMLModLoader::unregister_id);
	ClassDB::bind_method(D_METHOD("list_ids", "prefix"), &VMLModLoader::list_ids, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("list_namespaces"), &VMLModLoader::list_namespaces);
}

} // namespace godot
