// asset_import.cpp - The mapper's own sounds, pictures and particle files into the mod (see
// asset_import.h).

#include <sme/asset_import.h>

#include <sme/assets.h>
#include <sme/keyvalues.h>
#include <sme/sound_edit.h>
#include <sme/text.h>

#include <vtfpp/ImageConversion.h>
#include <vtfpp/VTF.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sme {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view BACKUP_SUFFIX = ".mapsmith-backup";

std::optional<std::string> readFile(const fs::path& file) {
	std::ifstream in(file, std::ios::binary);
	if (!in) return std::nullopt;
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

/// Writes through a temporary file next to the target and renames it (no half-written files).
bool writeFile(const fs::path& file, std::span<const std::byte> data, std::string* error) {
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	const fs::path temp = file.string() + ".mapsmith-tmp";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out) {
			if (error) *error = "cannot write " + displayPath(file);
			return false;
		}
		out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
		if (!out) {
			if (error) *error = "cannot write " + displayPath(file);
			return false;
		}
	}
	fs::rename(temp, file, ec);
	if (ec) {
		fs::remove(temp, ec);
		if (error) *error = "cannot write " + displayPath(file);
		return false;
	}
	return true;
}

bool writeText(const fs::path& file, std::string_view text, std::string* error) {
	return writeFile(file, std::as_bytes(std::span(text.data(), text.size())), error);
}

/// Writes a file of an import: a new file goes to `created`, an existing one (only with
/// `overwrite`) is first kept as a backup and goes to `replaced`.
bool writeImported(ImportRecord& record, const fs::path& file, std::span<const std::byte> data, bool overwrite, std::string* error) {
	std::error_code ec;
	if (fs::exists(file, ec)) {
		if (!overwrite) {
			if (error) *error = displayPath(file) + " already exists";
			return false;
		}
		fs::copy_file(file, file.string() + std::string(BACKUP_SUFFIX), fs::copy_options::overwrite_existing, ec);
		if (ec) {
			if (error) *error = "cannot keep a backup of " + displayPath(file);
			return false;
		}
		if (!writeFile(file, data, error)) return false;
		record.replaced.push_back(file);
		return true;
	}
	if (!writeFile(file, data, error)) return false;
	record.created.push_back(file);
	return true;
}

/// "!particles/x.pcf", "particles\X.pcf" -> "particles/x.pcf"
std::string manifestKey(std::string_view value) {
	std::string out = normalizePath(trim(value));
	while (!out.empty() && out.front() == '!') out.erase(out.begin());
	return toLower(out);
}

/// The quoted value of a manifest line ("file" "particles/x.pcf"), or nullopt.
std::optional<std::string> manifestLineValue(std::string_view line) {
	std::vector<std::string> quoted;
	for (std::size_t at = 0; at < line.size();) {
		if (line.substr(at, 2) == "//") break;
		if (line[at] == '"') {
			const auto end = line.find('"', at + 1);
			if (end == std::string_view::npos) break;
			quoted.emplace_back(line.substr(at + 1, end - at - 1));
			at = end + 1;
		} else {
			++at;
		}
	}
	if (quoted.size() != 2) return std::nullopt;
	const std::string key = toLower(quoted[0]);
	if (key != "file" && key != "precache_file" && key != "preload_file") return std::nullopt;
	return quoted[1];
}

/// Removes folders an import left empty, up to and including the top folder of its kind
/// (sound/, materials/ ...) - never the mod folder itself.
void removeEmptyFolders(fs::path dir) {
	static const std::vector<std::string> tops{"sound", "materials", "models", "modelsrc", "particles", "scripts"};
	std::error_code ec;
	for (int depth = 0; depth < 16 && !dir.empty() && dir.has_parent_path() && fs::is_directory(dir, ec) && fs::is_empty(dir, ec); ++depth) {
		const std::string name = toLower(dir.filename().string());
		fs::remove(dir, ec);
		if (std::find(tops.begin(), tops.end(), name) != tops.end()) break;
		dir = dir.parent_path();
	}
}

