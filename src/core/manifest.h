#pragma once

#include <vector>

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace vortarismodloader {

// A parsed mod manifest. Field names follow the Thunderstore manifest
// convention so the same file works on mod distribution platforms; Vortaris
// specific keys live under `extra.godot`.
struct ModManifest {
	godot::String id; // content namespace == mod id
	godot::String display_name;
	godot::String version; // "1.2.0" (semver)
	godot::String description;
	godot::String website_url;
	godot::String main_script; // "mod_main.gd" (relative to mod root)
	godot::String icon_path; // "icon.png"
	godot::Dictionary config_schema; // extra.godot.config_schema (JSON-Schema style)

	// "lib_mod" or "lib_mod@>=1.0"
	std::vector<godot::String> deps;
	std::vector<godot::String> optional_deps;
	std::vector<godot::String> load_before;
	std::vector<godot::String> load_after;
	std::vector<godot::String> incompatibilities;

	std::vector<godot::String> asset_dirs; // default { "assets" }
	std::vector<godot::String> data_dirs; // default { "data" }

	std::vector<godot::String> errors;

	bool valid() const { return errors.empty(); }
};

class ManifestParser {
public:
	/// Read + parse + validate a manifest.json. `p_out` may carry errors even when
	/// this returns true (warnings); returns false only when the file was unusable.
	static bool load(const godot::String &p_json_path, ModManifest &p_out);
};

} // namespace vortarismodloader
