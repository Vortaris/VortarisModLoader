#ifndef VML_MOD_LOADER_H
#define VML_MOD_LOADER_H

#include <unordered_map>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "../core/overlay_stack.h"
#include "../core/registry_index.h"
#include "../core/resource_id.h"

namespace godot {

// The VML engine singleton. Registered at MODULE_INITIALIZATION_LEVEL_SCENE so it
// exists before any autoload and before the main scene. This is the only global
// entry point mods and games talk to.
//
// M2 state: builds the routing layer (base layer scan of res://assets + res://data)
// and exposes id -> resource/data lookups. Later milestones layer on the content
// database, hooks, mod lifecycle, hot reload and the editor plugin.
class VMLModLoader : public Node {
	GDCLASS(VMLModLoader, Node)

public:
	VMLModLoader();
	~VMLModLoader();

	static void create_singleton();
	static void free_singleton();
	static VMLModLoader *get_singleton();

	/// True once the routing layer is built (constructor).
	bool is_initialized() const;

	// --- registry / resource routing -----------------------------------
	bool has(const String &p_id) const;
	/// Resolve to the winning physical path (debug/tooling). "" when unknown.
	String resolve(const String &p_id) const;
	/// Load by id: data files parse to Dictionary/Array, everything else returns
	/// the Resource instance.
	Variant get_data(const String &p_id) const;
	Ref<Resource> get_resource(const String &p_id) const;
	/// Explicitly map an id to a path (beats implicit providers at equal priority).
	bool register_id(const String &p_id, const String &p_path);
	bool unregister_id(const String &p_id);
	/// { namespace: PackedStringArray } optionally filtered by an id prefix.
	Dictionary list_ids(const String &p_prefix = "") const;
	PackedStringArray list_namespaces() const;

protected:
	static void _bind_methods();

private:
	void scan_base_layer();

	vortarismodloader::RegistryIndex registry_;
	vortarismodloader::OverlayStack overlays_;
	std::unordered_map<vortarismodloader::ResourceIdKey, String, vortarismodloader::ResourceIdKeyHash>
			explicit_paths_;
	bool initialized_ = false;

	static VMLModLoader *singleton;
};

} // namespace godot

#endif // VML_MOD_LOADER_H
