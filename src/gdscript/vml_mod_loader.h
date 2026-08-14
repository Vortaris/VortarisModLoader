#ifndef VML_MOD_LOADER_H
#define VML_MOD_LOADER_H

#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "../core/content_database.h"
#include "../core/hook_registry.h"
#include "../core/manifest.h"
#include "../core/overlay_stack.h"
#include "../core/registry_index.h"
#include "../core/resource_id.h"

namespace godot {

class VMLHotReloader;

// The VML engine singleton. Registered at MODULE_INITIALIZATION_LEVEL_SCENE so it
// exists before any autoload and before the main scene. This is the only global
// entry point mods and games talk to.
//
// M4 state: adds the unified ContentDatabase — data ids can be preloaded into an
// in-memory repository (get_data becomes an O(1) hash hit) and the repository is
// mutable via set_data/delete_data so live content can be rewritten in place.
class VMLModLoader : public Node {
	GDCLASS(VMLModLoader, Node)

public:
	VMLModLoader();
	~VMLModLoader();

	static void create_singleton();
	static void free_singleton();
	static VMLModLoader *get_singleton();

	/// True once the routing layer + initial mod scan are built (constructor).
	bool is_initialized() const;

	// --- registry / resource routing -----------------------------------
	bool has(const String &p_id) const;
	String resolve(const String &p_id) const;
	Variant get_data(const String &p_id) const;
	Ref<Resource> get_resource(const String &p_id) const;
	bool register_id(const String &p_id, const String &p_path);
	bool unregister_id(const String &p_id);
	Dictionary list_ids(const String &p_prefix = "") const;
	PackedStringArray list_namespaces() const;

	// --- content database (unified load) -------------------------------
	/// "data" (default) preloads data files; "all" preloads every id; "off" = lazy.
	void preload_database();
	/// Preload in bounded batches across frames (never blocks); emits
	/// preload_progress(current, total) and database_loaded when done.
	bool preload_database_async();
	/// Clear the repository and re-preload per the current mode.
	void reload_database();
	/// { canonical_id: value } for every loaded entry (optionally prefixed).
	Dictionary get_all(const String &p_prefix = "") const;
	bool set_data(const String &p_id, const Variant &p_value);
	bool delete_data(const String &p_id);
	String get_database_mode() const;
	bool set_database_mode(const String &p_mode);

	// --- convenience sugar (newcomer friendly) -------------------------
	/// Alias of get_data.
	Variant get(const String &p_id) const;
	/// Alias of get_resource.
	Ref<Resource> load(const String &p_id) const;
	/// Alias of has.
	bool exists(const String &p_id) const;
	/// Mod root directory ("" if unknown).
	String get_mod_path(const String &p_mod_id) const;
	/// Manifest version of a mod ("" if unknown).
	String get_mod_version(const String &p_mod_id) const;

	// --- id metadata & operations --------------------------------------
	/// Full status of an id: { valid, resolved, path, provider_mod, priority,
	/// explicit, preloaded, reserved, type, data_type }.
	Dictionary get_id_info(const String &p_id) const;
	/// Tag an id with a logical type (e.g. "unit", "item") for filtering.
	bool set_id_type(const String &p_id, const String &p_type);
	String get_id_type(const String &p_id) const;
	/// Every id tagged with `type` (dotted ids, sorted).
	PackedStringArray list_ids_by_type(const String &p_type) const;
	/// Reserve an id (declares it without a provider; has() reports true).
	bool reserve(const String &p_id);
	bool unreserve(const String &p_id);
	/// Coarse data category of the resolved file: data/scene/script/image/audio/font/resource.
	String get_id_data_type(const String &p_id) const;

