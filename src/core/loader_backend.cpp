#include "loader_backend.h"

#include <godot_cpp/classes/audio_stream_mp3.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/font_file.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/error_macros.hpp>

namespace vortarismodloader {

namespace {

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
		ERR_PRINT(godot::String("VML: cannot open data file: ") + p_path);
		return godot::Variant();
	}
	const godot::String text = f->get_as_text();
	if (ext == "json") {
		godot::Variant parsed = godot::JSON::parse_string(text);
		if (parsed.get_type() == godot::Variant::NIL) {
			ERR_PRINT(godot::String("VML: invalid JSON in ") + p_path);
		}
		return parsed;
	}
	if (ext == "csv") {
		return parse_csv(text);
	}
	// Fall through: non-data file.
	return godot::Variant();
}

godot::Ref<godot::ImageTexture> LoaderBackend::load_image(const godot::String &p_path) {
	godot::Ref<godot::Image> img = godot::Image::load_from_file(p_path);
	if (img.is_null()) {
		ERR_PRINT(godot::String("VML: cannot load image: ") + p_path);
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
			ERR_PRINT(godot::String("VML: cannot load font: ") + p_path);
			return godot::Ref<godot::Resource>();
		}
		return font;
	}
	// Fall back to the standard resource loader (text formats, scenes, scripts).
	return load_resource(p_path);
}

godot::Ref<godot::Resource> LoaderBackend::load_resource(const godot::String &p_path,
		godot::ResourceLoader::CacheMode p_mode) {
	// Raw assets are constructed directly so they work identically under
	// res:// (imported) and user:// (no import cache) and hot-reload cleanly.
	const godot::String ext = extension_of(p_path);
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "bmp" || ext == "tga" ||
			ext == "wav" || ext == "mp3" || ext == "ttf" || ext == "otf") {
		return load_raw_asset(p_path);
	}
	godot::Ref<godot::Resource> res = godot::ResourceLoader::get_singleton()->load(p_path, "", p_mode);
	if (res.is_null()) {
		ERR_PRINT(godot::String("VML: failed to load resource: ") + p_path);
	}
	return res;
}

} // namespace vortarismodloader
