#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include <godot_cpp/variant/string.hpp>

namespace vortarismodloader {

// mtime+size change tracking used by the dev hot-reloader. Seed a set of files
// (or a whole tree), then poll() returns the paths whose (mtime, size) changed.
class ChangeWatcher {
public:
	struct Stat {
		uint64_t mtime = 0;
		uint64_t size = 0;
	};

	void seed(const godot::String &p_path);
	/// Seed every file under a directory tree (res:// or user:// path).
	void seed_tree(const godot::String &p_root);
	/// Changed paths since the last poll (or seed).
	std::vector<godot::String> poll();
	void clear();

	static bool stat_file(const godot::String &p_path, Stat &p_out);

private:
	std::map<godot::String, Stat> stats_;
};

} // namespace vortarismodloader