	// --- persisted content registry (0.2.0) ----------------------------
	/// Declare an id in the persisted content registry (base-layer explicit route;
	/// mods override it). Saved with [method save_registry].
	bool set_registry_entry(const String &p_id, const String &p_path, const String &p_type = "",
			const String &p_description = "");
	Dictionary get_registry_entry(const String &p_id) const;
	Dictionary get_registry() const;
	bool remove_registry_entry(const String &p_id);
	Error save_registry(const String &p_path = "user://vml/registry.json");
	Error load_registry(const String &p_path = "user://vml/registry.json");

	// --- runtime reroute (0.2.0) ---------------------------------------
	/// Temporarily force an id to a path at runtime (highest priority, not persisted).
	bool reroute(const String &p_id, const String &p_path);
	bool clear_reroute(const String &p_id);

	// --- per-mod config (0.2.0) ----------------------------------------
	/// Read a mod's config from user://vml/configs/<mod_id>.json ({} if unset).
	Dictionary get_config(const String &p_mod_id) const;
	bool set_config(const String &p_mod_id, const Dictionary &p_values);
	/// The mod's declared config_schema (from manifest extra.godot.config_schema).
	Dictionary get_config_schema(const String &p_mod_id) const;

	// --- data-driven scene building (0.2.0) ----------------------------
	/// Build a Node tree from a Dictionary at id: {type,name,properties,children}.
	Node *build_node(const String &p_id);

	// --- validation (0.2.0) --------------------------------------------
	/// Scan all loaded data for id references and report missing ones.
	Dictionary validate() const;

	// --- lifecycle / mod_main ------------------------------------------
	/// Instantiate every enabled mod's mod_main.gd (the game calls this from a
	/// bootstrap autoload's _ready). mod_main must register hooks/config in _init.
	void finish_startup();

	// --- M7: resources + dev hot reload --------------------------------
	/// Instantiate a PackedScene by id (game:scenes/camp -> Node).
	Variant instantiate(const String &p_id);
	/// Dev hot reload: re-scan the affected mod(s) and refresh their data.
	void reload_resources(const PackedStringArray &p_paths);
	/// Root directories to watch for changes (base layer + enabled mods).
	PackedStringArray get_content_roots() const;
	/// Start a polling hot-reloader node (dev builds / editor).
	void start_hot_reload(double p_interval = 0.5);
	/// Full re-discovery + rebuild of the routing layer and database.
	void rescan();

	// --- declarative hooks ---------------------------------------------
	bool add_hook(const String &p_hook_id, const Callable &p_callable, int p_priority = 0);
	bool remove_hook(const String &p_hook_id, const Callable &p_callable);
	void emit_hook(const String &p_hook_id, const Array &p_args = Array());
	Variant invoke_hook(const String &p_hook_id, const Array &p_args = Array(),
			const Variant &p_default = Variant());
	bool check_hook(const String &p_hook_id, const Array &p_args = Array());
	/// Declare an available hook point (for docs/editor discovery).
	bool register_hook_point(const String &p_hook_id, const String &p_description,
			const PackedStringArray &p_arg_types = PackedStringArray());
	/// { hook_id: { "count": int, "mods": PackedStringArray } }
	Dictionary list_hooks(const String &p_prefix = "") const;
	Dictionary list_hook_points(const String &p_prefix = "") const;

	// --- mod management (M3: discovery + ordering) ---------------------
	PackedStringArray get_mod_ids() const;
	PackedStringArray get_load_order() const;
	PackedStringArray get_mod_errors(const String &p_mod_id) const;
	bool is_mod_enabled(const String &p_mod_id) const;
	bool is_mod_loaded(const String &p_mod_id) const;

	// --- mod lifecycle (M6: runtime load/unload + zip install) ---------
	/// Activate: stack content, instantiate mod_main, register hooks.
	bool enable_mod(const String &p_mod_id);
	/// Deactivate: remove hooks/content/database, destroy mod_main.
	bool disable_mod(const String &p_mod_id);
	bool load_mod(const String &p_mod_id);
	bool unload_mod(const String &p_mod_id);
	/// Transactionally extract a zip mod into user://vml/mods and activate it.
	Error install_mod_from_zip(const String &p_zip_path);
	/// Remove an installed (user://) mod entirely.
	Error uninstall_mod(const String &p_mod_id);

