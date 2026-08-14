#include "manifest.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "resource_id.h"

namespace vortarismodloader {

namespace {

godot::String dict_str(const godot::Dictionary &p_d, const char *p_key) {
	if (!p_d.has(godot::StringName(p_key))) {
		return godot::String();
	}
	const godot::Variant v = p_d[p_key];
	return v.get_type() == godot::Variant::STRING ? godot::String(v) : godot::String();
}

void dict_str_array(const godot::Dictionary &p_d, const char *p_key, std::vector<godot::String> &p_out) {
	if (!p_d.has(godot::StringName(p_key))) {
		return;
	}
	const godot::Variant v = p_d[p_key];
	if (v.get_type() != godot::Variant::ARRAY && v.get_type() != godot::Variant::PACKED_STRING_ARRAY) {
		return;
	}
	const godot::Array a = v;
	for (int i = 0; i < a.size(); i++) {
		if (a[i].get_type() == godot::Variant::STRING) {
			p_out.push_back(godot::String(a[i]));
		}
	}
}

} // namespace

bool ManifestParser::load(const godot::String &p_json_path, ModManifest &p_out) {
	godot::Ref<godot::FileAccess> f = godot::FileAccess::open(p_json_path, godot::FileAccess::READ);
	if (f.is_null()) {
		p_out.errors.push_back(godot::String("cannot open manifest: ") + p_json_path);
		return false;
	}
	godot::Variant parsed = godot::JSON::parse_string(f->get_as_text());
	if (parsed.get_type() != godot::Variant::DICTIONARY) {
		p_out.errors.push_back(godot::String("invalid JSON in ") + p_json_path);
		return false;
	}
	const godot::Dictionary root = parsed;

	p_out.id = dict_str(root, "namespace");
	p_out.display_name = dict_str(root, "name");
	p_out.version = dict_str(root, "version_number");
	p_out.description = dict_str(root, "description");
	p_out.website_url = dict_str(root, "website_url");

	dict_str_array(root, "dependencies", p_out.deps);
	dict_str_array(root, "optional_dependencies", p_out.optional_deps);
	dict_str_array(root, "load_before", p_out.load_before);
	dict_str_array(root, "load_after", p_out.load_after);
	dict_str_array(root, "incompatibilities", p_out.incompatibilities);

	// Vortaris extension block: extra.godot.{...}
	godot::Dictionary extra;
	if (root.has("extra") && root["extra"].get_type() == godot::Variant::DICTIONARY) {
		extra = root["extra"];
	}
	godot::Dictionary godot_block;
	if (extra.has("godot") && extra["godot"].get_type() == godot::Variant::DICTIONARY) {
		godot_block = extra["godot"];
	}
	p_out.main_script = dict_str(godot_block, "main_script");
	p_out.icon_path = dict_str(godot_block, "icon");
	dict_str_array(godot_block, "asset_dirs", p_out.asset_dirs);
	dict_str_array(godot_block, "data_dirs", p_out.data_dirs);
	if (godot_block.has("config_schema") && godot_block["config_schema"].get_type() == godot::Variant::DICTIONARY) {
		p_out.config_schema = godot_block["config_schema"];
	}

	// Defaults.
	if (p_out.main_script.is_empty()) {
		p_out.main_script = "mod_main.gd";
	}
	if (p_out.asset_dirs.empty()) {
		p_out.asset_dirs.push_back("assets");
	}
	if (p_out.data_dirs.empty()) {
		p_out.data_dirs.push_back("data");
	}

	// Validation.
	if (!ResourceId::is_valid_namespace(p_out.id)) {
		p_out.errors.push_back(godot::String("invalid namespace '") + p_out.id +
				godot::String("' (expected ^[a-z0-9_]{1,32}$)"));
	}
	if (p_out.display_name.is_empty()) {
		p_out.errors.push_back("missing 'name'");
	}
	if (p_out.version.is_empty()) {
		p_out.errors.push_back("missing 'version_number'");
	}
	return p_out.valid();
}

} // namespace vortarismodloader
