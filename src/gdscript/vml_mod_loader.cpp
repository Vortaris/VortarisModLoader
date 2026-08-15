#include "vml_mod_loader.h"

#include <algorithm>
#include <functional>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/print_string.hpp>

#include "vml_hot_reloader.h"

#include "../core/dependency_graph.h"
#include "../core/discovery.h"
#include "../core/loader_backend.h"
#include "../core/scanner.h"
#include "../core/zip_installer.h"

namespace godot {

VMLModLoader *VMLModLoader::singleton = nullptr;

VMLModLoader::VMLModLoader() {
	scan_base_layer();
	scan_mods();
	initialized_ = true;

	// Unified load: honor vortarismodloader/database_mode ("data"/"all"/"off").
	database_mode_ = mode_from_string(godot::Variant(
			ProjectSettings::get_singleton()->get_setting("vortarismodloader/database_mode", "data")));
	if (database_mode_ != DatabaseMode::OFF) {
		preload_database();
	}

	print_line(String("VML: VortarisModLoader initialized (") + String::num_int64(registry_.provider_count()) +
			String(" ids, ") + String::num_int64((int64_t)mods_.size()) + String(" mods, db=") +
			mode_to_string(database_mode_) + String(")"));

	// Auto-finish startup (project setting): instantiate mod_main after autoload
	// _ready without the game having to call finish_startup() itself.
	if (ProjectSettings::get_singleton()->get_setting("vortarismodloader/auto_finish_startup", false)) {
		finish_startup_auto();
	}
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
	mods_.clear();
	load_order_.clear();
	explicit_paths_.clear();
	overlays_.add_source("base", true);
	vortarismodloader::Scanner::scan_implicit_dir("res://assets", "base", 0, registry_);
	vortarismodloader::Scanner::scan_implicit_dir("res://data", "base", 0, registry_);
}

void VMLModLoader::scan_mods() {
	startup_validation_done_ = false; // mods_ is rebuilt; validation must re-run
	// 1) Discover from the configured mod roots (default res://mods-unpacked and
	//    user://vml/mods), honoring the export policy and the user-mod scan switch.
	std::vector<vortarismodloader::DiscoveredMod> discovered;
	const String export_mode = ProjectSettings::get_singleton()->get_setting(
			"vortarismodloader/export_mods", "embedded");
	const bool scan_user = ProjectSettings::get_singleton()->get_setting(
			"vortarismodloader/scan_user_mods", true);
	const PackedStringArray roots = mod_roots();
	for (int i = 0; i < roots.size(); i++) {
		const String root = roots[i];
		const bool is_user_root = !root.begins_with("res://");
		if (export_mode == "none") {
			break; // no mod scanning at all
		}
		if (export_mode == "external" && !is_user_root) {
			continue; // external-only: skip embedded res:// roots
		}
		if (is_user_root && !scan_user) {
			continue; // scan_user_mods=false skips user roots
		}
		vortarismodloader::DiscoveryScanner::scan_mod_dirs(root, discovered);
	}

	// 2) Parse + validate manifests; a zip with a duplicate id is skipped (unpacked wins).
	std::vector<vortarismodloader::ModManifest> valid_manifests;
	for (const vortarismodloader::DiscoveredMod &d : discovered) {
		ModRecord rec;
		rec.root = d.root;
		rec.from_zip = !d.root.begins_with("res://");
		vortarismodloader::ManifestParser::load(d.manifest_path, rec.manifest);
		for (const String &err : rec.manifest.errors) {
			rec.errors.push_back(err);
		}
		if (find_mod(rec.manifest.id) != nullptr) {
			continue; // duplicate id: keep the first (unpacked) record
		}
		if (!rec.manifest.valid()) {
			rec.enabled = false;
		} else {
			valid_manifests.push_back(rec.manifest);
		}
		mods_.push_back(std::move(rec));
	}

	// 3) User profile overrides the default enabled state.
	load_profile();
	// Enforce dependency consistency from the profile: a mod whose required dep
	// is disabled gets disabled too (matches runtime cascade-enable semantics).
	for (ModRecord &rec : mods_) {
		if (!rec.enabled) {
			continue;
		}
		for (const String &dep : rec.manifest.deps) {
			String dep_id, op, want;
			vortarismodloader::DependencyGraph::parse_dependency(dep, dep_id, op, want);
			if (!is_mod_enabled(dep_id)) {
				rec.enabled = false;
				rec.errors.push_back(String("dependency disabled: ") + dep_id);
				break;
			}
		}
	}

	// 4) Dependency-sorted load order (only valid mods participate).
	std::vector<String> order;
	std::vector<String> order_errors;
	vortarismodloader::DependencyGraph::compute_order(valid_manifests, order, order_errors);

	// 5) Load order = the dependency-sorted topological order, NOT discovery order.
	load_order_.clear();
	for (const String &id : order) {
		load_order_.push_back(id);
	}
	for (const String &err : order_errors) {
		const int colon = err.find(":");
		const String id = colon > 0 ? err.substr(0, colon) : String();
		if (ModRecord *rec = find_mod(id)) {
			rec->errors.push_back(err);
			rec->enabled = false;
		}
	}

	// 6) Stack enabled mods above base and scan their content.
	for (const String &id : load_order_) {
		ModRecord *rec = find_mod(id);
		if (rec != nullptr && rec->enabled) {
			scan_mod_content(*rec);
		}
	}
}

bool VMLModLoader::is_initialized() const {
	return initialized_;
}

bool VMLModLoader::has(const String &p_id) const {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	return registry_.has(rid) || is_reserved(rid) || database_.has(rid);
}

bool VMLModLoader::has_data(const String &p_id) const {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	return database_.has(rid);
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
	// Fast path: already resident in the unified database.
	if (database_mode_ != DatabaseMode::OFF) {
		Variant cached;
		if (database_.get(rid, cached)) {
			return cached;
		}
	}
	const vortarismodloader::ProviderEntry *e = registry_.lookup(rid);
	if (e == nullptr) {
		ERR_PRINT(String("VML: unknown id '") + p_id + String("'"));
		return Variant();
	}
	Variant val = provider_value(*e);
	if (database_mode_ != DatabaseMode::OFF) {
		database_.set(rid, val, e->physical_path, e->mod_id);
	}
	return val;
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
	registry_.add(rid, vortarismodloader::ProviderEntry{ "__explicit__", p_path, 0, true, Variant() });
	explicit_paths_[{ rid.ns, rid.path }] = p_path;
	refresh_database_entry(rid); // stale DB cache must follow the new route
	return true;
}

bool VMLModLoader::register_id(const String &p_id, const Variant &p_value, int p_priority) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		ERR_PRINT(String("VML: invalid id '") + p_id + String("'"));
		return false;
	}
	// Value provider: empty physical_path marks it; priority defaults to 0 so mod
	// providers (priority > 0) override it, matching the registry layer.
	registry_.add(rid, vortarismodloader::ProviderEntry{ "__explicit__", String(), p_priority, true, p_value });
	refresh_database_entry(rid);
	return true;
}

bool VMLModLoader::unregister_id(const String &p_id) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	bool ok = false;
	// Path-based explicit registration first.
	const auto it = explicit_paths_.find({ rid.ns, rid.path });
	if (it != explicit_paths_.end()) {
		ok = registry_.remove_provider(rid, "__explicit__", it->second);
		explicit_paths_.erase(it);
	} else {
		// Value-based explicit registration (empty path).
		ok = registry_.remove_provider(rid, "__explicit__", String());
	}
	if (ok) {
		refresh_database_entry(rid); // drop the stale cached value
	}
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

PackedStringArray VMLModLoader::list_ids_in_namespace(const String &p_ns) const {
	PackedStringArray out;
	if (!vortarismodloader::ResourceId::is_valid_namespace(p_ns)) {
		return out;
	}
	for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
		if (id.ns == p_ns) {
			out.push_back(id.canonical());
		}
	}
	out.sort();
	return out;
}

int VMLModLoader::count_ids(const String &p_prefix) const {
	int n = 0;
	for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
		if (p_prefix.is_empty() || id.canonical().begins_with(p_prefix)) {
			n++;
		}
	}
	return n;
}

void VMLModLoader::preload_database() {
	database_.clear();
	if (database_mode_ == DatabaseMode::OFF) {
		return;
	}
	for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
		const vortarismodloader::ProviderEntry *e = registry_.lookup(id);
		if (e == nullptr) {
			continue;
		}
		const String ext = e->physical_path.get_extension().to_lower();
		const bool is_value = e->physical_path.is_empty(); // value providers always load
		const bool is_data = is_value || is_data_extension(ext);
		if (database_mode_ == DatabaseMode::DATA && !is_data) {
			continue;
		}
		const Variant val = provider_value(*e);
		if (val.get_type() != Variant::NIL) {
			database_.set(id, val, e->physical_path, e->mod_id);
		}
	}
	emit_signal("database_loaded");
}

void VMLModLoader::reload_database() {
	preload_in_flight_ = false; // cancel any in-flight async chain
	pending_ids_.clear();
	preload_index_ = 0;
	preload_database();
}

Dictionary VMLModLoader::get_all(const String &p_prefix) const {
	Dictionary out;
	// 1) Registry-provided ids (values lazily resolved; reserved-only ids have no
	//    provider, so they carry no value and are skipped).
	for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
		const String canonical = id.canonical();
		if (!p_prefix.is_empty() && !canonical.begins_with(p_prefix)) {
			continue;
		}
		Variant v;
		if (database_.get(id, v)) {
			out[canonical] = v;
			continue;
		}
		const vortarismodloader::ProviderEntry *e = registry_.lookup(id);
		if (e == nullptr) {
			continue;
		}
		out[canonical] = provider_value(*e);
	}
	// 2) Database-only entries (set_data without a provider).
	for (const vortarismodloader::ResourceId &id : database_.loaded_ids()) {
		const String canonical = id.canonical();
		if (out.has(canonical)) {
			continue;
		}
		if (!p_prefix.is_empty() && !canonical.begins_with(p_prefix)) {
			continue;
		}
		Variant v;
		if (database_.get(id, v)) {
			out[canonical] = v;
		}
	}
	return out;
}

