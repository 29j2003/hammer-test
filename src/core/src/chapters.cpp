// chapters.cpp - Chapters of a mod: read, check, write and restore (see chapters.h).

#include <sme/chapters.h>

#include <sme/filesystem.h>
#include <sme/keyvalues.h>
#include <sme/localization.h>
#include <sme/mod_menu.h>
#include <sme/text.h>
#include <sme/texture_enhance.h>

#include <vtfpp/ImageConversion.h>
#include <vtfpp/VTF.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <set>

namespace sme {

namespace {

constexpr std::size_t MAX_PICTURE_BYTES = 8u << 20;   // a chapter picture larger than this is not one
constexpr int MAX_CHAPTERS = 999;

std::optional<std::vector<std::byte>> readBytes(const std::filesystem::path& path, std::size_t limit = 64u << 20) {
	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec || size > limit) return std::nullopt;
	std::ifstream in(path, std::ios::binary);
	if (!in) return std::nullopt;
	std::vector<std::byte> out(static_cast<std::size_t>(size));
	in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
	if (!in && !in.eof()) return std::nullopt;
	return out;
}

std::vector<std::byte> bytesOf(std::string_view text) {
	std::vector<std::byte> out(text.size());
	std::transform(text.begin(), text.end(), out.begin(), [](char c) { return static_cast<std::byte>(c); });
	return out;
}

/// A file of the mod (any case), else of the game.
std::optional<std::vector<std::byte>> readModOrGame(const std::filesystem::path& modDir, std::string_view relative, const GameFileSystem* fs,
                                                    bool* fromGame = nullptr, std::size_t limit = 64u << 20) {
	if (fromGame) *fromGame = false;
	if (const auto file = resolveI(modDir, relative)) {
		if (auto bytes = readBytes(*file, limit)) return bytes;
	}
	if (fs) {
		if (auto bytes = fs->read(relative); bytes && bytes->size() <= limit) {
			if (fromGame) *fromGame = true;
			return bytes;
		}
	}
	return std::nullopt;
}

std::string folderOf(const std::filesystem::path& modDir) {
	auto folder = modDir.filename().string();
	if (folder.empty()) folder = modDir.parent_path().filename().string();
	return folder;
}

std::string chapterCfg(int n) {
	return "cfg/chapter" + std::to_string(n) + ".cfg";
}
std::string pictureBase(int n) {
	return "materials/vgui/chapters/chapter" + std::to_string(n);
}

/// The texture a chapter picture material shows ($basetexture), default vgui/chapters/chapterN.
std::string pictureTexture(const std::vector<std::byte>& vmt, int n) {
	try {
		const auto root = parseKeyValues(decodeTextFile(vmt));
		for (const auto& shader : root.children) {
			if (!shader.block) continue;
			auto texture = normalizePath(shader.get("$basetexture"));
			if (!texture.empty() && isSafeGamePath("materials/" + texture)) return texture;
		}
	} catch (const std::exception&) {
	}
	return "vgui/chapters/chapter" + std::to_string(n);
}

// --- The log of what Mapsmith wrote --------------------------------------------------------------

enum class LogKind { Created, Replaced, Removed };

struct ChapterLog {
	std::map<std::string, LogKind> entries;   ///< Relative path (as written) -> what happened first
};

ChapterLog readLog(const std::filesystem::path& modDir) {
	ChapterLog log;
	const auto text = readFileText(chapterLogFile(modDir));
	if (!text) return log;
	for (const auto& line : splitTrimmed(*text, '\n')) {
		const auto space = line.find(' ');
		if (space == std::string::npos) continue;
		const std::string kind = line.substr(0, space), path = std::string(trim(std::string_view(line).substr(space + 1)));
		if (!isSafeGamePath(path)) continue;
		if (kind == "created") log.entries[path] = LogKind::Created;
		else if (kind == "replaced") log.entries[path] = LogKind::Replaced;
		else if (kind == "removed") log.entries[path] = LogKind::Removed;
	}
	return log;
}

bool writeLog(const std::filesystem::path& modDir, const ChapterLog& log) {
	const auto file = chapterLogFile(modDir);
	std::error_code ec;
	if (log.entries.empty()) {
		std::filesystem::remove(file, ec);
		return true;
	}
	std::string text = "// Files Mapsmith's chapter editor changed (Project > Chapters > Restore original puts them back).\n";
	for (const auto& [path, kind] : log.entries) {
		text += (kind == LogKind::Created ? "created " : kind == LogKind::Replaced ? "replaced " : "removed ") + path + "\n";
	}
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	out << text;
	return static_cast<bool>(out);
}

std::filesystem::path backupOf(const std::filesystem::path& path) {
	auto backup = path;
	backup += ".bak";
	return backup;
}

/// Writes and removes mod files, keeping the log and the .bak copies.
class ModWriter {
public:
	ModWriter(std::filesystem::path modDir, ChapterSaveResult& result) : m_modDir(std::move(modDir)), m_log(readLog(m_modDir)), m_result(result) {}

