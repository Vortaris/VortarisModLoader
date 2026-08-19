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
std::function<bool(const godot::String &)> LoaderBackend::condition_evaluator_ = nullptr;
std::set<godot::String> LoaderBackend::failed_paths_;

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

// CSV -> Array[Dictionary] using the first row as headers.
// RFC-4180 aware: quoted fields may contain commas, newlines and escaped
// quotes ("" -> "). This matters for data-mod tables whose cells hold JSON
// (e.g. an ECS component `schema` column) — the naive split(",") used to
// truncate such cells at the first inner comma. A full-featured parser still
// lives in VortarisCSV; this one is deliberately small but correct.
godot::Array parse_csv(const godot::String &p_text) {
	godot::Array out;

	// Parse into rows of fields (character-level, quote-aware).
	std::vector<godot::PackedStringArray> rows;
	godot::PackedStringArray cur_row;
	godot::String field;
	bool in_quotes = false;
	int64_t i = 0;
	const int64_t len = p_text.length();
	// Strip a leading UTF-8 BOM (decoded to U+FEFF by get_as_text).
	if (len > 0 && p_text[0] == 0xFEFF) {
		i = 1;
	}
	for (; i < len; i++) {
		const char32_t c = p_text[i];
		if (in_quotes) {
			if (c == U'"') {
				if (i + 1 < len && p_text[i + 1] == U'"') {
					field += U'"'; // escaped quote
					i++;
				} else {
					in_quotes = false;
				}
			} else {
				field += c; // commas / newlines inside quotes are literal
			}
		} else {
			if (c == U'"') {
				in_quotes = true;
			} else if (c == U',') {
				cur_row.push_back(field);
				field = godot::String();
			} else if (c == U'\n' || c == U'\r') {
				if (c == U'\r' && i + 1 < len && p_text[i + 1] == U'\n') {
					i++; // consume CRLF as one newline
				}
				cur_row.push_back(field);
				field = godot::String();
				rows.push_back(cur_row);
				cur_row = godot::PackedStringArray();
			} else {
				field += c;
			}
		}
	}
	if (!field.is_empty() || cur_row.size() > 0) {
		cur_row.push_back(field);
		rows.push_back(cur_row);
	}

	if (rows.size() < 2) {
		return out;
	}
	// First row = headers.
	godot::PackedStringArray headers;
	for (const godot::String &h : rows[0]) {
		headers.push_back(h.strip_edges());
	}
	// Data rows.
	for (size_t r = 1; r < rows.size(); r++) {
		const godot::PackedStringArray &cells = rows[r];
		if (cells.size() == 1 && cells[0].strip_edges().is_empty()) {
			continue; // blank line
		}
		godot::Dictionary row;
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
	// Audit fix: a path that already failed to parse is short-circuited, so a
	// broken data file is reported once instead of re-parsed (and re-errored)
	// on every get_data / preload pass.
	if (failed_paths_.count(p_path) > 0) {
		return godot::Variant();
	}
	const godot::String ext = extension_of(p_path);
	godot::Ref<godot::FileAccess> f = godot::FileAccess::open(p_path, godot::FileAccess::READ);
	if (f.is_null()) {
		report_load_failure(godot::String("cannot open data file: ") + p_path);
		return godot::Variant();
	}
	godot::String text = f->get_as_text();

	// 0.4.0 (conditional data loading): leading `@directive` lines configure how
	// the file loads without breaking JSON/CSV parsers. Recognized today:
	//   @condition,<term>   — skip this file entirely unless <term> holds.
	// Directives must be the FIRST lines of the file (blank lines allowed among
	// them); the first non-directive, non-blank line starts the payload. Terms
	// are evaluated by the VML singleton (mod_loaded:, tags_populated:, ...).
	if (text.begins_with("@")) {
		const godot::PackedStringArray lines = text.split("\n");
		int64_t start = 0;
		bool skip_file = false;
		for (int64_t i = 0; i < lines.size(); i++) {
			const godot::String line = lines[i].strip_edges();
			if (line.is_empty()) {
				start = i + 1;
				continue; // blank lines between directives are fine
			}
			if (!line.begins_with("@")) {
				start = i;
				break; // payload begins
			}
			start = i + 1;
			const int comma = line.find(",");
			const godot::String name = (comma >= 0 ? line.substr(0, comma) : line).strip_edges();
			const godot::String arg = comma >= 0 ? line.substr(comma + 1).strip_edges() : godot::String();
			if (name == "@condition") {
				const bool ok = condition_evaluator_ ? condition_evaluator_(arg) : true;
				if (!ok) {
					skip_file = true;
					log_debug(godot::String("loader: condition '") + arg +
							godot::String("' not met, skipping ") + p_path);
					break;
				}
			}
			// Unknown directives are ignored (forward compatible).
		}
		if (skip_file) {
			return godot::Variant();
		}
		// Rebuild the payload without the directive header.
		godot::String body;
		for (int64_t i = start; i < lines.size(); i++) {
			if (i > start) {
				body += "\n";
			}
			body += lines[i];
		}
		text = body;
	}

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
			failed_paths_.insert(p_path);
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