bool VMLModLoader::set_data(const String &p_id, const Variant &p_value, bool p_persist) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		ERR_PRINT(String("VML: invalid id '") + p_id + String("'"));
		return false;
	}
	if (p_persist) {
		// Project-level persisted value: __registry__ provider at priority 0 so a
		// mod (priority > 0) still overrides it. The DB cache is refreshed to match
		// the winning provider.
		registry_map_[{ rid.ns, rid.path }] = RegistryEntry{ String(), String(), String(), true, p_value };
		registry_.add(rid, vortarismodloader::ProviderEntry{ "__registry__", String(), 0, true, p_value });
		refresh_database_entry(rid);
		const Error err = save_registry();
		if (err != OK) {
			ERR_PRINT(String("VML: failed to persist registry: ") + String::num_int64((int64_t)err));
		}
	} else {
		database_.set(rid, p_value, String(), "__runtime__");
	}
	emit_entry_changed(rid);
	return true;
}

bool VMLModLoader::delete_data(const String &p_id) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid) || !database_.has(rid)) {
		return false;
	}
	database_.erase(rid);
	emit_entry_changed(rid);
	return true;
}

String VMLModLoader::get_database_mode() const {
	return mode_to_string(database_mode_);
}

bool VMLModLoader::set_database_mode(const String &p_mode) {
	const DatabaseMode m = mode_from_string(p_mode);
	if (m != database_mode_) {
		database_mode_ = m;
		ProjectSettings::get_singleton()->set_setting("vortarismodloader/database_mode", mode_to_string(m));
		reload_database();
	}
	return true;
}

Variant VMLModLoader::get(const String &p_id) const {
	return get_data(p_id);
}

Ref<Resource> VMLModLoader::load(const String &p_id) const {
	return get_resource(p_id);
}

bool VMLModLoader::exists(const String &p_id) const {
	return has(p_id);
}

String VMLModLoader::get_mod_path(const String &p_mod_id) const {
	const ModRecord *rec = find_mod(p_mod_id);
	return rec != nullptr ? rec->root : String();
}

String VMLModLoader::get_mod_version(const String &p_mod_id) const {
	const ModRecord *rec = find_mod(p_mod_id);
	return rec != nullptr ? rec->manifest.version : String();
}

bool VMLModLoader::is_reserved(const vortarismodloader::ResourceId &p_id) const {
	for (const vortarismodloader::ResourceId &r : reserved_ids_) {
		if (r == p_id) {
			return true;
		}
	}
	return false;
}

bool VMLModLoader::set_registry_entry(const String &p_id, const String &p_path, const String &p_type,
		const String &p_description) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid) || p_path.is_empty()) {
		return false;
	}
	registry_map_[{ rid.ns, rid.path }] = RegistryEntry{ p_path, p_type, p_description };
	// Base-layer explicit route: priority 0, so any mod provider (priority > 0) overrides it.
	registry_.add(rid, vortarismodloader::ProviderEntry{ "__registry__", p_path, 0, true, Variant() });
	refresh_database_entry(rid); // stale DB cache must follow the new route
	log_verbose(String("registry entry: ") + p_id + String(" -> ") + p_path);
	return true;
}

Dictionary VMLModLoader::get_registry_entry(const String &p_id) const {
	Dictionary out;
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return out;
	}
	const auto it = registry_map_.find({ rid.ns, rid.path });
	if (it != registry_map_.end()) {
		out["path"] = it->second.path;
		out["type"] = it->second.type;
		out["description"] = it->second.description;
		if (it->second.has_value) {
			out["value"] = it->second.value;
		}
	}
	return out;
}

Dictionary VMLModLoader::get_registry() const {
	Dictionary out;
	for (const auto &kv : registry_map_) {
		const String canonical = String(kv.first.ns) + String(":") + String(kv.first.path);
		Dictionary entry;
		entry["path"] = kv.second.path;
		entry["type"] = kv.second.type;
		entry["description"] = kv.second.description;
		if (kv.second.has_value) {
			entry["value"] = kv.second.value;
		}
		out[canonical] = entry;
	}
	return out;
}

bool VMLModLoader::remove_registry_entry(const String &p_id) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	const auto it = registry_map_.find({ rid.ns, rid.path });
	if (it == registry_map_.end()) {
		return false;
	}
	registry_.remove_provider(rid, "__registry__", it->second.path);
	registry_map_.erase(it);
	refresh_database_entry(rid);
	return true;
}

Error VMLModLoader::save_registry(const String &p_path) {
	String path = p_path;
	if (path.is_empty()) {
		path = registry_path();
	}
	Dictionary data;
	for (const auto &kv : registry_map_) {
		const String canonical = String(kv.first.ns) + String(":") + String(kv.first.path);
		Dictionary entry;
		entry["path"] = kv.second.path;
		entry["type"] = kv.second.type;
		entry["description"] = kv.second.description;
		if (kv.second.has_value) {
			entry["value"] = kv.second.value;
		}
		data[canonical] = entry;
	}
	DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_null()) {
		// res:// read-only (exported build) -> fall back to user://vml/registry.json.
		if (path.begins_with("res://")) {
			const String fallback = "user://vml/registry.json";
			DirAccess::make_dir_recursive_absolute(fallback.get_base_dir());
			f = FileAccess::open(fallback, FileAccess::WRITE);
			if (f.is_null()) {
				return ERR_CANT_OPEN;
			}
			path = fallback;
		} else {
			return ERR_CANT_OPEN;
		}
	}
	f->store_string(JSON::stringify(data, "  "));
	f->close();
	print_line(String("VML: registry saved (") + String::num_int64((int64_t)registry_map_.size()) +
			String(" entries) -> ") + path);
	return OK;
}

Error VMLModLoader::load_registry(const String &p_path) {
	String path = p_path;
	if (path.is_empty()) {
		path = registry_path();
	}
	const auto load_file = [this](const String &p_f) -> Error {
		Ref<FileAccess> f = FileAccess::open(p_f, FileAccess::READ);
		if (f.is_null()) {
			return ERR_FILE_NOT_FOUND;
		}
		const Variant parsed = JSON::parse_string(f->get_as_text());
		if (parsed.get_type() != Variant::DICTIONARY) {
			return ERR_PARSE_ERROR;
		}
		const Dictionary data = parsed;
		for (const Variant &k : data.keys()) {
			if (data[k].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary entry = data[k];
			if (entry.has("value")) {
				// Restore a persisted value provider without re-saving the file.
				vortarismodloader::ResourceId rid;
				if (vortarismodloader::ResourceId::parse(String(k), rid)) {
					registry_map_[{ rid.ns, rid.path }] =
							RegistryEntry{ String(), String(), String(), true, entry["value"] };
					registry_.add(rid,
							vortarismodloader::ProviderEntry{ "__registry__", String(), 0, true, entry["value"] });
					refresh_database_entry(rid);
				}
				continue;
			}
			const String p = entry.get("path", String());
			if (p.is_empty()) {
				continue;
			}
			set_registry_entry(String(k), p, entry.get("type", String()), entry.get("description", String()));
		}
		print_line(String("VML: registry loaded from ") + p_f);
		return OK;
	};
	Error err = load_file(path);
	if (err != OK && path.begins_with("res://")) {
		// Exported res:// may lack the project-level file -> also try the user copy.
		err = load_file("user://vml/registry.json");
	}
	return err;
}

bool VMLModLoader::reroute(const String &p_id, const String &p_path) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	registry_.add(rid, vortarismodloader::ProviderEntry{ "__reroute__", p_path, INT32_MAX, true, Variant() });
	if (std::find(rerouted_ids_.begin(), rerouted_ids_.end(), rid) == rerouted_ids_.end()) {
		rerouted_ids_.push_back(rid);
	}
	refresh_database_entry(rid);
	emit_signal("database_entry_changed", p_id);
	return true;
}

bool VMLModLoader::clear_reroute(const String &p_id) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	const bool ok = registry_.remove_mod_provider(rid, "__reroute__");
	rerouted_ids_.erase(std::remove_if(rerouted_ids_.begin(), rerouted_ids_.end(),
									[&](const vortarismodloader::ResourceId &r) { return r == rid; }),
			rerouted_ids_.end());
	refresh_database_entry(rid);
	emit_signal("database_entry_changed", p_id);
	return ok;
}

Dictionary VMLModLoader::get_config(const String &p_mod_id) const {
	if (!vortarismodloader::ResourceId::is_valid_namespace(p_mod_id)) {
		return Dictionary();
	}
	Ref<FileAccess> f = FileAccess::open(String("user://vml/configs/") + p_mod_id + String(".json"), FileAccess::READ);
	if (f.is_null()) {
		return Dictionary();
	}
	const Variant parsed = JSON::parse_string(f->get_as_text());
	return parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
}

bool VMLModLoader::set_config(const String &p_mod_id, const Dictionary &p_values) {
	if (!vortarismodloader::ResourceId::is_valid_namespace(p_mod_id)) {
		return false;
	}
	DirAccess::make_dir_recursive_absolute("user://vml/configs");
	Ref<FileAccess> f = FileAccess::open(String("user://vml/configs/") + p_mod_id + String(".json"), FileAccess::WRITE);
	if (f.is_null()) {
		return false;
	}
	f->store_string(JSON::stringify(p_values, "  "));
	f->close();
	return true;
}

Dictionary VMLModLoader::get_config_schema(const String &p_mod_id) const {
	const ModRecord *rec = find_mod(p_mod_id);
	return rec != nullptr ? rec->manifest.config_schema : Dictionary();
}

