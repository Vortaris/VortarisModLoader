#include "dependency_graph.h"

#include <algorithm>
#include <map>
#include <queue>

#include <godot_cpp/variant/packed_string_array.hpp>

namespace vortarismodloader {

int DependencyGraph::compare_versions(const godot::String &p_a, const godot::String &p_b) {
	const auto parts = [](const godot::String &v) {
		int ma = 0, mi = 0, pa = 0;
		const godot::PackedStringArray s = v.split(".");
		if (s.size() > 0 && s[0].is_valid_int()) {
			ma = s[0].to_int();
		}
		if (s.size() > 1 && s[1].is_valid_int()) {
			mi = s[1].to_int();
		}
		if (s.size() > 2 && s[2].is_valid_int()) {
			pa = s[2].to_int();
		}
		return std::make_tuple(ma, mi, pa);
	};
	const auto a = parts(p_a);
	const auto b = parts(p_b);
	if (a < b) {
		return -1;
	}
	if (a > b) {
		return 1;
	}
	return 0;
}

bool DependencyGraph::parse_dependency(const godot::String &p_dep, godot::String &r_id,
		godot::String &r_op, godot::String &r_version) {
	const int at = p_dep.find("@");
	if (at < 0) {
		r_id = p_dep;
		r_op = "";
		r_version = "";
		return true;
	}
	r_id = p_dep.substr(0, at);
	// Operators: >=, <=, >, <, = (and implicit =).
	const godot::String rest = p_dep.substr(at + 1);
	if (rest.begins_with(">=")) {
		r_op = ">=";
		r_version = rest.substr(2);
	} else if (rest.begins_with("<=")) {
		r_op = "<=";
		r_version = rest.substr(2);
	} else if (rest.begins_with(">")) {
		r_op = ">";
		r_version = rest.substr(1);
	} else if (rest.begins_with("<")) {
		r_op = "<";
		r_version = rest.substr(1);
	} else if (rest.begins_with("=")) {
		r_op = "=";
		r_version = rest.substr(1);
	} else {
		r_op = "=";
		r_version = rest;
	}
	return !r_id.is_empty() && !r_version.is_empty();
}

bool DependencyGraph::satisfies(const godot::String &p_op, const godot::String &p_actual, const godot::String &p_want) {
	if (p_op.is_empty()) {
		return true;
	}
	const int c = compare_versions(p_actual, p_want);
	if (p_op == ">=") {
		return c >= 0;
	}
	if (p_op == "<=") {
		return c <= 0;
	}
	if (p_op == ">") {
		return c > 0;
	}
	if (p_op == "<") {
		return c < 0;
	}
	return c == 0; // "="
}

void DependencyGraph::compute_order(const std::vector<ModManifest> &p_manifests,
		std::vector<godot::String> &r_order, std::vector<godot::String> &r_errors) {
	r_order.clear();
	r_errors.clear();

	// id -> manifest, id -> satisfied required deps. std::map (not unordered_map):
	// godot-cpp provides no std::hash<String> and we want deterministic order anyway.
	std::map<godot::String, const ModManifest *> by_id;
	for (const ModManifest &m : p_manifests) {
		by_id[m.id] = &m;
	}

	std::map<godot::String, std::vector<godot::String>> edges;
	std::map<godot::String, int> indeg;
	std::vector<godot::String> valid;

	// First pass: validate deps existence + versions, incompatibilities.
	for (const ModManifest &m : p_manifests) {
		indeg[m.id] = 0;
		bool ok = true;
		for (const godot::String &dep : m.deps) {
			godot::String dep_id, op, want;
			parse_dependency(dep, dep_id, op, want);
			const auto it = by_id.find(dep_id);
			if (it == by_id.end()) {
				r_errors.push_back(m.id + ": missing required dependency '" + dep_id + "'");
				ok = false;
				break;
			}
			if (!satisfies(op, it->second->version, want)) {
				r_errors.push_back(m.id + ": dependency '" + dep_id + "' version " + it->second->version +
						" does not satisfy " + op + want);
				ok = false;
				break;
			}
		}
		for (const godot::String &inc : m.incompatibilities) {
			if (by_id.find(inc) != by_id.end()) {
				r_errors.push_back(m.id + ": incompatible with '" + inc + "'");
				ok = false;
				break;
			}
		}
		if (ok) {
			valid.push_back(m.id);
		}
	}

	// Second pass: edges among valid mods (required deps + load_before/load_after).
	for (const ModManifest &m : p_manifests) {
		if (std::find(valid.begin(), valid.end(), m.id) == valid.end()) {
			continue;
		}
		for (const godot::String &dep : m.deps) {
			godot::String dep_id, op, want;
			parse_dependency(dep, dep_id, op, want);
			if (by_id.find(dep_id) != by_id.end() && std::find(valid.begin(), valid.end(), dep_id) != valid.end()) {
				// dep must load BEFORE m -> edge dep -> m (m depends on dep).
				auto &succ = edges[dep_id];
				if (std::find(succ.begin(), succ.end(), m.id) == succ.end()) {
					succ.push_back(m.id);
				}
			}
		}
		for (const godot::String &lb : m.load_before) {
			// m must load BEFORE lb -> edge m -> lb.
			if (by_id.find(lb) != by_id.end() && std::find(valid.begin(), valid.end(), lb) != valid.end()) {
				auto &succ = edges[m.id];
				if (std::find(succ.begin(), succ.end(), lb) == succ.end()) {
					succ.push_back(lb);
				}
			}
		}
		for (const godot::String &la : m.load_after) {
			// m must load AFTER la -> edge la -> m.
			if (by_id.find(la) != by_id.end() && std::find(valid.begin(), valid.end(), la) != valid.end()) {
				auto &succ = edges[la];
				if (std::find(succ.begin(), succ.end(), m.id) == succ.end()) {
					succ.push_back(m.id);
				}
			}
		}
	}
	for (auto &kv : edges) {
		std::sort(kv.second.begin(), kv.second.end());
		kv.second.erase(std::unique(kv.second.begin(), kv.second.end()), kv.second.end());
		for (const godot::String &to : kv.second) {
			indeg[to]++;
		}
	}

	// Kahn.
	std::priority_queue<godot::String, std::vector<godot::String>, std::greater<godot::String>> ready;
	for (const godot::String &id : valid) {
		if (indeg[id] == 0) {
			ready.push(id);
		}
	}
	while (!ready.empty()) {
		const godot::String id = ready.top();
		ready.pop();
		r_order.push_back(id);
		for (const godot::String &to : edges[id]) {
			if (--indeg[to] == 0) {
				ready.push(to);
			}
		}
	}
	// Anything left in `valid` but not ordered is part of a cycle: report it and
	// keep it OUT of the load order so a cycle never contributes content.
	for (const godot::String &id : valid) {
		if (std::find(r_order.begin(), r_order.end(), id) == r_order.end()) {
			r_errors.push_back(id + ": dependency cycle");
		}
	}
}

} // namespace vortarismodloader