	bool write(const std::string& relative, const std::vector<std::byte>& bytes) {
		if (!isSafeGamePath(relative)) return fail("unsafe path " + relative);
		const auto existing = resolveI(m_modDir, relative);
		const auto path = existing.value_or(m_modDir / relative);
		std::error_code ec;
		if (existing) {
			if (const auto old = readBytes(*existing); old && *old == bytes) return true;
			if (!m_log.entries.count(relative)) {
				const auto backup = backupOf(*existing);
				if (!std::filesystem::exists(backup, ec)) {
					std::filesystem::copy_file(*existing, backup, ec);
					if (ec) return fail("could not back up " + displayPath(*existing) + ": " + ec.message());
				}
				m_log.entries[relative] = LogKind::Replaced;
			}
		} else if (!m_log.entries.count(relative)) {
			m_log.entries[relative] = LogKind::Created;
		} else if (m_log.entries[relative] == LogKind::Removed) {
			m_log.entries[relative] = LogKind::Replaced;   // the original waits as .bak
		}
		std::filesystem::create_directories(path.parent_path(), ec);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!out) return fail("could not write " + displayPath(path));
		m_result.written.push_back(path);
		return true;
	}

	bool remove(const std::string& relative) {
		const auto existing = resolveI(m_modDir, relative);
		if (!existing) return true;
		std::error_code ec;
		const auto it = m_log.entries.find(relative);
		if (it != m_log.entries.end() && it->second == LogKind::Created) {
			m_log.entries.erase(it);   // Mapsmith made it: just gone
		} else {
			const auto backup = backupOf(*existing);
			if (!std::filesystem::exists(backup, ec)) {
				std::filesystem::copy_file(*existing, backup, ec);
				if (ec) return fail("could not back up " + displayPath(*existing) + ": " + ec.message());
			}
			if (it == m_log.entries.end()) m_log.entries[relative] = LogKind::Removed;
		}
		std::filesystem::remove(*existing, ec);
		if (ec) return fail("could not remove " + displayPath(*existing));
		m_result.removed.push_back(*existing);
		return true;
	}

	bool finish() {
		if (!writeLog(m_modDir, m_log)) return fail("could not write " + displayPath(chapterLogFile(m_modDir)));
		return m_result.error.empty();
	}

	[[nodiscard]] bool ok() const { return m_result.error.empty(); }

private:
	bool fail(std::string text) {
		if (m_result.error.empty()) m_result.error = std::move(text);
		return false;
	}

	std::filesystem::path m_modDir;
	ChapterLog m_log;
	ChapterSaveResult& m_result;
};

/// The centre of the picture with the aspect `w`:`h`.
ImageRgba cropToAspect(const ImageRgba& photo, double w, double h) {
	if (photo.width == 0 || photo.height == 0) return {};
	const double aspect = w / h, current = double(photo.width) / double(photo.height);
	unsigned cw = photo.width, ch = photo.height;
	if (current > aspect) cw = std::max(1u, unsigned(std::lround(photo.height * aspect)));
	else ch = std::max(1u, unsigned(std::lround(photo.width / aspect)));
	const unsigned x0 = (photo.width - cw) / 2, y0 = (photo.height - ch) / 2;
	ImageRgba out;
	out.width = static_cast<std::uint16_t>(cw);
	out.height = static_cast<std::uint16_t>(ch);
	out.pixels.resize(std::size_t(cw) * ch * 4);
	for (unsigned y = 0; y < ch; ++y) {
		const auto* src = photo.pixels.data() + (std::size_t(y0 + y) * photo.width + x0) * 4;
		std::copy(src, src + std::size_t(cw) * 4, out.pixels.data() + std::size_t(y) * cw * 4);
	}
	return out;
}

