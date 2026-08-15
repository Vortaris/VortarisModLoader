#include "hook_registry.h"

#include <algorithm>

#include "debug_log.h"

namespace vortarismodloader {

static bool handler_first(const HookHandler &a, const HookHandler &b) {
	if (a.priority != b.priority) {
		return a.priority > b.priority;
	}
	return a.mod_id < b.mod_id;
}

void HookRegistry::Entry::insert_sorted(const HookHandler &h) {
	handlers.push_back(h);
	std::sort(handlers.begin(), handlers.end(), handler_first);
}

void HookRegistry::add(const ResourceId &p_hook_id, const godot::Callable &p_callable,
		const godot::String &p_mod_id, int p_priority) {
	map_[ResourceIdKey{ p_hook_id.ns, p_hook_id.path }].insert_sorted(HookHandler{ p_callable, p_mod_id, p_priority });
	log_debug(godot::String("hook: register '") + p_hook_id.canonical() + godot::String("' (mod=") +
			p_mod_id + godot::String(", pri=") + godot::String::num_int64(p_priority) + godot::String(")"));
}

bool HookRegistry::remove(const ResourceId &p_hook_id, const godot::Callable &p_callable) {
	const auto it = map_.find(ResourceIdKey{ p_hook_id.ns, p_hook_id.path });
	if (it == map_.end()) {
		return false;
	}
	auto &handlers = it->second.handlers;
	const size_t before = handlers.size();
	handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
							[&](const HookHandler &h) { return h.callable == p_callable; }),
			handlers.end());
	// Capture the result BEFORE the entry may be destroyed by map_.erase().
	const bool removed = handlers.size() != before;
	if (handlers.empty()) {
		map_.erase(it);
	}
	if (removed) {
		log_debug(godot::String("hook: unregister '") + p_hook_id.canonical() + godot::String("'"));
	}
	return removed;
}

void HookRegistry::remove_mod(const godot::String &p_mod_id) {
	size_t removed_hooks = 0;
	for (auto it = map_.begin(); it != map_.end();) {
		auto &handlers = it->second.handlers;
		const size_t before = handlers.size();
		handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
								[&](const HookHandler &h) { return h.mod_id == p_mod_id; }),
				handlers.end());
		if (handlers.size() != before) {
			removed_hooks++;
		}
		if (handlers.empty()) {
			it = map_.erase(it);
		} else {
			++it;
		}
	}
	if (removed_hooks > 0) {
		log_debug(godot::String("hook: remove_mod '") + p_mod_id + godot::String("'"));
	}
}

bool HookRegistry::has(const ResourceId &p_hook_id) const {
	const auto it = map_.find(ResourceIdKey{ p_hook_id.ns, p_hook_id.path });
	return it != map_.end() && !it->second.handlers.empty();
}

int HookRegistry::handler_count(const ResourceId &p_hook_id) const {
	const auto it = map_.find(ResourceIdKey{ p_hook_id.ns, p_hook_id.path });
	return it == map_.end() ? 0 : (int)it->second.handlers.size();
}

godot::Variant HookRegistry::invoke(const ResourceId &p_hook_id, const godot::Array &p_args,
		const godot::Variant &p_default) {
	const auto it = map_.find(ResourceIdKey{ p_hook_id.ns, p_hook_id.path });
	if (it == map_.end()) {
		return p_default;
	}
	log_debug(godot::String("hook: invoke '") + p_hook_id.canonical() + godot::String("' handlers=") +
			godot::String::num_int64((int64_t)it->second.handlers.size()));
	godot::Variant current = p_default;
	godot::Array call_args;
	call_args.resize(p_args.size() + 1);
	call_args[0] = current;
	for (int i = 0; i < p_args.size(); i++) {
		call_args[i + 1] = p_args[i];
	}
	for (const HookHandler &h : it->second.handlers) {
		call_args[0] = current;
		current = h.callable.callv(call_args);
	}
	return current;
}

godot::Dictionary HookRegistry::invoke_ctx(const ResourceId &p_hook_id, const godot::Dictionary &p_ctx,
		const godot::Array &p_args) {
	const auto it = map_.find(ResourceIdKey{ p_hook_id.ns, p_hook_id.path });
	if (it == map_.end()) {
		return p_ctx;
	}
	log_debug(godot::String("hook: invoke_ctx '") + p_hook_id.canonical() + godot::String("' handlers=") +
			godot::String::num_int64((int64_t)it->second.handlers.size()));
	godot::Dictionary ctx = p_ctx;
	godot::Array call_args;
	call_args.resize(p_args.size() + 1);
	for (int i = 0; i < p_args.size(); i++) {
		call_args[i + 1] = p_args[i];
	}
	for (const HookHandler &h : it->second.handlers) {
		call_args[0] = ctx;
		const godot::Variant result = h.callable.callv(call_args);
		if (result.get_type() == godot::Variant::DICTIONARY) {
			ctx = godot::Dictionary(result);
		}
	}
	return ctx;
}

void HookRegistry::emit(const ResourceId &p_hook_id, const godot::Array &p_args) {
	const auto it = map_.find(ResourceIdKey{ p_hook_id.ns, p_hook_id.path });
	if (it == map_.end()) {
		return;
	}
	log_debug(godot::String("hook: emit '") + p_hook_id.canonical() + godot::String("' handlers=") +
			godot::String::num_int64((int64_t)it->second.handlers.size()));
	for (const HookHandler &h : it->second.handlers) {
		h.callable.callv(p_args);
	}
}

bool HookRegistry::check(const ResourceId &p_hook_id, const godot::Array &p_args) {
	const auto it = map_.find(ResourceIdKey{ p_hook_id.ns, p_hook_id.path });
	if (it == map_.end()) {
		return true; // no handlers -> allow
	}
	log_debug(godot::String("hook: check '") + p_hook_id.canonical() + godot::String("' handlers=") +
			godot::String::num_int64((int64_t)it->second.handlers.size()));
	for (const HookHandler &h : it->second.handlers) {
		const godot::Variant v = h.callable.callv(p_args);
		if (v.get_type() != godot::Variant::BOOL || !v.operator bool()) {
			return false;
		}
	}
	return true;
}

std::vector<ResourceId> HookRegistry::all_hooks() const {
	std::vector<ResourceId> out;
	out.reserve(map_.size());
	for (const auto &kv : map_) {
		out.push_back(ResourceId{ kv.first.ns, kv.first.path });
	}
	std::sort(out.begin(), out.end(), [](const ResourceId &a, const ResourceId &b) {
		if (a.ns != b.ns) {
			return a.ns < b.ns;
		}
		return a.path < b.path;
	});
	return out;
}

std::vector<HookHandler> HookRegistry::handlers_for(const ResourceId &p_hook_id) const {
	const auto it = map_.find(ResourceIdKey{ p_hook_id.ns, p_hook_id.path });
	return it == map_.end() ? std::vector<HookHandler>() : it->second.handlers;
}

void HookRegistry::clear() {
	map_.clear();
}

} // namespace vortarismodloader