/// Forward slashes, case kept (paths in the list of imports).
std::string slashes(std::string text) {
	std::replace(text.begin(), text.end(), '\\', '/');
	return text;
}

} // namespace

std::string importName(std::string_view text) {
	std::string out;
	bool gap = false;
	for (const char c : text) {
		const auto u = static_cast<unsigned char>(c);
		if (std::isalnum(u) || c == '-' || c == '_') {
			if (gap && !out.empty()) out += '_';
			gap = false;
			out += static_cast<char>(std::tolower(u));
		} else {
			gap = true;
		}
	}
	return out;
}

std::string importFolder(std::string_view text) {
	std::string out;
	for (const auto& part : splitTrimmed(normalizePath(text), '/')) {
		const std::string name = importName(part);
		if (name.empty() || name == "." || name == "..") continue;
		if (!out.empty()) out += '/';
		out += name;
	}
	return out;
}

// --- Undo and the list of imports -------------------------------------------------------

bool undoImport(const ImportRecord& record, std::string* error) {
	bool ok = true;
	std::error_code ec;
	for (const auto& file : record.created) {
		fs::remove(file, ec);
		if (ec) {
			ok = false;
			if (error) *error = "cannot remove " + displayPath(file);
		}
	}
	for (const auto& file : record.replaced) {
		const fs::path backup = file.string() + std::string(BACKUP_SUFFIX);
		if (fs::exists(backup, ec)) {
			fs::rename(backup, file, ec);
			if (ec) {
				ok = false;
				if (error) *error = "cannot restore " + displayPath(file);
			}
		}
	}
	// Sound script blocks first: a script file without entries goes, and with it its manifest line.
	for (const auto& entry : record.entries) {
		if (entry.kind != ImportEntry::Kind::SoundScript) continue;
		const auto text = readFile(entry.file);
		if (!text) continue;
		try {
			KvNode root = parseKeyValues(*text);
			std::erase_if(root.children, [&entry](const KvNode& node) { return iequals(node.key, entry.value); });
			if (root.children.empty()) {
				// The last entry: the file goes, and so does its line in the mod's manifest (also when
				// an earlier import added that line).
				fs::remove(entry.file, ec);
				const fs::path manifest = entry.file.parent_path() / "game_sounds_manifest.txt";
				if (const auto listed = readFile(manifest)) {
					const std::string value = "scripts/" + entry.file.filename().string();
					const std::string without = withoutManifestEntry(*listed, value);
					if (without != *listed && !writeText(manifest, without, error)) ok = false;
				}
			} else if (!writeText(entry.file, writeKeyValues(root), error)) {
				ok = false;
			}
		} catch (const std::exception&) {
			ok = false;
			if (error) *error = displayPath(entry.file) + " cannot be read";
		}
	}
	for (const auto& entry : record.entries) {
		if (entry.kind != ImportEntry::Kind::Manifest) continue;
		// The line stays while its file is still there (another import still uses it).
		const fs::path modDir = entry.file.parent_path().parent_path();
		if (fs::exists(modDir / manifestKey(entry.value), ec)) continue;
		const auto text = readFile(entry.file);
		if (!text) continue;
		if (!writeText(entry.file, withoutManifestEntry(*text, entry.value), error)) ok = false;
	}
	for (const auto& file : record.created) removeEmptyFolders(file.parent_path());
	return ok;
}

fs::path importLogFile(const fs::path& modDir) {
	return modDir / "cfg" / "mapsmith_imports.txt";
}

std::string writeImportLog(const std::vector<ImportRecord>& records, const fs::path& modDir) {
	const auto rel = [&modDir](const fs::path& file) {
		std::error_code ec;
		const auto relative = fs::relative(file, modDir, ec);
		return slashes(displayPath(ec || relative.empty() ? file : relative));
	};
	std::string out = "// Files Mapsmith imported into this mod (Remove import takes them out again).\n";
	for (const auto& record : records) {
		out += "import\t" + record.kind + "\t" + record.asset + "\n";
		for (const auto& file : record.created) out += "created\t" + rel(file) + "\n";
		for (const auto& file : record.replaced) out += "replaced\t" + rel(file) + "\n";
		for (const auto& entry : record.entries) {
			out += std::string(entry.kind == ImportEntry::Kind::SoundScript ? "script\t" : "manifest\t") + rel(entry.file) + "\t" + entry.value + "\n";
		}
		out += "end\n";
	}
	return out;
}

