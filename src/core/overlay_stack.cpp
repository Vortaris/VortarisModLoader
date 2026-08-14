#include "overlay_stack.h"

#include <algorithm>

namespace vortarismodloader {

void OverlayStack::clear() {
	sources_.clear();
}

int OverlayStack::add_source(const godot::String &p_mod_id, bool p_is_base) {
	for (const Source &s : sources_) {
		if (s.mod_id == p_mod_id) {
			return priority_of(p_mod_id);
		}
	}
	sources_.push_back(Source{ p_mod_id, p_is_base });
	return (int)sources_.size() - 1;
}

bool OverlayStack::remove_source(const godot::String &p_mod_id) {
	const auto before = sources_.size();
	sources_.erase(std::remove_if(sources_.begin(), sources_.end(),
							[&](const Source &s) { return s.mod_id == p_mod_id; }),
			sources_.end());
	return sources_.size() != before;
}

void OverlayStack::reorder(const std::vector<godot::String> &p_ordered_mod_ids) {
	std::vector<Source> next;
	next.reserve(sources_.size() + p_ordered_mod_ids.size());
	// Base layer first, in original order.
	for (const Source &s : sources_) {
		if (s.is_base) {
			next.push_back(s);
		}
	}
	for (const godot::String &id : p_ordered_mod_ids) {
		next.push_back(Source{ id, false });
	}
	sources_ = std::move(next);
}

std::vector<Source> OverlayStack::sources() const {
	return sources_;
}

int OverlayStack::priority_of(const godot::String &p_mod_id) const {
	for (int i = 0; i < (int)sources_.size(); i++) {
		if (sources_[i].mod_id == p_mod_id) {
			return i;
		}
	}
	return -1;
}

} // namespace vortarismodloader
