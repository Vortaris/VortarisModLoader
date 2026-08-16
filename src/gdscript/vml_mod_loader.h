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
	/// True when the id has a live entry in the in-memory content database.
	bool has_data(const String &p_id) const;
	String resolve(const String &p_id) const;
	Variant get_data(const String &p_id) const;
	Ref<Resource> get_resource(const String &p_id) const;
	/// Explicit path registration (GDScript name: `register`).
	bool register_id(const String &p_id, const String &p_path);
	/// Value registration (GDScript name: `register_id`): stores a Variant
	/// provider at priority `p_priority` (mod providers > 0 override it).
	bool register_id(const String &p_id, const Variant &p_value, int p_priority = 0);
	bool unregister_id(const String &p_id);
	Dictionary list_ids(const String &p_prefix = "") const;
	PackedStringArray list_namespaces() const;
	/// Every full id in a namespace, sorted ("ns:path").
	PackedStringArray list_ids_in_namespace(const String &p_ns) const;
	/// Number of ids whose dotted canonical form begins with `prefix`.
	int count_ids(const String &p_prefix = "") const;

	// --- content database (unified load) -------------------------------
	/// "data" (default) preloads data files; "all" preloads every id; "off" = lazy.
	void preload_database();
	/// Preload in bounded batches across frames (never blocks); emits
	/// preload_progress(current, total) and database_loaded when done.
	bool preload_database_async();
	/// Clear the repository and re-preload per the current mode.
	void reload_database();
	/// { canonical_id: value } for every loaded entry (optionally prefixed).
	/// Union of registry ids and database ids; values resolved lazily via get_data.
	Dictionary get_all(const String &p_prefix = "") const;
	/// Overwrite a database entry in place. `persist=true` additionally stores the
	/// value as a `__registry__` value provider (priority 0) and saves the registry
	/// so it survives restarts (see vortarismodloader/paths/registry_path).
	bool set_data(const String &p_id, const Variant &p_value, bool p_persist = false);
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
	/// mods override it). Saved with [method save_registry]. `p_placeholder` marks
	/// the entry as a developer placeholder (see [method get_placeholder_ids]).
	bool set_registry_entry(const String &p_id, const String &p_path, const String &p_type = "",
			const String &p_description = "", bool p_placeholder = false);
	Dictionary get_registry_entry(const String &p_id) const;
	Dictionary get_registry() const;
	bool remove_registry_entry(const String &p_id);

	// --- 0.3.0 B6: ID placeholders -------------------------------------
	/// Declare a placeholder id with a default value. `p_type` selects the storage:
	/// "data" (or any non-resource type) stores `p_default` as a constant value
	/// provider; otherwise `p_default` is treated as a resource path and the id
	/// resolves through [method get_resource]/`load("vml://id")`. Placeholders are
	/// base-layer (priority 0) — mods override them like any registry entry.
	bool set_placeholder(const String &p_id, const String &p_type, const Variant &p_default,
			const String &p_description = "");
	/// Every placeholder id, optionally filtered by type ("" = all).
	PackedStringArray get_placeholder_ids(const String &p_type = "") const;
	/// Persist the registry. Empty path (default) uses the configured project-level
	/// path (vortarismodloader/paths/registry_path, default res://vml/registry.json)
	/// and falls back to user://vml/registry.json when res:// is read-only (exports).
	Error save_registry(const String &p_path = "");
	Error load_registry(const String &p_path = "");

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
	/// Auto version of [method finish_startup]: call_deferred + retry until the
	/// scene tree is ready, so autoload _ready has run before mod_mains instantiate.
	void finish_startup_auto();
	/// True once [method finish_startup]/[method finish_startup_auto] ran.
	bool is_startup_done() const;

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
	/// Context hook: handlers receive (ctx, ...args) and may mutate/return the
	/// context Dictionary; the final ctx is returned.
	Dictionary invoke_hook_ctx(const String &p_hook_id, const Dictionary &p_ctx,
			const Array &p_args = Array());
	bool check_hook(const String &p_hook_id, const Array &p_args = Array());
	/// Declare an available hook point (for docs/editor discovery).
	bool register_hook_point(const String &p_hook_id, const String &p_description,
			const PackedStringArray &p_arg_types = PackedStringArray());
	/// { hook_id: { "count": int, "mods": PackedStringArray } }
	Dictionary list_hooks(const String &p_prefix = "") const;
	Dictionary list_hook_points(const String &p_prefix = "") const;
	/// Handlers of a hook in call order as Array of { mod_id, priority }.
	Array list_hook_handlers(const String &p_hook_id) const;
	/// Every provider of an id: { providers: [{ mod_id, path, priority, explicit }], best: int }.
	Dictionary list_providers(const String &p_id) const;

	// --- mod management (M3: discovery + ordering) ---------------------
	PackedStringArray get_mod_ids() const;
	PackedStringArray get_load_order() const;
	PackedStringArray get_mod_errors(const String &p_mod_id) const;
	bool is_mod_enabled(const String &p_mod_id) const;
	bool is_mod_loaded(const String &p_mod_id) const;
	/// Display name from the manifest ("" falls back to the mod id).
	String get_mod_display_name(const String &p_mod_id) const;
	/// Manifest description of a mod ("" if unknown).
	String get_mod_description(const String &p_mod_id) const;
	/// Overlay priority from the load order (base=0, first mod=1, ...). -1 if absent.
	int get_mod_priority(const String &p_mod_id) const;
	/// Enabled mods that depend on `p_mod_id` (for disable-confirmation UI).
	PackedStringArray get_mod_dependents(const String &p_mod_id) const;
	/// Deps of `p_mod_id` as { dep_id: { exists, enabled } } (for enable-confirmation UI).
	Dictionary get_mod_dependencies(const String &p_mod_id) const;

	// --- mod health (0.2.2) --------------------------------------------
	/// Validate a mod: manifest completeness, loadable main_script, parseable data
	/// JSON, and id cross-check. Returns { valid, errors, warnings, checked }.
	Dictionary validate_mod(const String &p_mod_id) const;
	/// { errors: PackedStringArray, warnings: PackedStringArray } for a mod.
	Dictionary get_mod_report(const String &p_mod_id) const;
	/// { mod_id: { errors, warnings } } for mods that have any.
	Dictionary get_errors_summary() const;
	/// Startup aggregate: { broken_mods: PackedStringArray, errors, warnings }.
	Dictionary get_startup_report() const;

	// --- mod lifecycle (M6: runtime load/unload + zip install) ---------
	/// Activate: stack content, instantiate mod_main, register hooks.
	bool enable_mod(const String &p_mod_id);
	/// Deactivate: remove hooks/content/database, destroy mod_main.
	bool disable_mod(const String &p_mod_id);
	bool load_mod(const String &p_mod_id);
	bool unload_mod(const String &p_mod_id);
	/// Hot reload one mod in place: drop hooks/content/database, re-scan, and
	/// re-instantiate its mod_main (no duplicate hooks). Emits mod_reloaded.
	bool reload_mod(const String &p_mod_id);
	/// Transactionally extract a zip mod into user://vml/mods and activate it.
	Error install_mod_from_zip(const String &p_zip_path);
	/// Remove an installed (user://) mod entirely.
	Error uninstall_mod(const String &p_mod_id);

	// --- packaging / roots (0.2.2) -------------------------------------
	/// { embedded, external, scan_user_mods } from project settings.
	Dictionary get_mod_package_plan() const;
	/// Set vortarismodloader/export/export_mods ("embedded"/"external"/"none") and
	/// vortarismodloader/paths/scan_user_mods, persisted to ProjectSettings.
	bool set_export_policy(const String &p_mode, bool p_scan_user);
	/// Configured mod root directories — the two per-dir settings
	/// (vortarismodloader/paths/mod_dir + unpacked_dir) composed with legacy
	/// mod_paths array entries and runtime extra roots.
	PackedStringArray get_mod_roots() const;
	bool add_mod_root(const String &p_path);
	bool remove_mod_root(const String &p_path);

	// --- 0.3.0 B5: user-defined mod order (priority) --------------------
	/// The persisted user-defined load order (empty when unset). The UI drives
	/// drag-to-reorder with this.
	PackedStringArray get_mod_order() const;
	/// Reorder the mod load order. `order` must contain known mod ids only and
	/// respect dependency edges (a dependency before its dependents). Persisted to
	/// user://vml/load_order.json and re-applied on every rescan/startup.
	bool set_mod_order(const PackedStringArray &p_order);

	// --- 0.3.0: error summary + debug log introspection -----------------
	/// Human-readable startup error summary ("<mod>: <err>" lines, one per error).
	/// This is what is printed to the console (and shown in the error dialog when
	/// vortarismodloader/general/show_error_dialogs is on and not headless).
	String get_error_summary() const;
	/// Recent advanced-debug lines (vortarismodloader/general/debug_output gate),
	/// each prefixed "[vortarismodloader][dbg] ".
	PackedStringArray get_debug_log() const;
	void clear_debug_log();
	/// One-time migration hint for 0.2.x zip mods left in user://vml/mods after the
	/// 0.3.0 default mod_paths change ("" when nothing was found / already shown).
	String get_legacy_mod_migration_notice() const;

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
		bool activating = false; // recursion guard for cascade-enable
		bool disabling = false; // recursion guard for cascade-disable
		std::vector<String> errors;
		std::vector<String> warnings;
	};

	struct HookPoint {
		vortarismodloader::ResourceId id;
		String description;
		PackedStringArray arg_types;
	};

	void scan_base_layer();
	void scan_mods();
	/// Mount every *.pck found under the configured mod roots (read-only packs,
	/// content must live under mods/<mod_id>/). Called at the top of scan_mods.
	void mount_packs();
	/// After a scan/finish_startup: always print the error summary to the console;
	/// when vortarismodloader/general/show_error_dialogs is on and the display is
	/// not headless, pop an AcceptDialog with the same summary.
	void maybe_show_error_dialogs();
	/// "<mod>: <err>" lines, one per startup error ("" when clean).
	String error_summary_text() const;
	/// Overlay priority from the load order (base=0, first mod=1, ...). -1 if absent.
	int mod_priority(const String &p_mod_id) const;
	/// Read user://vml/load_order.json into custom_load_order_ (empty when absent).
	void load_custom_order();
	/// Write custom_load_order_ to user://vml/load_order.json.
	void save_custom_order();
	/// Reorder load_order_ to follow custom_load_order_ where present (missing
	/// entries stay in topological order). No-op when the custom order is empty.
	void reorder_load_order();
	/// Re-scan enabled mods so their overlay priorities follow the new load order.
	void reapply_overlay_priorities();
	void _process_preload_batch();
	void _auto_finish_startup();
	void log_verbose(const String &p_msg) const;
	void log_debug(const String &p_msg) const;
	/// First configured mod root that passes a writable probe (skips read-only
	/// res:// in exports / under a mounted pack; custom absolute-path roots win too).
	/// Never returns a directory that scanning ignores: the fallback registers
	/// user://vml/mods as a scanned root before returning it (M3).
	String install_root();
	/// True when `p_root` accepts a write (create + remove a temp probe directory).
	bool root_writable(const String &p_root) const;
	/// True when applying `p_custom` to `p_topological` keeps every dependency
	/// strictly before its dependent in the resulting full load order.
	bool custom_order_valid(const std::vector<String> &p_custom,
			const std::vector<String> &p_topological) const;
	/// Detect legacy 0.2.x zip mods in user://vml/mods (not a configured root) and
	/// print a one-time migration notice (see get_legacy_mod_migration_notice).
	void check_legacy_mods_migration();
	bool is_reserved(const vortarismodloader::ResourceId &p_id) const;
	String data_type_for(const String &p_path) const;
	/// Load the value for a provider: json/csv parse to data, everything else is a Resource.
	Variant load_entry_value(const vortarismodloader::ProviderEntry &p_e) const;
	/// Value of a provider: the stored Variant for a value provider (empty path),
	/// otherwise load_entry_value from the physical file.
	Variant provider_value(const vortarismodloader::ProviderEntry &p_e) const;
	/// Configured (or default) project-level registry path.
	String registry_path() const;
	/// Configured mod root directories — mod_dir + unpacked_dir composed with the
	/// legacy mod_paths array (0.3.0/0.3.1) and runtime extra roots (0.3.2).
	PackedStringArray mod_roots() const;
	/// Runtime dependency/incompatibility/version re-check before an enable.
	bool runtime_deps_ok(ModRecord &p_rec, std::vector<String> &r_reason) const;
	/// Whether a disabled mod could be cascade-enabled (its own deps enable-able,
	/// no enabled incompatibility, cycle-guarded).
	bool can_cascade(const String &p_id, std::vector<String> &p_visited) const;
	/// Run validate_mod on every mod at boot when validate_on_startup is enabled.
	void run_startup_validation();
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
	std::vector<String> custom_load_order_; // persisted user priority (user://vml/load_order.json)
	bool startup_validation_done_ = false; // re-run only after a mod re-scan
	struct RegistryEntry {
		String path;
		String type;
		String description;
		bool has_value = false; // value provider persisted from set_data(..., true)
		Variant value;
		bool placeholder = false; // developer placeholder (VML.set_placeholder)
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
	std::vector<String> mounted_packs_; // pck paths already load_resource_pack'd
	String last_error_dialog_summary_; // last error summary that popped a dialog (F8 debounce)
	String legacy_migration_notice_; // one-time user://vml/mods migration hint (F5)
	bool legacy_migration_notified_ = false; // shown this session (or root re-added)

	static VMLModLoader *singleton;
};

} // namespace godot

#endif // VML_MOD_LOADER_H