std::vector<ImportRecord> readImportLog(std::string_view text, const fs::path& modDir) {
	std::vector<ImportRecord> out;
	std::optional<ImportRecord> current;
	std::istringstream lines{std::string(text)};
	for (std::string line; std::getline(lines, line);) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		std::vector<std::string> parts;
		for (std::size_t at = 0;;) {
			const auto tab = line.find('\t', at);
			parts.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
			if (tab == std::string::npos) break;
			at = tab + 1;
		}
		const auto file = [&modDir](const std::string& relative) -> std::optional<fs::path> {
			const std::string clean = slashes(relative);
			if (clean.empty() || clean.find("..") != std::string::npos || clean.front() == '/' || clean.find(':') != std::string::npos) return std::nullopt;
			return modDir / fs::path(clean);
		};
		if (parts[0] == "import" && parts.size() == 3) {
			current = ImportRecord{parts[1], parts[2], {}, {}, {}};
		} else if (!current) {
			continue;
		} else if ((parts[0] == "created" || parts[0] == "replaced") && parts.size() == 2) {
			if (const auto path = file(parts[1])) (parts[0] == "created" ? current->created : current->replaced).push_back(*path);
		} else if ((parts[0] == "script" || parts[0] == "manifest") && parts.size() == 3) {
			if (const auto path = file(parts[1])) {
				current->entries.push_back({parts[0] == "script" ? ImportEntry::Kind::SoundScript : ImportEntry::Kind::Manifest, *path, parts[2]});
			}
		} else if (parts[0] == "end") {
			out.push_back(std::move(*current));
			current.reset();
		}
	}
	return out;
}

std::vector<ImportRecord> loadImports(const fs::path& modDir) {
	const auto text = readFile(importLogFile(modDir));
	return text ? readImportLog(*text, modDir) : std::vector<ImportRecord>{};
}

bool rememberImport(const fs::path& modDir, const ImportRecord& record) {
	auto records = loadImports(modDir);
	records.push_back(record);
	return writeText(importLogFile(modDir), writeImportLog(records, modDir), nullptr);
}

bool forgetImport(const fs::path& modDir, const ImportRecord& record) {
	auto records = loadImports(modDir);
	for (auto it = records.rbegin(); it != records.rend(); ++it) {
		if (*it == record) {
			records.erase(std::next(it).base());
			break;
		}
	}
	return writeText(importLogFile(modDir), writeImportLog(records, modDir), nullptr);
}

// --- Manifests -------------------------------------------------------------------------

std::string withManifestEntry(std::string_view text, std::string_view root, std::string_view key, std::string_view value) {
	const std::string line = "\t\"" + std::string(key) + "\"\t\"" + std::string(value) + "\"\n";
	if (trim(text).empty()) {
		return std::string(root) + "\n{\n" + line + "}\n";
	}
	const std::string wanted = manifestKey(value);
	std::istringstream lines{std::string(text)};
	for (std::string each; std::getline(lines, each);) {
		if (const auto listed = manifestLineValue(each); listed && manifestKey(*listed) == wanted) {
			return std::string(text);
		}
	}
	// Before the brace that closes the first block (comments and quoted text skipped).
	int depth = 0;
	bool quoted = false;
	for (std::size_t at = 0; at < text.size(); ++at) {
		const char c = text[at];
		if (quoted) {
			quoted = c != '"';
		} else if (c == '"') {
			quoted = true;
		} else if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
			while (at < text.size() && text[at] != '\n') ++at;
		} else if (c == '{') {
			++depth;
		} else if (c == '}' && --depth == 0) {
			std::size_t lineStart = text.rfind('\n', at);
			lineStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
			const bool braceAlone = trim(text.substr(lineStart, at - lineStart)).empty();
			std::string out(text.substr(0, braceAlone ? lineStart : at));
			if (!braceAlone) out += '\n';
			out += line;
			out += text.substr(braceAlone ? lineStart : at);
			return out;
		}
	}
	std::string out(text);   // no closing brace: append a fresh block
	if (!out.empty() && out.back() != '\n') out += '\n';
	return out + std::string(root) + "\n{\n" + line + "}\n";
}

