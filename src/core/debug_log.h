#pragma once

#include <deque>
#include <vector>

#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/variant/string.hpp>

#include "vml_settings.h"

namespace vortarismodloader {

// Advanced debug logging (vortarismodloader/general/debug_output, default false).
//
// Mirrors VMLModLoader::log_verbose but with its own gate: when the setting is
// on, every log_debug() line is printed to the console with the
// "[vortarismodloader][dbg]" prefix AND appended to a small in-memory ring
// buffer so scripts/tests can introspect recent activity without capturing
// stdout (VMLModLoader::get_debug_log / clear_debug_log).
//
// The ring buffer lives in one function-local static (inline accessor) so every
// translation unit that includes this header shares the same buffer.

inline std::deque<godot::String> &debug_log_buffer() {
	static std::deque<godot::String> buffer;
	return buffer;
}

inline bool debug_active() {
	return bool(get_ml_setting("general", "debug_output", false));
}

inline void log_debug(const godot::String &p_msg) {
	if (!debug_active()) {
		return;
	}
	const godot::String line = godot::String("[vortarismodloader][dbg] ") + p_msg;
	print_line(line);
	auto &buffer = debug_log_buffer();
	buffer.push_back(line);
	while (buffer.size() > 512) {
		buffer.pop_front();
	}
}

inline std::vector<godot::String> debug_log_snapshot() {
	const auto &buffer = debug_log_buffer();
	return std::vector<godot::String>(buffer.begin(), buffer.end());
}

inline void debug_log_clear() {
	debug_log_buffer().clear();
}

} // namespace vortarismodloader
