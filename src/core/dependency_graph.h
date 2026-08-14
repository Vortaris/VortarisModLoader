#pragma once

#include <unordered_map>
#include <vector>

#include <godot_cpp/variant/string.hpp>

#include "manifest.h"

namespace vortarismodloader {

// Resolves mod load order from required dependencies (plus load_before/load_after
// hints folded into edges) using a stable Kahn topological sort. A mod whose
// required dependency is missing is excluded with an error; cycles are broken by
// excluding one participant with an error.
class DependencyGraph {
public:
	/// Compute a valid load order. `r_order` receives the ordered subset of
	/// manifests that can load; `r_errors` collects per-mod problems.
	static void compute_order(const std::vector<ModManifest> &p_manifests,
			std::vector<godot::String> &r_order, std::vector<godot::String> &r_errors);

	/// Compare two semver strings ("1.2.0"). -1 / 0 / +1.
	static int compare_versions(const godot::String &p_a, const godot::String &p_b);
	/// Parse "lib_mod@>=1.0" into (id, op, version). Plain "lib_mod" passes through.
	static bool parse_dependency(const godot::String &p_dep, godot::String &r_id,
			godot::String &r_op, godot::String &r_version);
	/// True when `p_actual` satisfies "op version" (e.g. ">=1.0").
	static bool satisfies(const godot::String &p_op, const godot::String &p_actual, const godot::String &p_want);
};

} // namespace vortarismodloader
