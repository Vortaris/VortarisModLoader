#include "vml_mod_loader.h"

#include <algorithm>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
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
	// 1) Discover from the unpacked dev folder + runtime-installed user://vml/mods.
	std::vector<vortarismodloader::DiscoveredMod> discovered;
	vortarismodloader::DiscoveryScanner::scan_mod_dirs("res://mods-unpacked", discovered);
	vortarismodloader::DiscoveryScanner::scan_mod_dirs("user://vml/mods", discovered);

	// 2) Parse + validate manifests; a zip with a duplicate id is skipped (unpacked wins).
	std::vector<vortarismodloader::ModManifest> valid_manifests;
	for (const vortarismodloader::DiscoveredMod &d : discovered) {
		ModRecord rec;
		rec.root = d.root;
		rec.from_zip = d.root.begins_with("user://vml/mods");
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

	// 4) Dependency-sorted load order (only valid mods participate).
	std::vector<String> order;
	std::vector<String> order_errors;
	vortarismodloader::DependencyGraph::compute_order(valid_manifests, order, order_errors);

	// 5) Attach order + errors back to records.
	for (ModRecord &rec : mods_) {
		if (std::find(order.begin(), order.end(), rec.manifest.id) != order.end()) {
			load_order_.push_back(rec.manifest.id);
		}
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
	const String ext = e->physical_path.get_extension().to_lower();
	Variant val;
	if (ext == "json" || ext == "csv") {
		val = vortarismodloader::LoaderBackend::load_data(e->physical_path);
	} else {
		val = vortarismodloader::LoaderBackend::load_resource(e->physical_path);
	}
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
		const bool is_data = is_data_extension(ext);
		if (database_mode_ == DatabaseMode::DATA && !is_data) {
			continue;
		}
		Variant val;
		if (is_data) {
			val = vortarismodloader::LoaderBackend::load_data(e->physical_path);
		} else {
			val = vortarismodloader::LoaderBackend::load_resource(e->physical_path);
		}
		if (val.get_type() != Variant::NIL) {
			database_.set(id, val, e->physical_path, e->mod_id);
		}
	}
	emit_signal("database_loaded");
}

void VMLModLoader::reload_database() {
	preload_database();
}