std::string withoutManifestEntry(std::string_view text, std::string_view value) {
	const std::string wanted = manifestKey(value);
	std::string out;
	std::istringstream lines{std::string(text)};
	for (std::string line; std::getline(lines, line);) {
		if (const auto listed = manifestLineValue(line); listed && manifestKey(*listed) == wanted) continue;
		out += line;
		out += '\n';
	}
	return out;
}

// --- Sounds ----------------------------------------------------------------------------

DecodedAudio prepareImportedSound(const DecodedAudio& audio, const SoundImportOptions& options) {
	DecodedAudio out = audio;
	out.loopStart.reset();
	if (options.channels == SoundImportOptions::Channels::Mono && out.channels > 1) {
		out = toMono(out);
	} else if (options.channels == SoundImportOptions::Channels::Stereo && out.channels == 1) {
		std::vector<std::int16_t> stereo;
		stereo.reserve(out.samples.size() * 2);
		for (const auto s : out.samples) stereo.insert(stereo.end(), {s, s});
		out.samples = std::move(stereo);
		out.channels = 2;
	} else if (out.channels > 2) {
		out = toMono(out);   // the engine plays mono and stereo only
	}
	int rate = options.sampleRate;
	if (rate <= 0) rate = isSourceSampleRate(out.sampleRate) ? out.sampleRate : 44100;
	if (!isSourceSampleRate(rate)) rate = 44100;
	if (out.sampleRate != rate && out.sampleRate > 0) out = resampled(out, rate);
	if (options.normalize) normalize(out, -1.0);
	if (options.loop && out.frames() > 0) out.loopStart = std::min(options.loopStart, out.frames() - 1);
	out.format = "PCM 16 bit";
	return out;
}

std::string soundScriptEntry(const SoundImportOptions& options, std::string_view wave) {
	const auto number = [](double v) {
		std::ostringstream s;
		s << v;
		return s.str();
	};
	std::string out = "\"" + options.scriptName + "\"\n{\n";
	out += "\t\"channel\"\t\"" + options.channel + "\"\n";
	out += "\t\"volume\"\t\"" + number(std::clamp(options.volume, 0.0, 1.0)) + "\"\n";
	out += "\t\"pitch\"\t\"" + std::to_string(std::clamp(options.pitch, 1, 255)) + "\"\n";
	out += "\t\"soundlevel\"\t\"" + options.soundLevel + "\"\n";
	out += "\t\"wave\"\t\"" + std::string(wave) + "\"\n";
	out += "}\n";
	return out;
}

fs::path importSoundScriptFile(const fs::path& modDir) {
	return modDir / "scripts" / "game_sounds_mapsmith.txt";
}

fs::path soundImportTarget(const fs::path& modDir, const SoundImportOptions& options) {
	const std::string folder = importFolder(options.folder);
	fs::path dir = modDir / "sound";
	if (!folder.empty()) dir /= fs::path(folder);
	return dir / (importName(options.name) + ".wav");
}