	// signals
	static void _static_bind_signals();

protected:
	static void _bind_methods();

private:
	enum class DatabaseMode {
		OFF,
		DATA,
		ALL,
	};

	struct ModRecord {
		vortarismodloader::ModManifest manifest;
		String root;
		bool enabled = true; // content is stacked in the registry
		bool content_scanned = false;
		bool mod_main_instantiated = false;
		Node *mod_main_node = nullptr;
		bool from_zip = false; // installed into user://vml/mods
		std::vector<String> errors;
	};

	struct HookPoint {
		vortarismodloader::ResourceId id;
		String description;
		PackedStringArray arg_types;
	};

	void scan_base_layer();
	void scan_mods();
	void _process_preload_batch();
	void log_verbose(const String &p_msg) const;
	bool is_reserved(const vortarismodloader::ResourceId &p_id) const;
	String data_type_for(const String &p_path) const;
	/// Load the value for a provider: json/csv parse to data, everything else is a Resource.
	Variant load_entry_value(const vortarismodloader::ProviderEntry &p_e) const;
	/// Reload one id into the database cache (used after reroute/clear_reroute).
	void refresh_database_entry(const vortarismodloader::ResourceId &p_id);
	String owning_mod(const String &p_path) const;
	Node *build_node_from_dict(const Dictionary &p_spec, const String &p_source_id);
	ModRecord *find_mod(const String &p_mod_id);
	const ModRecord *find_mod(const String &p_mod_id) const;
	void scan_mod_content(ModRecord &p_rec);
	void instantiate_mod_main(ModRecord &p_rec);
	void destroy_mod_main(ModRecord &p_rec);
	bool activate_mod(const String &p_mod_id);
	bool deactivate_mod(const String &p_mod_id);
	bool has_active_dependents(const String &p_mod_id) const;
	void load_profile();
	void save_profile();
	DatabaseMode mode_from_string(const String &p_mode) const;
	String mode_to_string(DatabaseMode p_mode) const;
	bool is_data_extension(const String &p_ext) const;
	void emit_entry_changed(const vortarismodloader::ResourceId &p_id);

	vortarismodloader::RegistryIndex registry_;
	vortarismodloader::OverlayStack overlays_;
	mutable vortarismodloader::ContentDatabase database_;
	mutable DatabaseMode database_mode_ = DatabaseMode::DATA;
	vortarismodloader::HookRegistry hooks_;
	std::vector<HookPoint> hook_points_;
	String active_mod_; // set while a mod_main's _init runs (hook attribution)
	bool startup_done_ = false;
	std::unordered_map<vortarismodloader::ResourceIdKey, String, vortarismodloader::ResourceIdKeyHash>
			explicit_paths_;
	std::vector<ModRecord> mods_;
	std::vector<String> load_order_;
	struct RegistryEntry {
		String path;
		String type;
		String description;
	};
	std::vector<vortarismodloader::ResourceId> pending_ids_;
	std::vector<vortarismodloader::ResourceId> reserved_ids_;
	std::vector<vortarismodloader::ResourceId> rerouted_ids_;
	std::unordered_map<vortarismodloader::ResourceIdKey, String, vortarismodloader::ResourceIdKeyHash> id_types_;
	std::unordered_map<vortarismodloader::ResourceIdKey, RegistryEntry, vortarismodloader::ResourceIdKeyHash> registry_map_;
	size_t preload_index_ = 0;
	bool preload_in_flight_ = false;
	VMLHotReloader *hot_reloader_ = nullptr;
	bool profile_loaded_ = false;
	bool initialized_ = false;

	static VMLModLoader *singleton;
};

} // namespace godot

#endif // VML_MOD_LOADER_H