bool safeImage(const ImageRgba& image) {
	return image.width > 0 && image.height > 0 && image.pixels.size() == std::size_t(image.width) * image.height * 4;
}

} // namespace

bool isSafeMapName(std::string_view name) {
	if (name.empty() || name.size() > 63 || name == "." || name == "..") return false;
	return std::all_of(name.begin(), name.end(), [](char c) {
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
	}) && name.find("..") == std::string_view::npos;
}

std::string Chapter::title(std::string_view language) const {
	if (const auto it = titles.find(toLower(language)); it != titles.end() && !it->second.empty()) return it->second;
	if (const auto it = titles.find("english"); it != titles.end()) return it->second;
	return {};
}

std::filesystem::path chapterLogFile(const std::filesystem::path& modDir) {
	return modDir / "cfg" / "mapsmith_chapters.txt";
}

bool hasChapterChanges(const std::filesystem::path& modDir) {
	return !readLog(modDir).entries.empty();
}

// --- scripts/chapters.txt ------------------------------------------------------------------------

ChapterList parseChapterList(std::string_view text) {
	ChapterList list;
	KvNode root;
	try {
		root = parseKeyValues(text);
	} catch (const std::exception&) {
		return list;
	}
	// "ChapterList" { "Chapters" { "1" "#T" ... } "map" "#T" ... } - the root block's name does not matter.
	const KvNode* block = nullptr;
	for (const auto& child : root.children) {
		if (child.block) {
			block = &child;
			break;
		}
	}
	if (!block) return list;
	for (const auto& entry : block->children) {
		if (entry.block) {
			if (!iequals(entry.key, "Chapters")) continue;
			for (const auto& chapter : entry.children) {
				if (chapter.block) continue;
				int number = 0;
				for (const char c : chapter.key) {
					if (!std::isdigit(static_cast<unsigned char>(c)) || number > MAX_CHAPTERS) {
						number = 0;
						break;
					}
					number = number * 10 + (c - '0');
				}
				if (number > 0 && number <= MAX_CHAPTERS) list.chapters.emplace_back(number, chapter.value);
			}
		} else if (!entry.key.empty()) {
			list.maps.emplace_back(entry.key, entry.value);
		}
	}
	return list;
}

std::string writeChapterList(const ChapterList& list) {
	const auto quote = [](std::string_view text) {
		std::string out = "\"";
		for (const char c : text) {
			if (c != '"' && c != '\n' && c != '\r') out += c;
		}
		return out + "\"";
	};
	std::string out = "// Mapbase: which maps belong to which chapter - unlocks chapters in New Game and names saves.\n"
	                  "// Written by Mapsmith (Project > Chapters).\n\"ChapterList\"\n{\n\t\"Chapters\"\n\t{\n";
	for (const auto& [number, token] : list.chapters) out += "\t\t" + quote(std::to_string(number)) + "\t" + quote(token) + "\n";
	out += "\t}\n";
	for (const auto& [map, token] : list.maps) out += "\t" + quote(map) + "\t" + quote(token) + "\n";
	out += "}\n";
	return out;
}