std::optional<ImportRecord> importSound(const fs::path& modDir, const DecodedAudio& audio, const SoundImportOptions& options, std::string_view gameManifest,
                                        bool overwrite, std::string* error) {
	const std::string name = importName(options.name);
	if (name.empty()) {
		if (error) *error = "the sound needs a name";
		return std::nullopt;
	}
	if (audio.samples.empty() || audio.channels <= 0) {
		if (error) *error = "the sound is empty";
		return std::nullopt;
	}
	const std::string folder = importFolder(options.folder);
	const std::string wave = (folder.empty() ? "" : folder + "/") + name + ".wav";
	ImportRecord record{"sound", wave, {}, {}, {}};

	// The script entry is checked before anything is written.
	KvNode scripts = KvNode::makeBlock({});
	const fs::path scriptFile = importSoundScriptFile(modDir);
	if (options.script) {
		if (trim(options.scriptName).empty() || options.scriptName.find('"') != std::string::npos) {
			if (error) *error = "the sound script name is empty or has a quote";
			return std::nullopt;
		}
		if (const auto text = readFile(scriptFile)) {
			try {
				scripts = parseKeyValues(*text);
			} catch (const std::exception&) {
				if (error) *error = displayPath(scriptFile) + " cannot be read";
				return std::nullopt;
			}
		}
		const bool taken = std::any_of(scripts.children.begin(), scripts.children.end(), [&](const KvNode& n) { return iequals(n.key, options.scriptName); });
		if (taken && !overwrite) {
			if (error) *error = "a sound called " + options.scriptName + " already exists";
			return std::nullopt;
		}
	}

	const auto wav = encodeWav(prepareImportedSound(audio, options));
	if (!writeImported(record, soundImportTarget(modDir, options), wav, overwrite, error)) return std::nullopt;
	if (options.script) {
		std::erase_if(scripts.children, [&](const KvNode& n) { return iequals(n.key, options.scriptName); });
		scripts.children.push_back(parseKeyValues(soundScriptEntry(options, wave)).children.front());
		if (!writeText(scriptFile, writeKeyValues(scripts), error)) {
			undoImport(record);
			return std::nullopt;
		}
		record.asset = options.scriptName;
		record.entries.push_back({ImportEntry::Kind::SoundScript, scriptFile, options.scriptName});
		const fs::path manifestFile = modDir / "scripts" / "game_sounds_manifest.txt";
		const auto own = readFile(manifestFile);
		const std::string before = own ? *own : std::string(gameManifest);
		const std::string after = withManifestEntry(before, "game_sounds_manifest", "precache_file", "scripts/game_sounds_mapsmith.txt");
		if (!own || after != *own) {
			if (!writeText(manifestFile, after, error)) {
				undoImport(record);
				return std::nullopt;
			}
			record.entries.push_back({ImportEntry::Kind::Manifest, manifestFile, "scripts/game_sounds_mapsmith.txt"});
		}
	}
	return record;
}

// --- Materials -------------------------------------------------------------------------

const std::vector<std::string>& commonSurfaceProps() {
	static const std::vector<std::string> props{
		"default", "concrete", "brick", "rock", "gravel", "dirt", "mud", "grass", "sand", "snow", "ice", "wood", "wood_box", "wood_crate", "wood_plank",
		"wood_panel", "metal", "metal_box", "metalpanel", "metalgrate", "metalvent", "chainlink", "glass", "tile", "plaster", "carpet", "cardboard",
		"paper", "plastic", "plastic_box", "rubber", "flesh", "water", "slime", "porcelain", "ceiling_tile", "computer", "pottery",
	};
	return props;
}

std::optional<ImageRgba> decodeImageFile(std::span<const std::byte> data, std::string* error) {
	try {
		vtfpp::ImageFormat format{};
		int width = 0, height = 0, frames = 0;
		const auto pixels = vtfpp::ImageConversion::convertFileToImageData(data, format, width, height, frames);
		if (pixels.empty() || width <= 0 || height <= 0 || width > 16384 || height > 16384) {
			if (error) *error = "not a picture Mapsmith can read (PNG, JPG, TGA, BMP)";
			return std::nullopt;
		}
		ImageRgba out;
		out.width = static_cast<std::uint16_t>(width);
		out.height = static_cast<std::uint16_t>(height);
		// Only the first frame of an animated picture.
		const auto firstFrame = std::span(pixels).first(std::min(pixels.size(), std::size_t(vtfpp::ImageFormatDetails::getDataLength(format, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height)))));
		out.pixels = vtfpp::ImageConversion::convertImageDataToFormat(firstFrame, format, vtfpp::ImageFormat::RGBA8888, out.width, out.height);
		if (out.pixels.size() != std::size_t(width) * height * 4) {
			if (error) *error = "the picture could not be converted";
			return std::nullopt;
		}
		return out;
	} catch (const std::exception&) {
		if (error) *error = "not a picture Mapsmith can read (PNG, JPG, TGA, BMP)";
		return std::nullopt;
	}
}