Node *VMLModLoader::build_node(const String &p_id) {
	const Variant data = get_data(p_id);
	if (data.get_type() != Variant::DICTIONARY) {
		ERR_PRINT(String("VML: build_node requires a Dictionary at id '") + p_id + String("'"));
		return nullptr;
	}
	return build_node_from_dict(Dictionary(data), p_id);
}

Node *VMLModLoader::build_node_from_dict(const Dictionary &p_spec, const String &p_source_id) {
	const String type = p_spec.get("type", "Node");
	const String node_name = p_spec.get("name", type);
	Node *node = Object::cast_to<Node>(ClassDB::instantiate(StringName(type)));
	if (node == nullptr) {
		node = memnew(Node);
	}
	node->set_name(node_name);
	const Dictionary props = p_spec.get("properties", Dictionary());
	for (const Variant &k : props.keys()) {
		node->set(StringName(String(k)), props[k]);
	}
	const Array children = p_spec.get("children", Array());
	for (int i = 0; i < children.size(); i++) {
		if (children[i].get_type() == Variant::DICTIONARY) {
			Node *child = build_node_from_dict(children[i], p_source_id);
			if (child != nullptr) {
				node->add_child(child);
			}
		}
	}
	return node;
}

Dictionary VMLModLoader::validate() const {
	Array missing;
	int checked = 0;
	std::function<void(const Variant &)> scan = [&](const Variant &v) {
		if (v.get_type() == Variant::STRING) {
			vortarismodloader::ResourceId rid;
			const String s = v;
			if (vortarismodloader::ResourceId::parse(s, rid) && !has(s)) {
				missing.append(s);
			}
			checked++;
		} else if (v.get_type() == Variant::DICTIONARY) {
			for (const Variant &k : Dictionary(v).keys()) {
				scan(Dictionary(v)[k]);
			}
		} else if (v.get_type() == Variant::ARRAY) {
			for (const Variant &e : Array(v)) {
				scan(e);
			}
		}
	};
	for (const vortarismodloader::ResourceId &id : database_.loaded_ids()) {
		Variant val;
		if (database_.get(id, val)) {
			scan(val);
		}
	}
	Dictionary out;
	out["valid"] = missing.is_empty();
	out["checked"] = checked;
	out["missing"] = missing;
	return out;
}

void VMLModLoader::refresh_database_entry(const vortarismodloader::ResourceId &p_id) {
	database_.erase(p_id);
	if (database_mode_ == DatabaseMode::OFF) {
		return;
	}
	const vortarismodloader::ProviderEntry *e = registry_.lookup(p_id);
	if (e == nullptr) {
		return;
	}
	const String ext = e->physical_path.get_extension().to_lower();
	const bool is_value = e->physical_path.is_empty();
	const bool is_data = is_value || is_data_extension(ext);
	if (database_mode_ == DatabaseMode::DATA && !is_data) {
		return;
	}
	const Variant val = provider_value(*e);
	if (val.get_type() != Variant::NIL) {
		database_.set(p_id, val, e->physical_path, e->mod_id);
	}
}

Variant VMLModLoader::load_entry_value(const vortarismodloader::ProviderEntry &p_e) const {
	const String ext = p_e.physical_path.get_extension().to_lower();
	if (ext == "json" || ext == "csv") {
		return vortarismodloader::LoaderBackend::load_data(p_e.physical_path);
	}
	return vortarismodloader::LoaderBackend::load_resource(p_e.physical_path);
}

Variant VMLModLoader::provider_value(const vortarismodloader::ProviderEntry &p_e) const {
	if (p_e.physical_path.is_empty()) {
		return p_e.value; // value provider (register_id / persisted set_data)
	}
	return load_entry_value(p_e);
}

String VMLModLoader::registry_path() const {
	const Variant configured = ProjectSettings::get_singleton()->get_setting(
			"vortarismodloader/registry_path", "res://vml/registry.json");
	if (configured.get_type() == Variant::STRING) {
		const String s = String(configured).strip_edges();
		if (!s.is_empty()) {
			return s;
		}
	}
	return "res://vml/registry.json";
}

String VMLModLoader::data_type_for(const String &p_path) const {
	const String ext = p_path.get_extension().to_lower();
	if (ext == "json" || ext == "csv" || ext == "tres" || ext == "res") {
		return "data";
	}
	if (ext == "tscn" || ext == "scn") {
		return "scene";
	}
	if (ext == "gd" || ext == "cs") {
		return "script";
	}
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "bmp" || ext == "tga") {
		return "image";
	}
	if (ext == "wav" || ext == "mp3" || ext == "ogg") {
		return "audio";
	}
	if (ext == "ttf" || ext == "otf") {
		return "font";
	}
	return "resource";
}

Dictionary VMLModLoader::get_id_info(const String &p_id) const {
	Dictionary info;
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		info["valid"] = false;
		return info;
	}
	info["valid"] = true;
	const vortarismodloader::ProviderEntry *e = registry_.lookup(rid);
	info["resolved"] = e != nullptr;
	if (e != nullptr) {
		info["path"] = e->physical_path;
		info["provider_mod"] = e->mod_id;
		info["priority"] = e->priority;
		info["explicit"] = e->explicit_;
		info["data_type"] = e->physical_path.is_empty() ? String("value") : data_type_for(e->physical_path);
	} else {
		info["data_type"] = "";
	}
	info["preloaded"] = database_.has(rid);
	info["reserved"] = is_reserved(rid);
	info["type"] = get_id_type(p_id);
	return info;
}

bool VMLModLoader::set_id_type(const String &p_id, const String &p_type) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	if (p_type.is_empty()) {
		id_types_.erase({ rid.ns, rid.path });
	} else {
		id_types_[{ rid.ns, rid.path }] = p_type;
	}
	return true;
}

String VMLModLoader::get_id_type(const String &p_id) const {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return String();
	}
	const auto it = id_types_.find({ rid.ns, rid.path });
	return it == id_types_.end() ? String() : it->second;
}

PackedStringArray VMLModLoader::list_ids_by_type(const String &p_type) const {
	PackedStringArray out;
	for (const auto &kv : id_types_) {
		if (kv.second == p_type) {
			out.push_back(String(kv.first.ns) + String(":") + String(kv.first.path));
		}
	}
	out.sort();
	return out;
}

bool VMLModLoader::reserve(const String &p_id) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	if (!is_reserved(rid)) {
		reserved_ids_.push_back(rid);
	}
	return true;
}

bool VMLModLoader::unreserve(const String &p_id) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return false;
	}
	const size_t before = reserved_ids_.size();
	reserved_ids_.erase(std::remove_if(reserved_ids_.begin(), reserved_ids_.end(),
									[&](const vortarismodloader::ResourceId &r) { return r == rid; }),
			reserved_ids_.end());
	return reserved_ids_.size() != before;
}

String VMLModLoader::get_id_data_type(const String &p_id) const {
	const String path = resolve(p_id);
	return path.is_empty() ? String() : data_type_for(path);
}

bool VMLModLoader::preload_database_async() {
	if (preload_in_flight_) {
		return false; // already running
	}
	database_.clear(); // same semantics as the sync preload
	pending_ids_.clear();
	preload_index_ = 0;
	if (database_mode_ == DatabaseMode::OFF) {
		emit_signal("database_loaded");
		return true;
	}
	for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
		pending_ids_.push_back(id);
	}
	if (pending_ids_.empty()) {
		emit_signal("database_loaded");
		return true;
	}
	preload_in_flight_ = true;
	emit_signal("preload_progress", 0, (int)pending_ids_.size());
	call_deferred("_process_preload_batch");
	return true;
}

void VMLModLoader::_process_preload_batch() {
	const size_t batch = 32;
	const size_t end = std::min(preload_index_ + batch, pending_ids_.size());
	for (; preload_index_ < end; preload_index_++) {
		const vortarismodloader::ResourceId &id = pending_ids_[preload_index_];
		const vortarismodloader::ProviderEntry *e = registry_.lookup(id);
		if (e == nullptr) {
			continue;
		}
		const String ext = e->physical_path.get_extension().to_lower();
		const bool is_value = e->physical_path.is_empty();
		const bool is_data = is_value || is_data_extension(ext);
		if (database_mode_ == DatabaseMode::DATA && !is_data) {
			continue;
		}
		const Variant val = provider_value(*e);
		if (val.get_type() != Variant::NIL) {
			database_.set(id, val, e->physical_path, e->mod_id);
		}
	}
	emit_signal("preload_progress", (int)preload_index_, (int)pending_ids_.size());
	if (preload_index_ >= pending_ids_.size()) {
		pending_ids_.clear();
		preload_in_flight_ = false;
		emit_signal("database_loaded");
	} else {
		call_deferred("_process_preload_batch");
	}
}

VMLModLoader::DatabaseMode VMLModLoader::mode_from_string(const String &p_mode) const {
	if (p_mode == "all") {
		return DatabaseMode::ALL;
	}
	if (p_mode == "off") {
		return DatabaseMode::OFF;
	}
	return DatabaseMode::DATA;
}

String VMLModLoader::mode_to_string(DatabaseMode p_mode) const {
	if (p_mode == DatabaseMode::ALL) {
		return "all";
	}
	if (p_mode == DatabaseMode::OFF) {
		return "off";
	}
	return "data";
}

bool VMLModLoader::is_data_extension(const String &p_ext) const {
	return p_ext == "json" || p_ext == "csv" || p_ext == "tres" || p_ext == "res";
}

void VMLModLoader::emit_entry_changed(const vortarismodloader::ResourceId &p_id) {
	emit_signal("database_entry_changed", p_id.canonical());
}

