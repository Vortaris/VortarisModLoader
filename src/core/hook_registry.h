#pragma once

#include <unordered_map>
#include <vector>

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "resource_id.h"

namespace vortarismodloader {

// One registered handler for a hook point.
struct HookHandler {
	godot::Callable callable;
	godot::String mod_id;
	int priority = 0; // higher runs first
};

// The declarative hook layer. Hook points are namespaced ids (e.g.
// `game:modify_damage`); mods register Callables with add(). Three dispatch
// semantics:
//   invoke(id, args, default)  -> pipeline: each handler rewrites the value,
//                                 last value returned to the caller.
//   emit(id, args)             -> fire-and-forget broadcast.
//   check(id, args) -> bool    -> predicate; any false short-circuits.
class HookRegistry {
public:
	void add(const ResourceId &p_hook_id, const godot::Callable &p_callable,
			const godot::String &p_mod_id, int p_priority);
	/// Remove one exact handler (compare callable identity).
	bool remove(const ResourceId &p_hook_id, const godot::Callable &p_callable);
	void remove_mod(const godot::String &p_mod_id);
	bool has(const ResourceId &p_hook_id) const;
	int handler_count(const ResourceId &p_hook_id) const;

	godot::Variant invoke(const ResourceId &p_hook_id, const godot::Array &p_args, const godot::Variant &p_default);
	void emit(const ResourceId &p_hook_id, const godot::Array &p_args);
	bool check(const ResourceId &p_hook_id, const godot::Array &p_args);

	/// Sorted hook ids (deterministic).
	std::vector<ResourceId> all_hooks() const;
	std::vector<HookHandler> handlers_for(const ResourceId &p_hook_id) const;
	void clear();

private:
	struct Entry {
		std::vector<HookHandler> handlers; // sorted by (priority desc, mod_id asc)
		void insert_sorted(const HookHandler &h);
	};
	std::unordered_map<ResourceIdKey, Entry, ResourceIdKeyHash> map_;
};

} // namespace vortarismodloader