ImageAlpha imageAlpha(const ImageRgba& image) {
	bool binary = false;
	for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
		const auto a = std::to_integer<std::uint8_t>(image.pixels[i]);
		if (a == 255) continue;
		if (a != 0) return ImageAlpha::Soft;
		binary = true;
	}
	return binary ? ImageAlpha::Binary : ImageAlpha::Opaque;
}

MaterialImportOptions::Alpha effectiveAlpha(const MaterialImportOptions& options, const ImageRgba& image) {
	if (options.alpha != MaterialImportOptions::Alpha::Auto) return options.alpha;
	switch (imageAlpha(image)) {
		case ImageAlpha::Opaque: return MaterialImportOptions::Alpha::None;
		case ImageAlpha::Binary: return MaterialImportOptions::Alpha::AlphaTest;
		case ImageAlpha::Soft: return MaterialImportOptions::Alpha::Translucent;
	}
	return MaterialImportOptions::Alpha::None;
}

std::optional<std::vector<std::byte>> encodeVtf(const ImageRgba& image, bool compress, bool normalMap, unsigned maxSize, std::string* error) {
	if (image.width == 0 || image.height == 0 || image.pixels.size() != std::size_t(image.width) * image.height * 4) {
		if (error) *error = "the picture is empty";
		return std::nullopt;
	}
	try {
		vtfpp::VTF::CreationOptions options;
		options.version = 4;   // 7.4: what Source 2013 and its tools read
		options.widthResizeMethod = vtfpp::ImageConversion::ResizeMethod::POWER_OF_TWO_NEAREST;
		options.heightResizeMethod = vtfpp::ImageConversion::ResizeMethod::POWER_OF_TWO_NEAREST;
		const auto limit = static_cast<std::uint16_t>(std::clamp(maxSize, 4u, 4096u));
		options.resizeBounds.resizeMaxWidth = limit;
		options.resizeBounds.resizeMaxHeight = limit;
		const bool alpha = imageAlpha(image) != ImageAlpha::Opaque;
		if (normalMap || !compress) {
			options.outputFormat = vtfpp::ImageFormat::BGRA8888;
		} else {
			options.outputFormat = alpha ? vtfpp::ImageFormat::DXT5 : vtfpp::ImageFormat::DXT1;
		}
		if (normalMap) options.flags |= vtfpp::VTF::FLAG_V0_NORMAL;
		options.compressionLevel = 0;
		const auto vtf = vtfpp::VTF::create(image.pixels, vtfpp::ImageFormat::RGBA8888, image.width, image.height, options);
		if (!vtf) {
			if (error) *error = "the texture could not be made";
			return std::nullopt;
		}
		auto data = vtf.bake();
		if (data.empty()) {
			if (error) *error = "the texture could not be made";
			return std::nullopt;
		}
		return data;
	} catch (const std::exception&) {
		if (error) *error = "the texture could not be made";
		return std::nullopt;
	}
}

std::string materialImportName(const MaterialImportOptions& options) {
	std::string folder = importFolder(options.folder);
	if (options.use == MaterialImportOptions::Use::Model) folder = folder.empty() ? "models" : "models/" + folder;
	return (folder.empty() ? "" : folder + "/") + importName(options.name);
}