PackedStringArray VMLModLoader::get_mod_ids() const {
	PackedStringArray out;
	for (const ModRecord &rec : mods_) {
		out.push_back(rec.manifest.id);
	}
	return out;
}

PackedStringArray VMLModLoader::get_load_order() const {
	PackedStringArray out;
	for (const String &id : load_order_) {
		out.push_back(id);
	}
	return out;
}

PackedStringArray VMLModLoader::get_mod_errors(const String &p_mod_id) const {
	PackedStringArray out;
	for (const ModRecord &rec : mods_) {
		if (rec.manifest.id == p_mod_id) {
			for (const String &e : rec.errors) {
				out.push_back(e);
			}
			break;
		}
	}
	return out;
}

Dictionary VMLModLoader::get_mod_report(const String &p_mod_id) const {
	Dictionary out;
	Array errors;
	Array warnings;
	const ModRecord *rec = find_mod(p_mod_id);
	if (rec == nullptr) {
		errors.append(String("unknown mod: ") + p_mod_id);
	} else {
		for (const String &e : rec->errors) {
			errors.append(e);
		}
		for (const String &w : rec->warnings) {
			warnings.append(w);
		}
	}
	out["errors"] = errors;
	out["warnings"] = warnings;
	return out;
}

Dictionary VMLModLoader::get_errors_summary() const {
	Dictionary out;
	for (const ModRecord &rec : mods_) {
		if (rec.errors.empty() && rec.warnings.empty()) {
			continue; // only problematic mods
		}
		Dictionary entry;
		Array errs;
		for (const String &e : rec.errors) {
			errs.append(e);
		}
		Array warns;
		for (const String &w : rec.warnings) {
			warns.append(w);
		}
		entry["errors"] = errs;
		entry["warnings"] = warns;
		out[rec.manifest.id] = entry;
	}
	return out;
}

Dictionary VMLModLoader::get_startup_report() const {
	Dictionary out;
	PackedStringArray broken;
	Array errors;
	Array warnings;
	for (const ModRecord &rec : mods_) {
		if (!rec.errors.empty()) {
			broken.push_back(rec.manifest.id);
		}
		for (const String &e : rec.errors) {
			errors.append(String(rec.manifest.id) + String(": ") + e);
		}
		for (const String &w : rec.warnings) {
			warnings.append(String(rec.manifest.id) + String(": ") + w);
		}
	}
	out["broken_mods"] = broken;
	out["errors"] = errors;
	out["warnings"] = warnings;
	return out;
}

Dictionary VMLModLoader::validate_mod(const String &p_mod_id) const {
	Dictionary out;
	Array errors;
	Array warnings;
	int checked = 0;
	const ModRecord *rec = find_mod(p_mod_id);
	if (rec == nullptr) {
		errors.append(String("unknown mod: ") + p_mod_id);
		out["valid"] = false;
		out["errors"] = errors;
		out["warnings"] = warnings;
		out["checked"] = 0;
		return out;
	}
	// Manifest completeness.
	if (!rec->manifest.valid()) {
		for (const String &e : rec->manifest.errors) {
			errors.append(e);
		}
	}
	// main_script loadable (declared, or an implicit mod_main.gd).
	String main_script = rec->manifest.main_script;
	if (main_script.is_empty() && FileAccess::file_exists(rec->root + String("/mod_main.gd"))) {
		main_script = "mod_main.gd";
	}
	if (!main_script.is_empty()) {
		const String script_path = rec->root + String("/") + main_script;
		checked++;
		if (!FileAccess::file_exists(script_path)) {
			errors.append(String("mod_main missing: ") + script_path);
		} else {
			Ref<GDScript> script = ResourceLoader::get_singleton()->load(script_path);
			if (script.is_null()) {
				errors.append(String("failed to load mod_main: ") + script_path);
			}
		}
	}
	// Data JSON validity + "id" cross-check against the path-inferred id.
	for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
		for (const vortarismodloader::ProviderEntry &e : registry_.providers_for(id)) {
			if (e.mod_id != rec->manifest.id || e.physical_path.is_empty()) {
				continue;
			}
			const String ext = e.physical_path.get_extension().to_lower();
			if (ext != "json" && ext != "csv") {
				continue;
			}
			checked++;
			const Variant val = vortarismodloader::LoaderBackend::load_data(e.physical_path);
			if (val.get_type() == Variant::NIL) {
				errors.append(String("invalid data file: ") + e.physical_path);
				continue;
			}
			if (ext == "json" && val.get_type() == Variant::DICTIONARY) {
				const Dictionary d = val;
				if (d.has("id")) {
					const String json_id = String(d["id"]);
					if (!json_id.is_empty() && json_id != id.canonical()) {
						warnings.append(String("JSON id '") + json_id + String("' != path id '") +
								id.canonical() + String("' at ") + e.physical_path);
					}
				}
			}
		}
	}
	out["valid"] = errors.is_empty();
	out["errors"] = errors;
	out["warnings"] = warnings;
	out["checked"] = checked;
	return out;
}

void VMLModLoader::run_startup_validation() {
	for (ModRecord &rec : mods_) {
		const Dictionary result = validate_mod(rec.manifest.id);
		const Array errs = result["errors"];
		const Array warns = result["warnings"];
		for (int i = 0; i < errs.size(); i++) {
			const String e = String(errs[i]);
			if (std::find(rec.errors.begin(), rec.errors.end(), e) == rec.errors.end()) {
				rec.errors.push_back(e);
			}
		}
		for (int i = 0; i < warns.size(); i++) {
			const String w = String(warns[i]);
			if (std::find(rec.warnings.begin(), rec.warnings.end(), w) == rec.warnings.end()) {
				rec.warnings.push_back(w);
			}
		}
	}
}

bool VMLModLoader::can_cascade(const String &p_id, std::vector<String> &p_visited) const {
	if (std::find(p_visited.begin(), p_visited.end(), p_id) != p_visited.end()) {
		return false; // cycle guard
	}
	p_visited.push_back(p_id);
	const ModRecord *rec = find_mod(p_id);
	if (rec == nullptr) {
		return false;
	}
	if (rec->enabled) {
		return true;
	}
	for (const String &inc : rec->manifest.incompatibilities) {
		if (is_mod_enabled(inc)) {
			return false;
		}
	}
	for (const String &dep : rec->manifest.deps) {
		String dep_id, op, want;
		vortarismodloader::DependencyGraph::parse_dependency(dep, dep_id, op, want);
		if (!can_cascade(dep_id, p_visited)) {
			return false;
		}
	}
	return true;
}

bool VMLModLoader::runtime_deps_ok(ModRecord &p_rec, std::vector<String> &r_reason) const {
	bool ok = true;
	for (const String &inc : p_rec.manifest.incompatibilities) {
		if (is_mod_enabled(inc)) {
			r_reason.push_back(String("incompatible with enabled mod '") + inc + String("'"));
			ok = false;
		}
	}
	for (const String &dep : p_rec.manifest.deps) {
		String dep_id, op, want;
		vortarismodloader::DependencyGraph::parse_dependency(dep, dep_id, op, want);
		const ModRecord *dep_rec = find_mod(dep_id);
		if (dep_rec == nullptr) {
			r_reason.push_back(String("missing required dependency '") + dep_id + String("'"));
			ok = false;
			continue;
		}
		if (!dep_rec->enabled) {
			std::vector<String> visited;
			if (!can_cascade(dep_id, visited)) {
				r_reason.push_back(String("dependency cannot be enabled: ") + dep_id);
				ok = false;
				continue;
			}
		}
		if (!vortarismodloader::DependencyGraph::satisfies(op, dep_rec->manifest.version, want)) {
			r_reason.push_back(String("dependency '") + dep_id + String("' version ") +
					dep_rec->manifest.version + String(" does not satisfy ") + op + want);
			ok = false;
		}
	}
	return ok;
}

VMLModLoader::ModRecord *VMLModLoader::find_mod(const String &p_mod_id) {
	for (ModRecord &rec : mods_) {
		if (rec.manifest.id == p_mod_id) {
			return &rec;
		}
	}
	return nullptr;
}

const VMLModLoader::ModRecord *VMLModLoader::find_mod(const String &p_mod_id) const {
	for (const ModRecord &rec : mods_) {
		if (rec.manifest.id == p_mod_id) {
			return &rec;
		}
	}
	return nullptr;
}

bool VMLModLoader::is_mod_enabled(const String &p_mod_id) const {
	const ModRecord *rec = find_mod(p_mod_id);
	return rec != nullptr && rec->enabled;
}

bool VMLModLoader::is_mod_loaded(const String &p_mod_id) const {
	const ModRecord *rec = find_mod(p_mod_id);
	return rec != nullptr && rec->mod_main_instantiated;
}

Dictionary VMLModLoader::get_mod_dependencies(const String &p_mod_id) const {
	Dictionary out;
	const ModRecord *rec = find_mod(p_mod_id);
	if (rec == nullptr) {
		return out;
	}
	for (const String &dep : rec->manifest.deps) {
		String dep_id, op, want;
		vortarismodloader::DependencyGraph::parse_dependency(dep, dep_id, op, want);
		const ModRecord *dep_rec = find_mod(dep_id);
		Dictionary info;
		info["exists"] = dep_rec != nullptr;
		info["enabled"] = dep_rec != nullptr && dep_rec->enabled;
		out[dep_id] = info;
	}
	return out;
}

PackedStringArray VMLModLoader::get_mod_dependents(const String &p_mod_id) const {
	PackedStringArray out;
	for (const ModRecord &rec : mods_) {
		if (!rec.enabled || rec.manifest.id == p_mod_id) {
			continue;
		}
		for (const String &dep : rec.manifest.deps) {
			String dep_id, op, want;
			vortarismodloader::DependencyGraph::parse_dependency(dep, dep_id, op, want);
			if (dep_id == p_mod_id) {
				out.push_back(rec.manifest.id);
				break;
			}
		}
	}
	return out;
}

