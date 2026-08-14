#include "resource_id.h"

namespace vortarismodloader {

godot::String ResourceId::canonical() const {
	return ns + godot::String(":") + path;
}

bool ResourceId::is_valid_namespace(const godot::String &p_ns) {
	if (p_ns.length() < 1 || p_ns.length() > 32) {
		return false;
	}
	for (int i = 0; i < p_ns.length(); i++) {
		const char32_t c = p_ns[i];
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
			return false;
		}
	}
	return true;
}

bool ResourceId::is_valid_path(const godot::String &p_path) {
	if (p_path.is_empty() || p_path.length() > 256) {
		return false;
	}
	if (p_path[0] == '.' || p_path[p_path.length() - 1] == '.') {
		return false;
	}
	for (int i = 0; i < p_path.length(); i++) {
		const char32_t c = p_path[i];
		// Slashes are deliberately NOT allowed: ids are dotted (`units.knight`),
		// never file-path-like (`units/knight`).
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				c == '_' || c == '-' || c == '.';
		if (!ok) {
			return false;
		}
	}
	return true;
}

bool ResourceId::parse(const godot::String &p_s, ResourceId &p_out) {
	const int colon = p_s.find(":");
	if (colon <= 0 || colon == p_s.length() - 1) {
		return false;
	}
	const godot::String ns = p_s.substr(0, colon);
	const godot::String path = p_s.substr(colon + 1);
	if (!is_valid_namespace(ns) || !is_valid_path(path)) {
		return false;
	}
	p_out.ns = ns;
	p_out.path = path;
	return true;
}

bool ResourceId::operator==(const ResourceId &p_o) const {
	return ns == p_o.ns && path == p_o.path;
}

bool ResourceId::operator!=(const ResourceId &p_o) const {
	return !(*this == p_o);
}

uint32_t ResourceId::hash() const {
	uint32_t h = 2166136261u;
	const godot::String both = canonical();
	for (int i = 0; i < both.length(); i++) {
		h ^= (uint32_t)both[i];
		h *= 16777619u;
	}
	return h;
}

bool ResourceIdKey::operator==(const ResourceIdKey &p_o) const {
	return ns == p_o.ns && path == p_o.path;
}

size_t ResourceIdKeyHash::operator()(const ResourceIdKey &p_k) const {
	// Combine the two StringName hashes so iteration order never matters.
	uint32_t h1 = p_k.ns.hash();
	uint32_t h2 = p_k.path.hash();
	return ((size_t)h1 << 32) ^ h2;
}

} // namespace vortarismodloader