Dictionary VMLModLoader::get_all(const String &p_prefix) const {
	Dictionary out;
	for (const vortarismodloader::ResourceId &id : database_.loaded_ids()) {
		const String canonical = id.canonical();
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

bool VMLModLoader::set_data(const String &p_id, const Variant &p_value) {
	vortarismodloader::ResourceId rid;
	if (!vortarismodloader::ResourceId::parse(p_id, rid)) {
		ERR_PRINT(String("VML: invalid id '") + p_id + String("'"));
		return false;
	}
	database_.set(rid, p_value, String(), "__runtime__");
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

void VMLModLoader::scan_mod_content(ModRecord &p_rec) {
	int pri = overlays_.priority_of(p_rec.manifest.id);
	if (pri < 0) {
		pri = overlays_.add_source(p_rec.manifest.id, false);
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
			const bool is_data = is_data_extension(ext);
			if (database_mode_ == DatabaseMode::DATA && !is_data) {
				continue;
			}
			Variant val;
			if (is_data) {
				val = vortarismodloader::LoaderBackend::load_data(e->physical_path);
			} else {
				val = vortarismodloader::LoaderBackend::load_resource(e->physical_path);
			}
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

bool VMLModLoader::has_active_dependents(const String &p_mod_id) const {
	for (const ModRecord &rec : mods_) {
		if (!rec.enabled) {
			continue;
		}
		for (const String &dep : rec.manifest.deps) {
			String dep_id, op, want;
			vortarismodloader::DependencyGraph::parse_dependency(dep, dep_id, op, want);
			if (dep_id == p_mod_id) {
				return true;
			}
		}
	}
	return false;
}

bool VMLModLoader::activate_mod(const String &p_mod_id) {
	ModRecord *rec = find_mod(p_mod_id);
	if (rec == nullptr) {
		return false;
	}
	if (rec->enabled && rec->mod_main_instantiated) {
		return true;
	}
	for (const String &dep : rec->manifest.deps) {
		String dep_id, op, want;
		vortarismodloader::DependencyGraph::parse_dependency(dep, dep_id, op, want);
		if (!is_mod_enabled(dep_id)) {
			rec->errors.push_back(String("dependency not enabled: ") + dep_id);
			return false;
		}
	}
	if (!rec->content_scanned) {
		scan_mod_content(*rec);
	}
	if (!rec->manifest.main_script.is_empty() && !rec->mod_main_instantiated) {
		instantiate_mod_main(*rec);
	}
	rec->enabled = true;
	save_profile();
	emit_signal("mod_enabled", p_mod_id);
	return true;
}

bool VMLModLoader::deactivate_mod(const String &p_mod_id) {
	ModRecord *rec = find_mod(p_mod_id);
	if (rec == nullptr) {
		return false;
	}
	if (!rec->enabled) {
		return true;
	}
	if (has_active_dependents(p_mod_id)) {
		rec->errors.push_back(String("cannot disable '") + p_mod_id + String("': active mods depend on it"));
		return false;
	}
	hooks_.remove_mod(p_mod_id);
	destroy_mod_main(*rec);
	registry_.remove_mod(p_mod_id);
	database_.erase_mod(p_mod_id);
	rec->enabled = false;
	rec->content_scanned = false; // re-scan on the next activate
	save_profile();
	emit_signal("mod_unloaded", p_mod_id);
	emit_signal("mod_disabled", p_mod_id);
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
	if (find_mod(m.id) != nullptr) {
		return ERR_ALREADY_EXISTS;
	}
	ModRecord rec;
	rec.root = String("user://vml/mods/") + m.id;
	rec.from_zip = true;
	rec.manifest = m;
	mods_.push_back(std::move(rec));
	load_order_.push_back(m.id);
	return activate_mod(m.id) ? OK : ERR_UNAVAILABLE;
}

Error VMLModLoader::uninstall_mod(const String &p_mod_id) {
	ModRecord *rec = find_mod(p_mod_id);
	if (rec == nullptr) {
		return ERR_DOES_NOT_EXIST;
	}
	if (rec->enabled) {
		deactivate_mod(p_mod_id);
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
	return err;
}

String VMLModLoader::owning_mod(const String &p_path) const {
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
		ModRecord *rec = find_mod(mid);
		if (rec == nullptr || !rec->enabled) {
			continue;
		}
		// Re-scan content (handles added/removed/renamed files), refresh data.
		registry_.remove_mod(mid);
		database_.erase_mod(mid);
		rec->content_scanned = false;
		scan_mod_content(*rec);
		if (database_mode_ == DatabaseMode::OFF) {
			continue;
		}
		for (const vortarismodloader::ResourceId &id : registry_.all_ids()) {
			const vortarismodloader::ProviderEntry *e = registry_.lookup(id);
			if (e == nullptr || e->mod_id != mid) {
				continue;
			}
			const String ext = e->physical_path.get_extension().to_lower();
			const bool is_data = is_data_extension(ext);
			if (database_mode_ == DatabaseMode::DATA && !is_data) {
				continue;
			}
			Variant val;
			if (is_data) {
				val = vortarismodloader::LoaderBackend::load_data(e->physical_path);
			} else {
				val = vortarismodloader::LoaderBackend::load_resource(e->physical_path);
			}
			if (val.get_type() != Variant::NIL) {
				database_.set(id, val, e->physical_path, e->mod_id);
			}
			emit_signal("database_entry_changed", id.canonical());
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
	add_child(hot_reloader_);
	hot_reloader_->rescan();
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
	if (p_rec.manifest.main_script.is_empty()) {
		return;
	}
	const String script_path = p_rec.root + String("/") + p_rec.manifest.main_script;
	if (!FileAccess::file_exists(script_path)) {
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
	for (ModRecord &rec : mods_) {
		if (rec.enabled) {
			instantiate_mod_main(rec);
		}
	}
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
	ClassDB::bind_method(D_METHOD("get_mod_ids"), &VMLModLoader::get_mod_ids);
	ClassDB::bind_method(D_METHOD("get_load_order"), &VMLModLoader::get_load_order);
	ClassDB::bind_method(D_METHOD("get_mod_errors", "mod_id"), &VMLModLoader::get_mod_errors);

	ClassDB::bind_method(D_METHOD("preload_database"), &VMLModLoader::preload_database);
	ClassDB::bind_method(D_METHOD("reload_database"), &VMLModLoader::reload_database);
	ClassDB::bind_method(D_METHOD("get_all", "prefix"), &VMLModLoader::get_all, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("set_data", "id", "value"), &VMLModLoader::set_data);
	ClassDB::bind_method(D_METHOD("delete_data", "id"), &VMLModLoader::delete_data);
	ClassDB::bind_method(D_METHOD("get_database_mode"), &VMLModLoader::get_database_mode);
	ClassDB::bind_method(D_METHOD("set_database_mode", "mode"), &VMLModLoader::set_database_mode);

	ClassDB::bind_method(D_METHOD("finish_startup"), &VMLModLoader::finish_startup);
	ClassDB::bind_method(D_METHOD("add_hook", "hook_id", "callable", "priority"), &VMLModLoader::add_hook,
			DEFVAL(0));
	ClassDB::bind_method(D_METHOD("remove_hook", "hook_id", "callable"), &VMLModLoader::remove_hook);
	ClassDB::bind_method(D_METHOD("emit_hook", "hook_id", "args"), &VMLModLoader::emit_hook, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("invoke_hook", "hook_id", "args", "default"), &VMLModLoader::invoke_hook,
			DEFVAL(Array()), DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("check_hook", "hook_id", "args"), &VMLModLoader::check_hook, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("register_hook_point", "hook_id", "description", "arg_types"),
			&VMLModLoader::register_hook_point, DEFVAL(PackedStringArray()));
	ClassDB::bind_method(D_METHOD("list_hooks", "prefix"), &VMLModLoader::list_hooks, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("list_hook_points", "prefix"), &VMLModLoader::list_hook_points, DEFVAL(""));

	ClassDB::bind_method(D_METHOD("is_mod_enabled", "mod_id"), &VMLModLoader::is_mod_enabled);
	ClassDB::bind_method(D_METHOD("is_mod_loaded", "mod_id"), &VMLModLoader::is_mod_loaded);
	ClassDB::bind_method(D_METHOD("enable_mod", "mod_id"), &VMLModLoader::enable_mod);
	ClassDB::bind_method(D_METHOD("disable_mod", "mod_id"), &VMLModLoader::disable_mod);
	ClassDB::bind_method(D_METHOD("load_mod", "mod_id"), &VMLModLoader::load_mod);
	ClassDB::bind_method(D_METHOD("unload_mod", "mod_id"), &VMLModLoader::unload_mod);
	ClassDB::bind_method(D_METHOD("install_mod_from_zip", "zip_path"), &VMLModLoader::install_mod_from_zip);
	ClassDB::bind_method(D_METHOD("uninstall_mod", "mod_id"), &VMLModLoader::uninstall_mod);

	ClassDB::bind_method(D_METHOD("instantiate", "id"), &VMLModLoader::instantiate);
	ClassDB::bind_method(D_METHOD("reload_resources", "paths"), &VMLModLoader::reload_resources);
	ClassDB::bind_method(D_METHOD("get_content_roots"), &VMLModLoader::get_content_roots);
	ClassDB::bind_method(D_METHOD("start_hot_reload", "interval"), &VMLModLoader::start_hot_reload,
			DEFVAL(0.5));

	ADD_SIGNAL(MethodInfo("database_loaded"));
	ADD_SIGNAL(MethodInfo("database_entry_changed", PropertyInfo(Variant::STRING, "id")));
	ADD_SIGNAL(MethodInfo("mod_loaded", PropertyInfo(Variant::STRING, "mod_id")));
	ADD_SIGNAL(MethodInfo("mod_unloaded", PropertyInfo(Variant::STRING, "mod_id")));
	ADD_SIGNAL(MethodInfo("mod_enabled", PropertyInfo(Variant::STRING, "mod_id")));
	ADD_SIGNAL(MethodInfo("mod_disabled", PropertyInfo(Variant::STRING, "mod_id")));
	ADD_SIGNAL(MethodInfo("registry_rebuilt"));
}

} // namespace godot