int VMLModLoader::mod_priority(const String &p_mod_id) const {
	for (int i = 0; i < (int)load_order_.size(); i++) {
		if (load_order_[i] == p_mod_id) {
			return i + 1; // base is 0
		}
	}
	return -1;
}

void VMLModLoader::scan_mod_content(ModRecord &p_rec) {
	// Priority comes from the dependency-sorted load order, so a re-enabled mod
	// always lands at the same priority it had at boot (never appended at the end).
	int pri = mod_priority(p_rec.manifest.id);
	if (pri < 0) {
		pri = overlays_.add_source(p_rec.manifest.id, false);
	} else {
		overlays_.add_source(p_rec.manifest.id, false);
	}
	for (const String &dir : p_rec.manifest.asset_dirs) {
		vortarismodloader::Scanner::scan_implicit_dir(p_rec.root + String("/") + dir,
				p_rec.manifest.id, pri, registry_);
	}
	for (const String &dir : p_rec.manifest.data_dirs) {
		vortarismodloader::Scanner::scan_implicit_dir(p_rec.root + String("/") + dir,
				p_rec.manifest.id, pri, registry_);
	}
	p_rec.content_scanned = true;

	// Preload this mod's data into the unified database per the current mode.
	if (database_mode_ != DatabaseMode::OFF) {
		for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
			const vortarismodloader::ProviderEntry *e = registry_.lookup(id);
			if (e == nullptr || e->mod_id != p_rec.manifest.id) {
				continue;
			}
			const String ext = e->physical_path.get_extension().to_lower();
			const bool is_value = e->physical_path.is_empty();
			const bool is_data = is_value || is_data_extension(ext);
			if (database_mode_ == DatabaseMode::DATA && !is_data) {
				continue;
			}
			const Variant val = provider_value(*e);
			if (val.get_type() != Variant::NIL) {
				database_.set(id, val, e->physical_path, e->mod_id);
			}
		}
	}
}

void VMLModLoader::destroy_mod_main(ModRecord &p_rec) {
	if (p_rec.mod_main_node != nullptr) {
		memdelete(p_rec.mod_main_node);
		p_rec.mod_main_node = nullptr;
	}
	p_rec.mod_main_instantiated = false;
}

bool VMLModLoader::activate_mod(const String &p_mod_id) {
	ModRecord *rec = find_mod(p_mod_id);
	if (rec == nullptr) {
		return false;
	}
	rec->errors.clear(); // a fresh activate starts clean; failures repopulate it
	rec->warnings.clear();
	if (rec->enabled && rec->content_scanned) {
		return true; // idempotent (a fresh install has enabled=true but no scan yet)
	}
	if (rec->activating) {
		// Dependency cycle: a mod can't enable itself through its deps.
		rec->errors.push_back(String("dependency cycle while enabling '") + p_mod_id + String("'"));
		return false;
	}
	rec->activating = true;
	// Runtime re-check of deps/incompatibilities/versions BEFORE cascade-enabling:
	// a mod can no longer sneak in with a broken or conflicting dependency chain.
	{
		std::vector<String> reason;
		if (!runtime_deps_ok(*rec, reason)) {
			for (const String &r : reason) {
				rec->errors.push_back(r);
			}
			rec->activating = false;
			return false;
		}
	}
	// Cascade-enable required dependencies first, so a mod always brings its
	// deps with it (user confusion was caused by silent enable failures).
	for (const String &dep : rec->manifest.deps) {
		String dep_id, op, want;
		vortarismodloader::DependencyGraph::parse_dependency(dep, dep_id, op, want);
		if (!is_mod_enabled(dep_id) && !activate_mod(dep_id)) {
			rec->errors.push_back(String("dependency cannot be enabled: ") + dep_id);
			rec->activating = false;
			return false;
		}
	}
	if (!rec->content_scanned) {
		scan_mod_content(*rec);
	}
	if (!rec->mod_main_instantiated) {
		instantiate_mod_main(*rec); // no-op for pure-data mods
	}
	rec->enabled = true;
	rec->activating = false;
	save_profile();
	print_line(String("VML: mod '") + p_mod_id + String("' enabled"));
	log_verbose(String("stacked content, mod_main instantiated"));
	emit_signal("mod_enabled", p_mod_id);
	if (hot_reloader_ != nullptr) {
		hot_reloader_->rescan(); // watch the newly enabled content tree
	}
	return true;
}

bool VMLModLoader::deactivate_mod(const String &p_mod_id) {
	ModRecord *rec = find_mod(p_mod_id);
	if (rec == nullptr) {
		return false;
	}
	rec->errors.clear(); // a fresh deactivate starts clean
	rec->warnings.clear();
	if (!rec->enabled) {
		return true; // idempotent
	}
	if (rec->disabling) {
		return false; // dependency-cycle guard
	}
	rec->disabling = true;
	// Cascade-disable mods that depend on this one first, so toggling a mod off
	// always works (its dependents go off with it) and can never strand a
	// half-broken dependency chain.
	for (const ModRecord &m : mods_) {
		if (!m.enabled || m.manifest.id == p_mod_id) {
			continue;
		}
		bool depends = false;
		for (const String &dep : m.manifest.deps) {
			String dep_id, op, want;
			vortarismodloader::DependencyGraph::parse_dependency(dep, dep_id, op, want);
			if (dep_id == p_mod_id) {
				depends = true;
				break;
			}
		}
		if (depends && !deactivate_mod(m.manifest.id)) {
			rec->errors.push_back(String("cannot disable '") + p_mod_id + String("': '") + m.manifest.id +
					String("' refused"));
			rec->disabling = false;
			return false;
		}
	}
	rec->disabling = false;

	hooks_.remove_mod(p_mod_id);
	destroy_mod_main(*rec);
	registry_.remove_mod(p_mod_id);
	database_.erase_mod(p_mod_id);
	rec->enabled = false;
	rec->content_scanned = false; // re-scan on the next activate
	save_profile();
	print_line(String("VML: mod '") + p_mod_id + String("' disabled"));
	log_verbose("hooks removed, content dropped");
	emit_signal("mod_unloaded", p_mod_id);
	emit_signal("mod_disabled", p_mod_id);
	if (hot_reloader_ != nullptr) {
		hot_reloader_->rescan(); // drop the disabled mod's dirs from the watcher
	}
	return true;
}

bool VMLModLoader::enable_mod(const String &p_mod_id) {
	return activate_mod(p_mod_id);
}

bool VMLModLoader::load_mod(const String &p_mod_id) {
	return activate_mod(p_mod_id);
}

bool VMLModLoader::disable_mod(const String &p_mod_id) {
	return deactivate_mod(p_mod_id);
}

bool VMLModLoader::unload_mod(const String &p_mod_id) {
	return deactivate_mod(p_mod_id);
}

Error VMLModLoader::install_mod_from_zip(const String &p_zip_path) {
	vortarismodloader::ModManifest m;
	const Error err = vortarismodloader::ZipInstaller::install(p_zip_path, "user://vml/mods", m);
	if (err != OK) {
		return err;
	}
	if (ModRecord *existing = find_mod(m.id)) {
		if (existing->from_zip) {
			// Reinstall/upgrade in place: keep the freshly extracted copy, refresh the record.
			deactivate_mod(m.id);
			existing->root = String("user://vml/mods/") + m.id;
			existing->manifest = m;
			existing->from_zip = true;
			existing->enabled = true;
			existing->content_scanned = false;
			existing->mod_main_instantiated = false;
			existing->errors.clear();
			return activate_mod(m.id) ? OK : ERR_UNAVAILABLE;
		}
		// res:// (unpacked) already provides this id — unpacked wins; drop the new copy.
		vortarismodloader::ZipInstaller::uninstall(String("user://vml/mods/") + m.id, "user://vml/trash");
		return ERR_ALREADY_EXISTS;
	}
	ModRecord rec;
	rec.root = String("user://vml/mods/") + m.id;
	rec.from_zip = true;
	rec.manifest = m;
	mods_.push_back(std::move(rec));
	load_order_.push_back(m.id);
	print_line(String("VML: installed mod '") + m.id + String("' from ") + p_zip_path);
	return activate_mod(m.id) ? OK : ERR_UNAVAILABLE;
}

Error VMLModLoader::uninstall_mod(const String &p_mod_id) {
	ModRecord *rec = find_mod(p_mod_id);
	if (rec == nullptr) {
		return ERR_DOES_NOT_EXIST;
	}
	if (rec->enabled && !deactivate_mod(p_mod_id)) {
		rec->errors.push_back(String("cannot uninstall '") + p_mod_id + String("': still active"));
		return ERR_UNAVAILABLE;
	}
	Error err = OK;
	if (rec->from_zip) {
		err = vortarismodloader::ZipInstaller::uninstall(rec->root, "user://vml/trash");
	}
	load_order_.erase(std::remove(load_order_.begin(), load_order_.end(), p_mod_id), load_order_.end());
	mods_.erase(std::remove_if(mods_.begin(), mods_.end(),
							[&](const ModRecord &m) { return m.manifest.id == p_mod_id; }),
			mods_.end());
	save_profile();
	print_line(String("VML: uninstalled mod '") + p_mod_id + String("'"));
	if (hot_reloader_ != nullptr) {
		hot_reloader_->rescan();
	}
	return err;
}

