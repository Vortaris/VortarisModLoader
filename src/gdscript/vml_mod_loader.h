#ifndef VML_MOD_LOADER_H
#define VML_MOD_LOADER_H

#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "../core/content_database.h"
#include "../core/manifest.h"
#include "../core/overlay_stack.h"
#include "../core/registry_index.h"
#include "../core/resource_id.h"

namespace godot {

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
	/// Clear the repository and re-preload per the current mode.
	void reload_database();
	/// { canonical_id: value } for every loaded entry (optionally prefixed).
	Dictionary get_all(const String &p_prefix = "") const;
	bool set_data(const String &p_id, const Variant &p_value);
	bool delete_data(const String &p_id);
	String get_database_mode() const;
	bool set_database_mode(const String &p_mode);

	// --- mod management (M3: discovery + ordering) ---------------------
	PackedStringArray get_mod_ids() const;
	PackedStringArray get_load_order() const;
	PackedStringArray get_mod_errors(const String &p_mod_id) const;

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
		bool enabled = true;
		std::vector<String> errors;
	};

	void scan_base_layer();
	void scan_mods();
	DatabaseMode mode_from_string(const String &p_mode) const;
	String mode_to_string(DatabaseMode p_mode) const;
	bool is_data_extension(const String &p_ext) const;
	void emit_entry_changed(const vortarismodloader::ResourceId &p_id);

	vortarismodloader::RegistryIndex registry_;
	vortarismodloader::OverlayStack overlays_;
	mutable vortarismodloader::ContentDatabase database_;
	mutable DatabaseMode database_mode_ = DatabaseMode::DATA;
	std::unordered_map<vortarismodloader::ResourceIdKey, String, vortarismodloader::ResourceIdKeyHash>
			explicit_paths_;
	std::vector<ModRecord> mods_;
	std::vector<String> load_order_;
	bool initialized_ = false;

	static VMLModLoader *singleton;
};

} // namespace godot

#endif // VML_MOD_LOADER_H
