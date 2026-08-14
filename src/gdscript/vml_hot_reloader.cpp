#include "vml_hot_reloader.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "vml_mod_loader.h"

namespace godot {

VMLHotReloader::VMLHotReloader() {
	set_process(true);
}

void VMLHotReloader::set_poll_interval(double p_seconds) {
	poll_interval_ = p_seconds < 0.05 ? 0.05 : p_seconds;
}

void VMLHotReloader::rescan() {
	watcher_.clear();
	VMLModLoader *vml = VMLModLoader::get_singleton();
	if (vml == nullptr) {
		return;
	}
	const PackedStringArray roots = vml->get_content_roots();
	for (int i = 0; i < roots.size(); i++) {
		watcher_.seed_tree(roots[i]);
	}
}

void VMLHotReloader::_process(double p_delta) {
	elapsed_ += p_delta;
	if (elapsed_ < poll_interval_) {
		return;
	}
	elapsed_ = 0.0;
	const std::vector<godot::String> changed = watcher_.poll();
	if (changed.empty()) {
		return;
	}
	PackedStringArray arr;
	for (const godot::String &c : changed) {
		arr.push_back(c);
	}
	VMLModLoader *vml = VMLModLoader::get_singleton();
	if (vml != nullptr) {
		vml->reload_resources(arr);
	}
}

void VMLHotReloader::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_poll_interval", "seconds"), &VMLHotReloader::set_poll_interval);
	ClassDB::bind_method(D_METHOD("rescan"), &VMLHotReloader::rescan);
}

} // namespace godot