bool VMLModLoader::reload_mod(const String &p_mod_id) {
	ModRecord *rec = find_mod(p_mod_id);
	if (rec == nullptr) {
		return false;
	}
	// Drop hooks FIRST so re-instantiation can never double-register handlers.
	hooks_.remove_mod(p_mod_id);
	destroy_mod_main(*rec);
	registry_.remove_mod(p_mod_id);
	database_.erase_mod(p_mod_id);
	rec->content_scanned = false;
	rec->errors.clear();
	rec->warnings.clear();
	if (rec->enabled) {
		scan_mod_content(*rec); // refresh content in place
		if (!rec->mod_main_instantiated) {
			instantiate_mod_main(*rec);
		}
		for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
			const vortarismodloader::ProviderEntry *e = registry_.lookup(id);
			if (e != nullptr && e->mod_id == p_mod_id) {
				emit_signal("database_entry_changed", id.canonical());
			}
		}
	}
	if (hot_reloader_ != nullptr) {
		hot_reloader_->rescan();
	}
	emit_signal("mod_reloaded", p_mod_id);
	print_line(String("VML: mod '") + p_mod_id + String("' reloaded"));
	return true;
}

Dictionary VMLModLoader::get_mod_package_plan() const {
	Dictionary out;
	const String mode = ProjectSettings::get_singleton()->get_setting("vortarismodloader/export_mods", "embedded");
	out["embedded"] = mode == "embedded";
	out["external"] = mode == "external";
	out["scan_user_mods"] =
			ProjectSettings::get_singleton()->get_setting("vortarismodloader/scan_user_mods", true);
	return out;
}

bool VMLModLoader::set_export_policy(const String &p_mode, bool p_scan_user) {
	const String m = p_mode.to_lower();
	if (m != "embedded" && m != "external" && m != "none") {
		return false;
	}
	ProjectSettings::get_singleton()->set_setting("vortarismodloader/export_mods", m);
	ProjectSettings::get_singleton()->set_setting("vortarismodloader/scan_user_mods", p_scan_user);
	return true;
}

PackedStringArray VMLModLoader::mod_roots() const {
	PackedStringArray defaults;
	defaults.push_back("res://mods-unpacked");
	defaults.push_back("user://vml/mods");
	const Variant configured =
			ProjectSettings::get_singleton()->get_setting("vortarismodloader/mod_paths", defaults);
	PackedStringArray out;
	if (configured.get_type() == Variant::PACKED_STRING_ARRAY) {
		out = configured;
	} else if (configured.get_type() == Variant::ARRAY) {
		const Array a = configured;
		for (int i = 0; i < a.size(); i++) {
			if (a[i].get_type() == Variant::STRING) {
				out.push_back(String(a[i]));
			}
		}
	}
	return out;
}

PackedStringArray VMLModLoader::get_mod_roots() const {
	return mod_roots();
}

bool VMLModLoader::add_mod_root(const String &p_path) {
	if (p_path.is_empty()) {
		return false;
	}
	PackedStringArray roots = mod_roots();
	if (roots.has(p_path)) {
		return true;
	}
	roots.push_back(p_path);
	ProjectSettings::get_singleton()->set_setting("vortarismodloader/mod_paths", roots);
	return true;
}

bool VMLModLoader::remove_mod_root(const String &p_path) {
	PackedStringArray roots = mod_roots();
	const int idx = roots.find(p_path);
	if (idx < 0) {
		return false;
	}
	roots.remove_at(idx);
	ProjectSettings::get_singleton()->set_setting("vortarismodloader/mod_paths", roots);
	return true;
}

void VMLModLoader::log_verbose(const String &p_msg) const {
	if (ProjectSettings::get_singleton()->get_setting("vortarismodloader/verbose", false)) {
		print_line(String("VML[v] ") + p_msg);
	}
}

String VMLModLoader::owning_mod(const String &p_path) const {
	if (p_path.begins_with("res://assets/") || p_path.begins_with("res://data/")) {
		return "base";
	}
	for (const ModRecord &rec : mods_) {
		if (p_path.begins_with(rec.root + String("/"))) {
			return rec.manifest.id;
		}
	}
	return String();
}

Variant VMLModLoader::instantiate(const String &p_id) {
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
	if (ext != "tscn" && ext != "scn") {
		ERR_PRINT(String("VML: id '") + p_id + String("' is not a scene"));
		return Variant();
	}
	Ref<PackedScene> scene = vortarismodloader::LoaderBackend::load_resource(e->physical_path);
	if (scene.is_null()) {
		return Variant();
	}
	return scene->instantiate();
}

void VMLModLoader::reload_resources(const PackedStringArray &p_paths) {
	std::vector<String> affected;
	for (int i = 0; i < p_paths.size(); i++) {
		const String mid = owning_mod(p_paths[i]);
		if (!mid.is_empty() && std::find(affected.begin(), affected.end(), mid) == affected.end()) {
			affected.push_back(mid);
		}
	}
	for (const String &mid : affected) {
		if (mid == "base") {
			// Re-scan the base layer (res://assets + res://data) and refresh its data.
			registry_.remove_mod("base");
			database_.erase_mod("base");
			vortarismodloader::Scanner::scan_implicit_dir("res://assets", "base", 0, registry_);
			vortarismodloader::Scanner::scan_implicit_dir("res://data", "base", 0, registry_);
			if (database_mode_ != DatabaseMode::OFF) {
				for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
					const vortarismodloader::ProviderEntry *e = registry_.lookup(id);
					if (e == nullptr || e->mod_id != "base") {
						continue;
					}
					const String ext = e->physical_path.get_extension().to_lower();
					const bool is_data = is_data_extension(ext);
					if (database_mode_ == DatabaseMode::DATA && !is_data) {
						continue;
					}
					const Variant val = load_entry_value(*e);
					if (val.get_type() != Variant::NIL) {
						database_.set(id, val, e->physical_path, e->mod_id);
					}
					emit_signal("database_entry_changed", id.canonical());
				}
			}
			continue;
		}
		ModRecord *rec = find_mod(mid);
		if (rec == nullptr || !rec->enabled) {
			continue;
		}
		// Re-scan content (handles added/removed/renamed files), refresh data.
		registry_.remove_mod(mid);
		database_.erase_mod(mid);
		rec->content_scanned = false;
		scan_mod_content(*rec);
		if (database_mode_ != DatabaseMode::OFF) {
			// scan_mod_content() already refreshed this mod's entries; just notify.
			for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
				const vortarismodloader::ProviderEntry *e = registry_.lookup(id);
				if (e == nullptr || e->mod_id != mid) {
					continue;
				}
				emit_signal("database_entry_changed", id.canonical());
			}
		}
	}
	emit_signal("registry_rebuilt");
}

PackedStringArray VMLModLoader::get_content_roots() const {
	PackedStringArray roots;
	roots.push_back("res://assets");
	roots.push_back("res://data");
	for (const ModRecord &rec : mods_) {
		if (!rec.enabled) {
			continue;
		}
		for (const String &dir : rec.manifest.asset_dirs) {
			roots.push_back(rec.root + String("/") + dir);
		}
		for (const String &dir : rec.manifest.data_dirs) {
			roots.push_back(rec.root + String("/") + dir);
		}
	}
	return roots;
}

void VMLModLoader::start_hot_reload(double p_interval) {
	if (hot_reloader_ != nullptr) {
		return;
	}
	hot_reloader_ = memnew(VMLHotReloader);
	hot_reloader_->set_poll_interval(p_interval);
	hot_reloader_->set_name("VMLHotReloader");
	// The VML singleton itself is not in the scene tree, so _process would never
	// run on a child node. Attach the poller to the SceneTree root instead.
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (tree != nullptr) {
		tree->get_root()->add_child(hot_reloader_);
	} else {
		add_child(hot_reloader_);
	}
	hot_reloader_->rescan();
}

void VMLModLoader::rescan() {
	// Cancel any in-flight async preload chain.
	preload_in_flight_ = false;
	pending_ids_.clear();
	preload_index_ = 0;
	// Tear down runtime state first so nothing leaks or dangles.
	for (ModRecord &rec : mods_) {
		destroy_mod_main(rec);
	}
	hooks_.clear();
	explicit_paths_.clear();
	scan_base_layer(); // clears registry / overlays / mods_ / load_order_
	scan_mods();
	database_.clear();
	preload_database();
	// Re-validate after a re-scan so the error/warning summary stays accurate.
	if (ProjectSettings::get_singleton()->get_setting("vortarismodloader/validate_on_startup", true) &&
			!startup_validation_done_) {
		run_startup_validation();
		startup_validation_done_ = true;
	}
	// Re-instantiate mod_main for enabled mods (rescan destroyed them above);
	// otherwise every mod silently becomes "enabled but not loaded".
	for (ModRecord &rec : mods_) {
		if (rec.enabled && !rec.mod_main_instantiated) {
			instantiate_mod_main(rec); // no-op for pure-data mods
		}
	}
	if (hot_reloader_ != nullptr) {
		hot_reloader_->rescan();
	}
	emit_signal("registry_rebuilt");
}

void VMLModLoader::load_profile() {
	if (profile_loaded_) {
		return;
	}
	profile_loaded_ = true;
	Ref<FileAccess> f = FileAccess::open("user://vml/profile.json", FileAccess::READ);
	if (f.is_null()) {
		return;
	}
	Variant parsed = JSON::parse_string(f->get_as_text());
	if (parsed.get_type() != Variant::DICTIONARY) {
		return;
	}
	const Dictionary d = parsed;
	for (ModRecord &rec : mods_) {
		// Never let a profile re-enable a mod with an invalid manifest.
		if (!rec.manifest.valid()) {
			rec.enabled = false;
			continue;
		}
		if (d.has(rec.manifest.id) && d[rec.manifest.id].get_type() == Variant::BOOL) {
			rec.enabled = d[rec.manifest.id];
		}
	}
}

void VMLModLoader::save_profile() {
	Dictionary d;
	for (const ModRecord &rec : mods_) {
		d[rec.manifest.id] = rec.enabled;
	}
	DirAccess::make_dir_recursive_absolute("user://vml");
	Ref<FileAccess> f = FileAccess::open("user://vml/profile.json", FileAccess::WRITE);
	if (f.is_null()) {
		return;
	}
	f->store_string(JSON::stringify(d));
	f->close();
}

