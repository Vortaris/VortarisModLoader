#ifndef VML_MOD_LOADER_H
#define VML_MOD_LOADER_H

#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "../core/manifest.h"
#include "../core/overlay_stack.h"
#include "../core/registry_index.h"
#include "../core/resource_id.h"

namespace godot {

// The VML engine singleton. Registered at MODULE_INITIALIZATION_LEVEL_SCENE so it
// exists before any autoload and before the main scene. This is the only global
// entry point mods and games talk to.
//
// M3 state: discovers mods under res://mods-unpacked/, parses+validates their
// manifests, resolves a dependency-sorted load order and stacks every valid mod's
// assets/data above the base layer with override arbitration.
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

	// --- mod management (M3: discovery + ordering) ---------------------
	/// All discovered mod ids (valid or not), in deterministic order.
	PackedStringArray get_mod_ids() const;
	/// Dependency-sorted load order of valid mods (base excluded).
	PackedStringArray get_load_order() const;
	/// Human-readable errors for one mod (missing deps, cycle, bad manifest).
	PackedStringArray get_mod_errors(const String &p_mod_id) const;

protected:
	static void _bind_methods();

private:
	struct ModRecord {
		vortarismodloader::ModManifest manifest;
		String root;
		bool enabled = true;
		std::vector<String> errors;
	};

	void scan_base_layer();
	void scan_mods();

	vortarismodloader::RegistryIndex registry_;
	vortarismodloader::OverlayStack overlays_;
	std::unordered_map<vortarismodloader::ResourceIdKey, String, vortarismodloader::ResourceIdKeyHash>
			explicit_paths_;
	std::vector<ModRecord> mods_;
	std::vector<String> load_order_;
	bool initialized_ = false;

	static VMLModLoader *singleton;
};

} // namespace godot

#endif // VML_MOD_LOADER_H
