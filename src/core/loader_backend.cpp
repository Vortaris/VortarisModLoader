#include "loader_backend.h"

#include <godot_cpp/classes/audio_stream_mp3.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/font_file.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include "debug_log.h"

namespace vortarismodloader {

bool LoaderBackend::quiet_errors_ = false;

namespace {

// Report a data/resource load failure. In editor tooling the same files are
// probed over and over while browsing broken mods, so gate the ERROR print and
// fall back to the debug log (invisible unless verbose is on).
void report_load_failure(const godot::String &p_msg) {
	if (LoaderBackend::is_quiet_errors()) {
		log_debug(godot::String("loader: ") + p_msg);
		return;
	}
	ERR_PRINT(godot::String("VML: ") + p_msg);
}

godot::String extension_of(const godot::String &p_path) {
	const int dot = p_path.rfind(".");
	if (dot < 0) {
		return godot::String();
	}
	return p_path.substr(dot + 1).to_lower();
}

// Minimal CSV -> Array[Dictionary] using the first row as headers. Good enough
// for data mods; the full RFC-4180 parser lives in VortarisCSV.
godot::Array parse_csv(const godot::String &p_text) {
	godot::Array out;
	const godot::PackedStringArray lines = p_text.split("\n");
	if (lines.size() < 2) {
		return out;
	}
	godot::PackedStringArray headers;
	for (const godot::String &h : lines[0].split(",")) {
		headers.push_back(h.strip_edges());
	}
	for (int i = 1; i < lines.size(); i++) {
		const godot::String line = lines[i].strip_edges();
		if (line.is_empty()) {
			continue;
		}
		godot::Dictionary row;
		const godot::PackedStringArray cells = line.split(",");
		for (int c = 0; c < headers.size(); c++) {
			godot::String val = c < cells.size() ? cells[c].strip_edges() : godot::String();
			// Best-effort numeric conversion so data mods can use plain numbers.
			if (val.is_valid_int()) {
				row[headers[c]] = val.to_int();
			} else if (val.is_valid_float()) {
				row[headers[c]] = val.to_float();
			} else {
				row[headers[c]] = val;
			}
		}
		out.push_back(row);
	}
	return out;
}

} // namespace

godot::Variant LoaderBackend::load_data(const godot::String &p_path) {
	const godot::String ext = extension_of(p_path);
	godot::Ref<godot::FileAccess> f = godot::FileAccess::open(p_path, godot::FileAccess::READ);
	if (f.is_null()) {
		report_load_failure(godot::String("cannot open data file: ") + p_path);
		return godot::Variant();
	}
	const godot::String text = f->get_as_text();
	if (ext == "json") {
		// Use the instance parse() so malformed files don't trigger the engine's own
		// "Parse JSON failed" ERROR print — we report failures ourselves (silencable
		// in the editor, L2). parse_string() has no quiet path.
		godot::Ref<godot::JSON> parser;
		parser.instantiate();
		const godot::Error jerr = parser->parse(text);
		const godot::Variant parsed = parser->get_data();
		if (jerr != godot::OK) {
			report_load_failure(godot::String("invalid JSON in ") + p_path);
		}
		log_debug(godot::String("loader: parse data '") + p_path + godot::String("' -> ") +
				(parsed.get_type() == godot::Variant::DICTIONARY ? godot::String("Dictionary") :
							godot::String("Array")));
		return parsed;
	}
	if (ext == "csv") {
		godot::Array rows = parse_csv(text);
		log_debug(godot::String("loader: parse csv '") + p_path + godot::String("' rows=") +
				godot::String::num_int64(rows.size()));
		return rows;
	}
	// Fall through: non-data file.
	return godot::Variant();
}

godot::Ref<godot::ImageTexture> LoaderBackend::load_image(const godot::String &p_path) {
	godot::Ref<godot::Image> img = godot::Image::load_from_file(p_path);
	if (img.is_null()) {
		report_load_failure(godot::String("cannot load image: ") + p_path);
		return godot::Ref<godot::ImageTexture>();
	}
	return godot::ImageTexture::create_from_image(img);
}

godot::Ref<godot::Resource> LoaderBackend::load_raw_asset(const godot::String &p_path) {
	const godot::String ext = extension_of(p_path);
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "bmp" || ext == "tga") {
		return load_image(p_path);
	}
	if (ext == "wav") {
		return godot::AudioStreamWAV::load_from_file(p_path);
	}
	if (ext == "mp3") {
		return godot::AudioStreamMP3::load_from_file(p_path);
	}
	if (ext == "ttf" || ext == "otf") {
		godot::Ref<godot::FontFile> font;
		font.instantiate();
		if (font->load_dynamic_font(p_path) != godot::OK) {
			report_load_failure(godot::String("cannot load font: ") + p_path);
			return godot::Ref<godot::Resource>();
		}
		return font;
	}
	// Fall back to the standard resource loader (text formats, scenes, scripts).
	return load_resource(p_path);
}

godot::Ref<godot::Resource> LoaderBackend::load_resource(const godot::String &p_path,
		godot::ResourceLoader::CacheMode p_mode) {
	const godot::String ext = extension_of(p_path);
	// Images: prefer the standard ResourceLoader so imported textures resolve to
	// their CompressedTexture2D. Image::load_from_file does raw file I/O on the
	// source PNG, which is stripped from exported .pck files (only the imported
	// .ctex is packed) — so raw loading breaks every image placeholder in a
	// distributed build. ResourceLoader::load on a res:// path reads the .import
	// sidecar + .ctex from the pck and works. Mod files under user:// (no import
	// cache) fall back to direct raw construction, keeping dev hot-reload intact.
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "bmp" || ext == "tga") {
		if (p_path.begins_with("res://")) {
			godot::Ref<godot::Resource> res = godot::ResourceLoader::get_singleton()->load(p_path, "", p_mode);
			if (!res.is_null()) {
				log_debug(godot::String("loader: resource '") + p_path + godot::String("' -> ") + res->get_class());
				return res;
			}
		}
		godot::Ref<godot::Resource> raw = load_raw_asset(p_path);
		if (!raw.is_null()) {
			return raw;
		}
		report_load_failure(godot::String("failed to load image: ") + p_path);
		return godot::Ref<godot::Resource>();
	}
	if (ext == "wav" || ext == "mp3" || ext == "ttf" || ext == "otf") {
		return load_raw_asset(p_path);
	}
	godot::Ref<godot::Resource> res = godot::ResourceLoader::get_singleton()->load(p_path, "", p_mode);
	if (res.is_null()) {
		report_load_failure(godot::String("failed to load resource: ") + p_path);
	} else {
		log_debug(godot::String("loader: resource '") + p_path + godot::String("' -> ") + res->get_class());
	}
	return res;
}

} // namespace vortarismodloader