void VMLModLoader::instantiate_mod_main(ModRecord &p_rec) {
	if (p_rec.mod_main_instantiated) {
		return;
	}
	String main_script = p_rec.manifest.main_script;
	if (main_script.is_empty()) {
		// No entry declared. A bare mod_main.gd still counts (backward compat);
		// otherwise this is a pure-data mod — valid, nothing to instantiate.
		if (!FileAccess::file_exists(p_rec.root + String("/mod_main.gd"))) {
			return;
		}
		main_script = "mod_main.gd";
	}
	const String script_path = p_rec.root + String("/") + main_script;
	if (!FileAccess::file_exists(script_path)) {
		p_rec.errors.push_back(String("mod_main missing: ") + script_path);
		return;
	}
	Ref<GDScript> script = ResourceLoader::get_singleton()->load(script_path);
	if (script.is_null()) {
		p_rec.errors.push_back(String("failed to load mod_main: ") + script_path);
		return;
	}
	// _init() runs inside new(); attribute hooks registered there to this mod.
	active_mod_ = p_rec.manifest.id;
	Variant inst = script->call("new");
	active_mod_ = String();
	Node *node = Object::cast_to<Node>(inst);
	if (node == nullptr) {
		p_rec.errors.push_back(String("mod_main must extend Node: ") + script_path);
		return;
	}
	node->set_name(p_rec.manifest.id);
	add_child(node);
	p_rec.mod_main_node = node;
	p_rec.mod_main_instantiated = true;
	emit_signal("mod_loaded", p_rec.manifest.id);
}

void VMLModLoader::finish_startup() {
	if (startup_done_) {
		return;
	}
	startup_done_ = true;
	// Load the persisted content registry: legacy built-in res://registry.json,
	// then the project-level path (default res://vml/registry.json), then the
	// legacy user://vml/registry.json override (last wins on duplicate ids).
	load_registry("res://registry.json");
	load_registry(registry_path());
	load_registry("user://vml/registry.json");
	// Startup validation: mark problems, never throw or refuse to load. Runs here
	// (after the VML singleton is registered) so mod_main scripts parse cleanly.
	if (ProjectSettings::get_singleton()->get_setting("vortarismodloader/validate_on_startup", true) &&
			!startup_validation_done_) {
		run_startup_validation();
		startup_validation_done_ = true;
	}
	// Instantiate mod_main in dependency order (a dependent may rely on hooks/data
	// registered by its dependencies during _init).
	for (const String &id : load_order_) {
		ModRecord *rec = find_mod(id);
		if (rec != nullptr && rec->enabled) {
			instantiate_mod_main(*rec);
		}
	}
}

void VMLModLoader::finish_startup_auto() {
	if (startup_done_) {
		return;
	}
	call_deferred("_auto_finish_startup");
}

void VMLModLoader::_auto_finish_startup() {
	if (startup_done_) {
		return;
	}
	// The scene tree may not exist yet (VML is created at SCENE level, before
	// autoloads). Retry until it does so autoload _ready has already run.
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (tree == nullptr || tree->get_root() == nullptr) {
		call_deferred("_auto_finish_startup");
		return;
	}
	finish_startup();
}

bool VMLModLoader::is_startup_done() const {
	return startup_done_;
}

bool VMLModLoader::add_hook(const String &p_hook_id, const Callable &p_callable, int p_priority) {
	vortarismodloader::ResourceId hid;
	if (!vortarismodloader::ResourceId::parse(p_hook_id, hid) || !p_callable.is_valid()) {
		ERR_PRINT(String("VML: invalid hook '") + p_hook_id + String("'"));
		return false;
	}
	hooks_.add(hid, p_callable, active_mod_.is_empty() ? String("__runtime__") : active_mod_, p_priority);
	return true;
}

bool VMLModLoader::remove_hook(const String &p_hook_id, const Callable &p_callable) {
	vortarismodloader::ResourceId hid;
	if (!vortarismodloader::ResourceId::parse(p_hook_id, hid)) {
		return false;
	}
	return hooks_.remove(hid, p_callable);
}

void VMLModLoader::emit_hook(const String &p_hook_id, const Array &p_args) {
	vortarismodloader::ResourceId hid;
	if (vortarismodloader::ResourceId::parse(p_hook_id, hid)) {
		hooks_.emit(hid, p_args);
	}
}

Variant VMLModLoader::invoke_hook(const String &p_hook_id, const Array &p_args, const Variant &p_default) {
	vortarismodloader::ResourceId hid;
	if (!vortarismodloader::ResourceId::parse(p_hook_id, hid)) {
		return p_default;
	}
	return hooks_.invoke(hid, p_args, p_default);
}

Dictionary VMLModLoader::invoke_hook_ctx(const String &p_hook_id, const Dictionary &p_ctx,
		const Array &p_args) {
	vortarismodloader::ResourceId hid;
	if (!vortarismodloader::ResourceId::parse(p_hook_id, hid)) {
		return p_ctx;
	}
	return hooks_.invoke_ctx(hid, p_ctx, p_args);
}

bool VMLModLoader::check_hook(const String &p_hook_id, const Array &p_args) {
	vortarismodloader::ResourceId hid;
	if (!vortarismodloader::ResourceId::parse(p_hook_id, hid)) {
		return true;
	}
	return hooks_.check(hid, p_args);
}

bool VMLModLoader::register_hook_point(const String &p_hook_id, const String &p_description,
		const PackedStringArray &p_arg_types) {
	vortarismodloader::ResourceId hid;
	if (!vortarismodloader::ResourceId::parse(p_hook_id, hid)) {
		return false;
	}
	for (const HookPoint &hp : hook_points_) {
		if (hp.id == hid) {
			return true; // already declared
		}
	}
	hook_points_.push_back(HookPoint{ hid, p_description, p_arg_types });
	return true;
}

Dictionary VMLModLoader::list_hooks(const String &p_prefix) const {
	Dictionary out;
	for (const vortarismodloader::ResourceId &id : hooks_.all_hooks()) {
		const String canonical = id.canonical();
		if (!p_prefix.is_empty() && !canonical.begins_with(p_prefix)) {
			continue;
		}
		Dictionary info;
		info["count"] = hooks_.handler_count(id);
		PackedStringArray mods;
		for (const vortarismodloader::HookHandler &h : hooks_.handlers_for(id)) {
			if (!mods.has(h.mod_id)) {
				mods.push_back(h.mod_id);
			}
		}
		info["mods"] = mods;
		out[canonical] = info;
	}
	return out;
}

Dictionary VMLModLoader::list_hook_points(const String &p_prefix) const {
	Dictionary out;
	for (const HookPoint &hp : hook_points_) {
		const String canonical = hp.id.canonical();
		if (!p_prefix.is_empty() && !canonical.begins_with(p_prefix)) {
			continue;
		}
		Dictionary info;
		info["description"] = hp.description;
		info["arg_types"] = hp.arg_types;
		out[canonical] = info;
	}
	return out;
}

Array VMLModLoader::list_hook_handlers(const String &p_hook_id) const {
	Array out;
	vortarismodloader::ResourceId hid;
	if (!vortarismodloader::ResourceId::parse(p_hook_id, hid)) {
		return out;
	}
	// handlers_for is already in call order (priority desc, mod_id asc).
	for (const vortarismodloader::HookHandler &h : hooks_.handlers_for(hid)) {
		Dictionary d;
		d["mod_id"] = h.mod_id;
		d["priority"] = h.priority;
		out.append(d);
	}
	return out;
}

Dictionary VMLModLoader::list_providers(const String &p_id) const {
	Dictionary out;
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		return out;
	}
	Array providers;
	int best = -1;
	const std::vector<vortarismodloader::ProviderEntry> all = registry_.providers_for(rid);
	for (int i = 0; i < (int)all.size(); i++) {
		Dictionary d;
		d["mod_id"] = all[i].mod_id;
		d["path"] = all[i].physical_path;
		d["priority"] = all[i].priority;
		d["explicit"] = all[i].explicit_;
		providers.append(d);
		if (i == 0) {
			best = 0; // providers_for is already in provider_higher order
		}
	}
	out["providers"] = providers;
	out["best"] = best;
	return out;
}