ChapterList chapterListFor(const std::vector<Chapter>& chapters, std::string_view modFolder) {
	ChapterList list;
	std::vector<std::pair<std::string, std::string>> maps;
	std::set<std::string> seen;
	for (std::size_t i = 0; i < chapters.size(); ++i) {
		const int number = static_cast<int>(i) + 1;
		const std::string token = "#" + chapterTitleToken(modFolder, number);
		list.chapters.emplace_back(number, token);
		std::vector<std::string> names{chapters[i].map};
		names.insert(names.end(), chapters[i].moreMaps.begin(), chapters[i].moreMaps.end());
		for (const auto& name : names) {
			if (!isSafeMapName(name) || !seen.insert(toLower(name)).second) continue;
			maps.emplace_back(name, token);
		}
	}
	// The game takes the first entry whose name begins the map name: longer names first.
	std::stable_sort(maps.begin(), maps.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
	list.maps = std::move(maps);
	return list;
}

// --- Loading -------------------------------------------------------------------------------------

ChapterSet loadChapters(const std::filesystem::path& modDir, const GameFileSystem* fs) {
	ChapterSet set;
	const std::string folder = folderOf(modDir);
	const std::string setName = toLower(folder);

	// Languages: the mod's own localization files (English always).
	std::map<std::string, LocalizationFile> files;
	for (const auto& localization : findLocalizationSets(modDir)) {
		if (localization.name != setName) continue;
		for (const auto& [language, path] : localization.languages) {
			if (auto file = loadLocalizationFile(path)) files.emplace(language, std::move(*file));
		}
	}
	set.languages.push_back("english");
	for (const auto& [language, file] : files) {
		if (language != "english") set.languages.push_back(language);
	}

	std::map<int, std::string> backgrounds;
	if (const auto bytes = readModOrGame(modDir, "scripts/ChapterBackgrounds.txt", fs, nullptr, 1u << 20)) {
		backgrounds = parseChapterBackgrounds(decodeTextFile(*bytes));
	}
	ChapterList list;
	if (const auto file = resolveI(modDir, "scripts/chapters.txt")) {
		if (const auto bytes = readBytes(*file, 1u << 20)) {
			list = parseChapterList(decodeTextFile(*bytes));
			set.chapterList = true;
		}
	}

	for (int number = 1; number <= MAX_CHAPTERS; ++number) {
		const auto cfgFile = resolveI(modDir, chapterCfg(number));
		if (!cfgFile) break;
		const auto cfg = readBytes(*cfgFile, 1u << 20);
		if (!cfg) break;
		Chapter chapter;
		chapter.map = chapterMap(decodeTextFile(*cfg));
		const std::string token = chapterTitleToken(folder, number);
		for (const auto& [language, file] : files) {
			if (auto text = file.get(token); !text.empty()) chapter.titles[language] = std::move(text);
		}
		if (const auto it = backgrounds.find(number); it != backgrounds.end()) chapter.background = it->second;
		// Its other maps in Mapbase's list (everything with this chapter's title token).
		for (const auto& [map, title] : list.maps) {
			if (iequals(title, "#" + token) && !iequals(map, chapter.map) && isSafeMapName(map)) chapter.moreMaps.push_back(map);
		}
		// Its picture: the material's texture, from the mod or the game.
		bool vmtFromGame = false;
		if (const auto vmt = readModOrGame(modDir, pictureBase(number) + ".vmt", fs, &vmtFromGame, 64u << 10)) {
			const std::string texture = "materials/" + pictureTexture(*vmt, number) + ".vtf";
			bool vtfFromGame = false;
			if (auto vtf = readModOrGame(modDir, texture, fs, &vtfFromGame, MAX_PICTURE_BYTES); vtf && readTextureInfo(*vtf)) {
				chapter.picture = std::move(*vtf);
				chapter.pictureFromGame = vmtFromGame && vtfFromGame;
				chapter.pictureNumber = number;
			}
		}
		set.chapters.push_back(std::move(chapter));
	}
	return set;
}

// --- Saving --------------------------------------------------------------------------------------

ChapterSaveResult saveChapters(const std::filesystem::path& modDir, const ChapterSet& set) {
	ChapterSaveResult result;
	if (!std::filesystem::is_directory(modDir)) {
		result.error = "the mod folder does not exist: " + displayPath(modDir);
		return result;
	}
	for (std::size_t i = 0; i < set.chapters.size(); ++i) {
		const auto& chapter = set.chapters[i];
		if (!chapter.map.empty() && !isSafeMapName(chapter.map)) {
			result.error = "chapter " + std::to_string(i + 1) + ": \"" + chapter.map + "\" is not a map name";
			return result;
		}
		if (!chapter.background.empty() && !isSafeMapName(chapter.background)) {
			result.error = "chapter " + std::to_string(i + 1) + ": \"" + chapter.background + "\" is not a map name";
			return result;
		}
	}
	if (set.chapters.size() > static_cast<std::size_t>(MAX_CHAPTERS)) {
		result.error = "too many chapters";
		return result;
	}
	const std::string folder = folderOf(modDir);
	ModWriter writer(modDir, result);
	const int count = static_cast<int>(set.chapters.size());

	// Configs 1..n; configs past the last chapter would still show in New Game.
	for (int n = 1; n <= count && writer.ok(); ++n) {
		std::string old;
		if (const auto file = resolveI(modDir, chapterCfg(n))) {
			if (const auto bytes = readBytes(*file, 1u << 20)) old = decodeTextFile(*bytes);
		}
		writer.write(chapterCfg(n), bytesOf(setChapterMap(old, set.chapters[std::size_t(n - 1)].map)));
	}
	int oldCount = count;
	for (int n = count + 1; n <= MAX_CHAPTERS && writer.ok(); ++n) {
		if (!resolveI(modDir, chapterCfg(n))) break;
		writer.remove(chapterCfg(n));
		oldCount = n;
	}

	// Titles in every language.
	std::vector<std::string> languages = set.languages;
	if (std::find(languages.begin(), languages.end(), "english") == languages.end()) languages.insert(languages.begin(), "english");
	for (const auto& language : languages) {
		if (!writer.ok()) break;
		if (language.empty() || !std::all_of(language.begin(), language.end(), [](char c) { return std::islower(static_cast<unsigned char>(c)); })) continue;
		const std::string relative = "resource/" + toLower(folder) + "_" + language + ".txt";
		LocalizationFile file;
		const auto existing = resolveI(modDir, relative);
		if (existing) {
			file = loadLocalizationFile(*existing).value_or(LocalizationFile{});
		} else {
			file.language = languageDisplayName(language);
		}
		const std::string before = writeLocalizationFile(file);
		for (int n = 1; n <= std::max(count, oldCount); ++n) {
			const std::string token = chapterTitleToken(folder, n);
			std::string text;
			if (n <= count) {
				const auto& titles = set.chapters[std::size_t(n - 1)].titles;
				if (const auto it = titles.find(language); it != titles.end()) text = it->second;
			}
			if (text.empty()) file.remove(token);
			else file.set(token, text);
		}
		const std::string after = writeLocalizationFile(file);
		if (after != before || (!existing && !file.tokens.empty())) writer.write(relative, encodeUtf16Le(after));
	}

	// Pictures: moved with their chapter, removed where a chapter has none.
	for (int n = 1; n <= count && writer.ok(); ++n) {
		const auto& chapter = set.chapters[std::size_t(n - 1)];
		const std::string base = pictureBase(n);
		if (chapter.picture.empty()) {
			writer.remove(base + ".vmt");
			writer.remove(base + ".vtf");
			continue;
		}
		if (chapter.pictureFromGame && chapter.pictureNumber == n) continue;   // the game shows it anyway
		const auto vtfBefore = resolveI(modDir, base + ".vtf");
		const bool vmtMissing = !resolveI(modDir, base + ".vmt");
		const auto oldVtf = vtfBefore ? readBytes(*vtfBefore, MAX_PICTURE_BYTES) : std::nullopt;
		if (!oldVtf || *oldVtf != chapter.picture || vmtMissing || chapter.pictureNumber != n) {
			writer.write(base + ".vtf", chapter.picture);
			writer.write(base + ".vmt", bytesOf(chapterPictureVmt(n)));
		}
	}
	for (int n = count + 1; n <= std::max(oldCount, count + 8) && writer.ok(); ++n) {
		writer.remove(pictureBase(n) + ".vmt");
		writer.remove(pictureBase(n) + ".vtf");
	}

	// Background maps behind the menu.
	std::map<int, std::string> backgrounds;
	for (int n = 1; n <= count; ++n) {
		if (const auto& bg = set.chapters[std::size_t(n - 1)].background; !bg.empty()) backgrounds[n] = bg;
	}
	if (writer.ok() && (!backgrounds.empty() || resolveI(modDir, "scripts/ChapterBackgrounds.txt"))) {
		writer.write("scripts/ChapterBackgrounds.txt", bytesOf(writeChapterBackgrounds(backgrounds)));
	}

	// New loading pictures.
	for (const auto& [background, photo] : set.newLoadingPictures) {
		if (!writer.ok()) break;
		if (!isSafeMapName(background) || !safeImage(photo)) continue;
		for (const bool wide : {false, true}) {
			const std::string name = background + (wide ? "_widescreen" : "");
			std::string error;
			const auto vtf = encodeMenuVtf(loadingPictureImage(photo, wide), true, &error);
			if (!vtf) {
				result.error = "loading picture " + name + ": " + error;
				break;
			}
			writer.write("materials/console/" + name + ".vtf", *vtf);
			writer.write("materials/console/" + name + ".vmt", bytesOf(loadingPictureVmt(name)));
		}
	}

	// Mapbase's chapter list: unlocking and save names.
	if (writer.ok() && (count > 0 || set.chapterList)) {
		if (count > 0) writer.write("scripts/chapters.txt", bytesOf(writeChapterList(chapterListFor(set.chapters, folder))));
		else writer.remove("scripts/chapters.txt");
	}
	writer.finish();
	return result;
}

bool restoreChapters(const std::filesystem::path& modDir, std::string* error) {
	const auto log = readLog(modDir);
	std::error_code ec;
	bool ok = true;
	ChapterLog left;
	for (const auto& [relative, kind] : log.entries) {
		const auto existing = resolveI(modDir, relative);
		const auto path = existing.value_or(modDir / relative);
		if (kind == LogKind::Created) {
			if (existing) std::filesystem::remove(*existing, ec);
		} else {
			const auto backup = backupOf(path);
			if (std::filesystem::exists(backup, ec)) {
				std::filesystem::create_directories(path.parent_path(), ec);
				std::filesystem::copy_file(backup, path, std::filesystem::copy_options::overwrite_existing, ec);
				if (!ec) std::filesystem::remove(backup, ec);
			}
		}
		if (ec) {
			ok = false;
			left.entries[relative] = kind;
			if (error && error->empty()) *error = "could not restore " + displayPath(path) + ": " + ec.message();
			ec.clear();
		}
	}
	writeLog(modDir, left);
	return ok;
}

// --- Pictures ------------------------------------------------------------------------------------

ImageRgba chapterPictureImage(const ImageRgba& photo) {
	ImageRgba out;
	out.width = CHAPTER_PICTURE_WIDTH;
	out.height = CHAPTER_PICTURE_HEIGHT;
	out.pixels.assign(std::size_t(out.width) * out.height * 4, std::byte{0});
	if (!safeImage(photo)) return out;
	const auto shown = resizeLanczos(cropToAspect(photo, CHAPTER_PICTURE_SHOWN_WIDTH, CHAPTER_PICTURE_SHOWN_HEIGHT), CHAPTER_PICTURE_SHOWN_WIDTH,
	                                 CHAPTER_PICTURE_SHOWN_HEIGHT, EdgeMode::Clamp, EdgeMode::Clamp);
	if (!safeImage(shown)) return out;
	for (unsigned y = 0; y < shown.height; ++y) {
		const auto* src = shown.pixels.data() + std::size_t(y) * shown.width * 4;
		auto* dst = out.pixels.data() + std::size_t(y) * out.width * 4;
		std::copy(src, src + std::size_t(shown.width) * 4, dst);
		for (unsigned x = 0; x < shown.width; ++x) dst[x * 4 + 3] = std::byte{255};
	}
	return out;
}

ImageRgba loadingPictureImage(const ImageRgba& photo, bool widescreen) {
	if (!safeImage(photo)) return {};
	auto out = resizeLanczos(cropToAspect(photo, widescreen ? 16 : 4, widescreen ? 9 : 3), LOADING_PICTURE_SIZE, LOADING_PICTURE_SIZE, EdgeMode::Clamp,
	                         EdgeMode::Clamp);
	for (std::size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = std::byte{255};
	return out;
}

std::optional<std::vector<std::byte>> encodeMenuVtf(const ImageRgba& image, bool compress, std::string* error) {
	if (!safeImage(image)) {
		if (error) *error = "the picture is empty";
		return std::nullopt;
	}
	try {
		vtfpp::VTF::CreationOptions options;
		options.version = 4;
		options.widthResizeMethod = vtfpp::ImageConversion::ResizeMethod::POWER_OF_TWO_NEAREST;
		options.heightResizeMethod = vtfpp::ImageConversion::ResizeMethod::POWER_OF_TWO_NEAREST;
		options.resizeBounds.resizeMaxWidth = 2048;
		options.resizeBounds.resizeMaxHeight = 2048;
		bool opaque = true;
		for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
			if (image.pixels[i] != std::byte{255}) {
				opaque = false;
				break;
			}
		}
		options.outputFormat = compress && opaque ? vtfpp::ImageFormat::DXT1 : vtfpp::ImageFormat::BGRA8888;
		options.flags = vtfpp::VTF::FLAG_V0_NO_MIP | vtfpp::VTF::FLAG_V0_NO_LOD | vtfpp::VTF::FLAG_V0_CLAMP_S | vtfpp::VTF::FLAG_V0_CLAMP_T;
		options.computeMips = false;
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

std::string chapterPictureVmt(int chapter) {
	return "\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"vgui/chapters/chapter" + std::to_string(chapter) +
	       "\"\n\t\"$translucent\" \"1\"\n\t\"$vertexcolor\" \"1\"\n\t\"$vertexalpha\" \"1\"\n\t\"$ignorez\" \"1\"\n\t\"$no_fullbright\" \"1\"\n}\n";
}

std::string loadingPictureVmt(std::string_view textureName) {
	return "\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"console/" + std::string(textureName) +
	       "\"\n\t\"$vertexcolor\" \"1\"\n\t\"$vertexalpha\" \"1\"\n\t\"$ignorez\" \"1\"\n\t\"$no_fullbright\" \"1\"\n\t\"$nolod\" \"1\"\n}\n";
}

// --- Checks --------------------------------------------------------------------------------------

std::vector<ChapterProblem> checkChapters(const ChapterSet& set, const std::function<MapState(const std::string&)>& mapState,
                                          const std::function<bool(const std::string&)>& hasLoadingPicture) {
	std::vector<ChapterProblem> problems;
	if (set.chapters.empty()) {
		problems.push_back({0, false, "No chapters: New Game has nothing to offer."});
		return problems;
	}
	std::map<std::string, int> firstUse;
	for (std::size_t i = 0; i < set.chapters.size(); ++i) {
		const int n = static_cast<int>(i) + 1;
		const auto& chapter = set.chapters[i];
		if (chapter.map.empty()) {
			problems.push_back({n, true, "No map: New Game cannot start this chapter."});
		} else if (!isSafeMapName(chapter.map)) {
			problems.push_back({n, true, "\"" + chapter.map + "\" is not a map name."});
		} else {
			const auto state = mapState ? mapState(chapter.map) : MapState::Compiled;
			if (state == MapState::Missing) problems.push_back({n, true, "Map \"" + chapter.map + "\" not found."});
			else if (state == MapState::SourceOnly) problems.push_back({n, false, "Map \"" + chapter.map + "\" is not compiled yet."});
			if (const auto [it, added] = firstUse.emplace(toLower(chapter.map), n); !added) {
				problems.push_back({n, false, "Starts the same map as chapter " + std::to_string(it->second) + "."});
			}
		}
		for (const auto& more : chapter.moreMaps) {
			if (!isSafeMapName(more)) problems.push_back({n, true, "\"" + more + "\" is not a map name."});
		}
		if (chapter.title("english").empty()) {
			problems.push_back({n, false, "No title: New Game shows the token name."});
		} else {
			std::string missing;
			for (const auto& language : set.languages) {
				if (language == "english") continue;
				if (const auto it = chapter.titles.find(language); it == chapter.titles.end() || it->second.empty()) {
					missing += (missing.empty() ? "" : ", ") + languageDisplayName(language);
				}
			}
			if (!missing.empty()) problems.push_back({n, false, "No title in " + missing + " (English is shown)."});
		}
		if (chapter.picture.empty()) {
			problems.push_back({n, false, "No picture: New Game shows an empty frame."});
		} else if (const auto info = readTextureInfo(chapter.picture);
		           !info || info->width != CHAPTER_PICTURE_WIDTH || info->height != CHAPTER_PICTURE_HEIGHT) {
			problems.push_back({n, false, "The picture is not 256x128 (New Game shows the top-left 152x86 of it)."});
		}
		if (!chapter.background.empty()) {
			if (!isSafeMapName(chapter.background)) {
				problems.push_back({n, true, "\"" + chapter.background + "\" is not a map name."});
			} else {
				if (mapState && mapState(chapter.background) == MapState::Missing) {
					problems.push_back({n, false, "Background map \"" + chapter.background + "\" not found."});
				}
				const bool newPicture = set.newLoadingPictures.count(chapter.background) > 0;
				if (!newPicture && hasLoadingPicture && !hasLoadingPicture(chapter.background)) {
					problems.push_back({n, false, "No loading picture for \"" + chapter.background + "\" (materials/console/" + chapter.background + ")."});
				}
			}
		}
	}
	return problems;
}

} // namespace sme