std::string importedMaterialVmt(const MaterialImportOptions& options, MaterialImportOptions::Alpha alpha, bool hasNormalMap) {
	const std::string name = materialImportName(options);
	std::string out = options.use == MaterialImportOptions::Use::Model ? "\"VertexLitGeneric\"\n{\n" : "\"LightmappedGeneric\"\n{\n";
	out += "\t\"$basetexture\" \"" + name + "\"\n";
	if (hasNormalMap) out += "\t\"$bumpmap\" \"" + name + "_normal\"\n";
	out += "\t\"$surfaceprop\" \"" + (options.surfaceProp.empty() ? std::string("default") : options.surfaceProp) + "\"\n";
	if (alpha == MaterialImportOptions::Alpha::Translucent) out += "\t\"$translucent\" \"1\"\n";
	if (alpha == MaterialImportOptions::Alpha::AlphaTest) out += "\t\"$alphatest\" \"1\"\n";
	out += "}\n";
	return out;
}

std::vector<fs::path> materialImportTargets(const fs::path& modDir, const MaterialImportOptions& options, bool hasNormalMap) {
	const fs::path base = modDir / "materials" / fs::path(materialImportName(options));
	std::vector<fs::path> out{base.string() + ".vmt", base.string() + ".vtf"};
	if (hasNormalMap) out.emplace_back(base.string() + "_normal.vtf");
	return out;
}

std::optional<ImportRecord> importMaterial(const fs::path& modDir, const ImageRgba& color, const ImageRgba* normalMap, const MaterialImportOptions& options,
                                           bool overwrite, std::string* error) {
	if (importName(options.name).empty()) {
		if (error) *error = "the material needs a name";
		return std::nullopt;
	}
	const auto targets = materialImportTargets(modDir, options, normalMap != nullptr);
	if (!overwrite) {
		std::error_code ec;
		for (const auto& file : targets) {
			if (fs::exists(file, ec)) {
				if (error) *error = displayPath(file) + " already exists";
				return std::nullopt;
			}
		}
	}
	const auto vtf = encodeVtf(color, options.compress, false, options.maxSize, error);
	if (!vtf) return std::nullopt;
	std::optional<std::vector<std::byte>> normal;
	if (normalMap) {
		normal = encodeVtf(*normalMap, false, true, std::min(options.maxSize, 2048u), error);
		if (!normal) return std::nullopt;
	}
	ImportRecord record{"material", materialImportName(options), {}, {}, {}};
	const std::string vmt = importedMaterialVmt(options, effectiveAlpha(options, color), normalMap != nullptr);
	const bool ok = writeImported(record, targets[1], *vtf, overwrite, error) && (!normal || writeImported(record, targets[2], *normal, overwrite, error)) &&
	                writeImported(record, targets[0], std::as_bytes(std::span(vmt.data(), vmt.size())), overwrite, error);
	if (!ok) {
		undoImport(record);
		return std::nullopt;
	}
	return record;
}

// --- Particles -------------------------------------------------------------------------

fs::path particleImportTarget(const fs::path& modDir, std::string_view name) {
	return modDir / "particles" / (importName(name) + ".pcf");
}

std::optional<ImportRecord> importParticles(const fs::path& modDir, std::span<const std::byte> pcf, std::string_view name, std::string_view gameManifest,
                                            bool precache, bool overwrite, std::string* error) {
	if (importName(name).empty()) {
		if (error) *error = "the particle file needs a name";
		return std::nullopt;
	}
	if (particleSystemNames(pcf).empty()) {
		if (error) *error = "not a particle file (.pcf) or it has no particle systems";
		return std::nullopt;
	}
	const std::string file = "particles/" + importName(name) + ".pcf";
	ImportRecord record{"particles", file, {}, {}, {}};
	if (!writeImported(record, particleImportTarget(modDir, name), pcf, overwrite, error)) return std::nullopt;
	const fs::path manifestFile = modDir / "particles" / "particles_manifest.txt";
	const auto own = readFile(manifestFile);
	const std::string before = own ? *own : std::string(gameManifest);
	const std::string after = withManifestEntry(before, "particles_manifest", "file", (precache ? "!" : "") + file);
	if (!own || after != *own) {
		if (!writeText(manifestFile, after, error)) {
			undoImport(record);
			return std::nullopt;
		}
		record.entries.push_back({ImportEntry::Kind::Manifest, manifestFile, file});
	}
	return record;
}

} // namespace sme