void VMLModLoader::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_initialized"), &VMLModLoader::is_initialized);
	ClassDB::bind_method(D_METHOD("has", "id"), &VMLModLoader::has);
	ClassDB::bind_method(D_METHOD("resolve", "id"), &VMLModLoader::resolve);
	ClassDB::bind_method(D_METHOD("get_data", "id"), &VMLModLoader::get_data);
	ClassDB::bind_method(D_METHOD("get_resource", "id"), &VMLModLoader::get_resource);
	ClassDB::bind_method(D_METHOD("register", "id", "path"),
			static_cast<bool (VMLModLoader::*)(const String &, const String &)>(&VMLModLoader::register_id));
	ClassDB::bind_method(D_METHOD("register_id", "id", "value", "priority"),
			static_cast<bool (VMLModLoader::*)(const String &, const Variant &, int)>(&VMLModLoader::register_id),
			DEFVAL(0));
	ClassDB::bind_method(D_METHOD("unregister", "id"), &VMLModLoader::unregister_id);
	ClassDB::bind_method(D_METHOD("has_data", "id"), &VMLModLoader::has_data);
	ClassDB::bind_method(D_METHOD("list_ids", "prefix"), &VMLModLoader::list_ids, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("list_namespaces"), &VMLModLoader::list_namespaces);
	ClassDB::bind_method(D_METHOD("list_ids_in_namespace", "ns"), &VMLModLoader::list_ids_in_namespace);
	ClassDB::bind_method(D_METHOD("count_ids", "prefix"), &VMLModLoader::count_ids, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("get_mod_ids"), &VMLModLoader::get_mod_ids);
	ClassDB::bind_method(D_METHOD("get_load_order"), &VMLModLoader::get_load_order);
	ClassDB::bind_method(D_METHOD("get_mod_errors", "mod_id"), &VMLModLoader::get_mod_errors);

	ClassDB::bind_method(D_METHOD("preload_database"), &VMLModLoader::preload_database);
	ClassDB::bind_method(D_METHOD("reload_database"), &VMLModLoader::reload_database);
	ClassDB::bind_method(D_METHOD("get_all", "prefix"), &VMLModLoader::get_all, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("set_data", "id", "value", "persist"), &VMLModLoader::set_data, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("delete_data", "id"), &VMLModLoader::delete_data);
	ClassDB::bind_method(D_METHOD("get_database_mode"), &VMLModLoader::get_database_mode);
	ClassDB::bind_method(D_METHOD("set_database_mode", "mode"), &VMLModLoader::set_database_mode);
	ClassDB::bind_method(D_METHOD("get", "id"), &VMLModLoader::get);
	ClassDB::bind_method(D_METHOD("load", "id"), &VMLModLoader::load);
	ClassDB::bind_method(D_METHOD("exists", "id"), &VMLModLoader::exists);
	ClassDB::bind_method(D_METHOD("get_mod_path", "mod_id"), &VMLModLoader::get_mod_path);
	ClassDB::bind_method(D_METHOD("get_mod_version", "mod_id"), &VMLModLoader::get_mod_version);
	ClassDB::bind_method(D_METHOD("get_id_info", "id"), &VMLModLoader::get_id_info);
	ClassDB::bind_method(D_METHOD("set_id_type", "id", "type"), &VMLModLoader::set_id_type);
	ClassDB::bind_method(D_METHOD("get_id_type", "id"), &VMLModLoader::get_id_type);
	ClassDB::bind_method(D_METHOD("list_ids_by_type", "type"), &VMLModLoader::list_ids_by_type);
	ClassDB::bind_method(D_METHOD("reserve", "id"), &VMLModLoader::reserve);
	ClassDB::bind_method(D_METHOD("unreserve", "id"), &VMLModLoader::unreserve);
	ClassDB::bind_method(D_METHOD("get_id_data_type", "id"), &VMLModLoader::get_id_data_type);

	ClassDB::bind_method(D_METHOD("set_registry_entry", "id", "path", "type", "description"),
			&VMLModLoader::set_registry_entry, DEFVAL(""), DEFVAL(""));
	ClassDB::bind_method(D_METHOD("get_registry_entry", "id"), &VMLModLoader::get_registry_entry);
	ClassDB::bind_method(D_METHOD("get_registry"), &VMLModLoader::get_registry);
	ClassDB::bind_method(D_METHOD("remove_registry_entry", "id"), &VMLModLoader::remove_registry_entry);
	ClassDB::bind_method(D_METHOD("save_registry", "path"), &VMLModLoader::save_registry, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("load_registry", "path"), &VMLModLoader::load_registry, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("reroute", "id", "path"), &VMLModLoader::reroute);
	ClassDB::bind_method(D_METHOD("clear_reroute", "id"), &VMLModLoader::clear_reroute);
	ClassDB::bind_method(D_METHOD("get_config", "mod_id"), &VMLModLoader::get_config);
	ClassDB::bind_method(D_METHOD("set_config", "mod_id", "values"), &VMLModLoader::set_config);
	ClassDB::bind_method(D_METHOD("get_config_schema", "mod_id"), &VMLModLoader::get_config_schema);
	ClassDB::bind_method(D_METHOD("build_node", "id"), &VMLModLoader::build_node);
	ClassDB::bind_method(D_METHOD("validate"), &VMLModLoader::validate);

	ClassDB::bind_method(D_METHOD("preload_database_async"), &VMLModLoader::preload_database_async);
	ClassDB::bind_method(D_METHOD("_process_preload_batch"), &VMLModLoader::_process_preload_batch);

	ClassDB::bind_method(D_METHOD("finish_startup"), &VMLModLoader::finish_startup);
	ClassDB::bind_method(D_METHOD("finish_startup_auto"), &VMLModLoader::finish_startup_auto);
	ClassDB::bind_method(D_METHOD("_auto_finish_startup"), &VMLModLoader::_auto_finish_startup);
	ClassDB::bind_method(D_METHOD("is_startup_done"), &VMLModLoader::is_startup_done);
	ClassDB::bind_method(D_METHOD("add_hook", "hook_id", "callable", "priority"), &VMLModLoader::add_hook,
			DEFVAL(0));
	ClassDB::bind_method(D_METHOD("remove_hook", "hook_id", "callable"), &VMLModLoader::remove_hook);
	ClassDB::bind_method(D_METHOD("emit_hook", "hook_id", "args"), &VMLModLoader::emit_hook, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("invoke_hook", "hook_id", "args", "default"), &VMLModLoader::invoke_hook,
			DEFVAL(Array()), DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("invoke_hook_ctx", "hook_id", "ctx", "args"), &VMLModLoader::invoke_hook_ctx,
			DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("check_hook", "hook_id", "args"), &VMLModLoader::check_hook, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("register_hook_point", "hook_id", "description", "arg_types"),
			&VMLModLoader::register_hook_point, DEFVAL(PackedStringArray()));
	ClassDB::bind_method(D_METHOD("list_hooks", "prefix"), &VMLModLoader::list_hooks, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("list_hook_points", "prefix"), &VMLModLoader::list_hook_points, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("list_hook_handlers", "hook_id"), &VMLModLoader::list_hook_handlers);
	ClassDB::bind_method(D_METHOD("list_providers", "id"), &VMLModLoader::list_providers);

	ClassDB::bind_method(D_METHOD("is_mod_enabled", "mod_id"), &VMLModLoader::is_mod_enabled);
	ClassDB::bind_method(D_METHOD("is_mod_loaded", "mod_id"), &VMLModLoader::is_mod_loaded);
	ClassDB::bind_method(D_METHOD("get_mod_dependents", "mod_id"), &VMLModLoader::get_mod_dependents);
	ClassDB::bind_method(D_METHOD("get_mod_dependencies", "mod_id"), &VMLModLoader::get_mod_dependencies);
	ClassDB::bind_method(D_METHOD("enable_mod", "mod_id"), &VMLModLoader::enable_mod);
	ClassDB::bind_method(D_METHOD("disable_mod", "mod_id"), &VMLModLoader::disable_mod);
	ClassDB::bind_method(D_METHOD("load_mod", "mod_id"), &VMLModLoader::load_mod);
	ClassDB::bind_method(D_METHOD("unload_mod", "mod_id"), &VMLModLoader::unload_mod);
	ClassDB::bind_method(D_METHOD("install_mod_from_zip", "zip_path"), &VMLModLoader::install_mod_from_zip);
	ClassDB::bind_method(D_METHOD("uninstall_mod", "mod_id"), &VMLModLoader::uninstall_mod);
	ClassDB::bind_method(D_METHOD("reload_mod", "mod_id"), &VMLModLoader::reload_mod);
	ClassDB::bind_method(D_METHOD("validate_mod", "mod_id"), &VMLModLoader::validate_mod);
	ClassDB::bind_method(D_METHOD("get_mod_report", "mod_id"), &VMLModLoader::get_mod_report);
	ClassDB::bind_method(D_METHOD("get_errors_summary"), &VMLModLoader::get_errors_summary);
	ClassDB::bind_method(D_METHOD("get_startup_report"), &VMLModLoader::get_startup_report);
	ClassDB::bind_method(D_METHOD("get_mod_package_plan"), &VMLModLoader::get_mod_package_plan);
	ClassDB::bind_method(D_METHOD("set_export_policy", "mode", "scan_user"), &VMLModLoader::set_export_policy);
	ClassDB::bind_method(D_METHOD("get_mod_roots"), &VMLModLoader::get_mod_roots);
	ClassDB::bind_method(D_METHOD("add_mod_root", "path"), &VMLModLoader::add_mod_root);
	ClassDB::bind_method(D_METHOD("remove_mod_root", "path"), &VMLModLoader::remove_mod_root);

	ClassDB::bind_method(D_METHOD("instantiate", "id"), &VMLModLoader::instantiate);
	ClassDB::bind_method(D_METHOD("reload_resources", "paths"), &VMLModLoader::reload_resources);
	ClassDB::bind_method(D_METHOD("get_content_roots"), &VMLModLoader::get_content_roots);
	ClassDB::bind_method(D_METHOD("start_hot_reload", "interval"), &VMLModLoader::start_hot_reload,
			DEFVAL(0.5));
	ClassDB::bind_method(D_METHOD("rescan"), &VMLModLoader::rescan);

	ADD_SIGNAL(MethodInfo("database_loaded"));
	ADD_SIGNAL(MethodInfo("preload_progress", PropertyInfo(Variant::INT, "current"),
			PropertyInfo(Variant::INT, "total")));
	ADD_SIGNAL(MethodInfo("database_entry_changed", PropertyInfo(Variant::STRING, "id")));
	ADD_SIGNAL(MethodInfo("mod_loaded", PropertyInfo(Variant::STRING, "mod_id")));
	ADD_SIGNAL(MethodInfo("mod_unloaded", PropertyInfo(Variant::STRING, "mod_id")));
	ADD_SIGNAL(MethodInfo("mod_enabled", PropertyInfo(Variant::STRING, "mod_id")));
	ADD_SIGNAL(MethodInfo("mod_disabled", PropertyInfo(Variant::STRING, "mod_id")));
	ADD_SIGNAL(MethodInfo("mod_reloaded", PropertyInfo(Variant::STRING, "mod_id")));
	ADD_SIGNAL(MethodInfo("registry_rebuilt"));
}

} // namespace godot
