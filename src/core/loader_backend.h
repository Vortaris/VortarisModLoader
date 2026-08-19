#pragma once

#include <functional>
#include <set>

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace vortarismodloader {

// The one bridge that turns a resolved physical path into a Godot value.
// Data-ish files (.json/.csv) parse to Dictionary/Array; everything else loads
// as a Resource through ResourceLoader. Raw image/audio/font construction for
// user:// paths (no import cache) is added in a later milestone.
class LoaderBackend {
public:
	static godot::Variant load_data(const godot::String &p_path);
	static godot::Ref<godot::Resource> load_resource(const godot::String &p_path,
			godot::ResourceLoader::CacheMode p_mode = godot::ResourceLoader::CACHE_MODE_REUSE);
	/// Construct a raw asset directly (no import cache), by extension.
	static godot::Ref<godot::Resource> load_raw_asset(const godot::String &p_path);
	static godot::Ref<godot::ImageTexture> load_image(const godot::String &p_path);
	/// Editor tooling: when true, data-loading failures are not printed as ERROR
	/// (the editor browses broken mods constantly; the mod list already surfaces the
	/// errors). Runtime builds keep the loud ERR_PRINT diagnostics.
	static void set_quiet_errors(bool p_quiet) { quiet_errors_ = p_quiet; }
	static bool is_quiet_errors() { return quiet_errors_; }

	/// 0.4.0 (conditional data loading): `@condition,<term>` directive lines at
	/// the top of a .json/.csv data file are evaluated through this callback
	/// before parsing; a failing condition skips the file entirely. Set by the
	/// VML singleton. When unset, conditions are treated as satisfied.
	static void set_condition_evaluator(std::function<bool(const godot::String &)> p_fn) {
		condition_evaluator_ = std::move(p_fn);
	}
	/// 0.4.0 (audit): paths whose data failed to parse are cached and short-
	/// circuited on later loads (avoids re-parsing + error spam per access).
	/// Cleared on rescan / reload_resources.
	static void clear_failed_paths() { failed_paths_.clear(); }

private:
	static bool quiet_errors_;
	static std::function<bool(const godot::String &)> condition_evaluator_;
	static std::set<godot::String> failed_paths_;
};

} // namespace vortarismodloader
