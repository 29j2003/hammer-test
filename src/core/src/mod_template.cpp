// mod_template.cpp - Files of a new Source mod (see mod_template.h).

#include <sme/mod_template.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace sme {

namespace {

/// Search paths of the Source SDK Base 2013 Singleplayer mods (the layout Valve's mod template
/// uses: the mod first, then Episode Two, Episode One and Half-Life 2 content).
const std::vector<std::pair<std::string, std::string>> SINGLEPLAYER_CONTENT = {
	{"game+mod", "|all_source_engine_paths|ep2/ep2_english.vpk"},
	{"game+mod", "|all_source_engine_paths|ep2/ep2_pak.vpk"},
	{"game", "|all_source_engine_paths|episodic/ep1_english.vpk"},
	{"game", "|all_source_engine_paths|episodic/ep1_pak.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_english.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_pak.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_textures.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_sound_vo_english.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_sound_misc.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_misc.vpk"},
	{"platform", "|all_source_engine_paths|platform/platform_misc.vpk"},
	{"game", "|all_source_engine_paths|ep2"},
	{"game", "|all_source_engine_paths|episodic"},
	{"game", "|all_source_engine_paths|hl2"},
	{"platform", "|all_source_engine_paths|platform"},
};

/// Search paths of Source SDK Base 2013 Multiplayer mods (Half-Life 2: Deathmatch content).
const std::vector<std::pair<std::string, std::string>> MULTIPLAYER_CONTENT = {
	{"game", "|all_source_engine_paths|hl2mp/hl2mp_english.vpk"},
	{"game", "|all_source_engine_paths|hl2mp/hl2mp_pak.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_english.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_pak.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_textures.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_sound_vo_english.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_sound_misc.vpk"},
	{"game", "|all_source_engine_paths|hl2/hl2_misc.vpk"},
	{"platform", "|all_source_engine_paths|platform/platform_misc.vpk"},
	{"game", "|all_source_engine_paths|hl2mp"},
	{"game", "|all_source_engine_paths|hl2"},
	{"platform", "|all_source_engine_paths|platform"},
};

/// Text inside a quoted KeyValues string. The engine reads files without escapes, so a quote
/// would end the string: it becomes an apostrophe; line breaks are dropped.
std::string quotedValue(std::string_view text) {
	std::string out;
	for (const char c : text) {
		if (c == '"') {
			out += '\'';
		} else if (c != '\n' && c != '\r') {
			out += c;
		}
	}
	return out;
}

/// UTF-8 to UTF-16 LE bytes (invalid sequences become U+FFFD).
void appendUtf16(std::vector<std::byte>& out, std::string_view utf8) {
	const auto push = [&out](char16_t unit) {
		out.push_back(static_cast<std::byte>(unit & 0xFF));
		out.push_back(static_cast<std::byte>(unit >> 8));
	};
	for (std::size_t i = 0; i < utf8.size();) {
		const auto c = static_cast<unsigned char>(utf8[i]);
		char32_t code = 0xFFFD;
		std::size_t length = 1;
		if (c < 0x80) {
			code = c;
		} else if ((c >> 5) == 0x6 && i + 1 < utf8.size()) {
			code = ((c & 0x1F) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
			length = 2;
		} else if ((c >> 4) == 0xE && i + 2 < utf8.size()) {
			code = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(utf8[i + 2]) & 0x3F);
			length = 3;
		} else if ((c >> 3) == 0x1E && i + 3 < utf8.size()) {
			code = ((c & 0x07) << 18) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 12) |
			       ((static_cast<unsigned char>(utf8[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(utf8[i + 3]) & 0x3F);
			length = 4;
		}
		i += length;
		if (code >= 0x10000) {
			code -= 0x10000;
			push(static_cast<char16_t>(0xD800 + (code >> 10)));
			push(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
		} else {
			push(static_cast<char16_t>(code));
		}
	}
}

bool writeFile(const std::filesystem::path& path, const void* data, std::size_t size) {
	std::ofstream out(path, std::ios::binary);
	out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
	return static_cast<bool>(out);
}

} // namespace

bool isValidModFolder(std::string_view folder) {
	if (folder.empty() || folder.size() > 64 || folder.front() == '-') {
		return false;
	}
	return std::all_of(folder.begin(), folder.end(), [](char c) {
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
	});
}

std::string suggestModFolder(std::string_view title) {
	std::string out;
	for (const char c : title) {
		const auto u = static_cast<unsigned char>(c);
		if (std::isalnum(u) && u < 0x80) {
			out += static_cast<char>(std::tolower(u));
		} else if ((c == ' ' || c == '_' || c == '-') && !out.empty() && out.back() != '_') {
			out += '_';
		}
	}
	while (!out.empty() && out.back() == '_') out.pop_back();
	return out.substr(0, 64);
}

std::string gameInfoText(const ModTemplate& mod) {
	const bool multiplayer = mod.base == ModBase::Multiplayer;
	const std::string title = quotedValue(mod.title);
	std::string text = "\"GameInfo\"\n{\n";
	text += "\tgame\t\t\"" + title + "\"\n";
	text += "\ttitle\t\t\"" + title + "\"\n";
	text += std::string("\ttype\t\t") + (multiplayer ? "multiplayer_only" : "singleplayer_only") + "\n";
	text += "\tsupportsvr\t0\n";
	text += "\n\tFileSystem\n\t{\n";
	text += std::string("\t\tSteamAppId\t\t") + (multiplayer ? "243750" : "243730") + "\n";
	text += "\n\t\tSearchPaths\n\t\t{\n";
	const auto line = [&text](const std::string& id, const std::string& path) { text += "\t\t\t" + id + "\t\t" + path + "\n"; };
	// The mod itself first: its custom folder, its own files (also where the game writes), its code.
	line("game+mod", "|gameinfo_path|custom/*");
	line("game+mod+mod_write+default_write_path", "|gameinfo_path|.");
	line("gamebin", "|gameinfo_path|bin");
	for (const auto& [id, path] : mod.extraMounts) line(id, path);
	for (const auto& [id, path] : multiplayer ? MULTIPLAYER_CONTENT : SINGLEPLAYER_CONTENT) line(id, path);
	text += "\t\t}\n\t}\n}\n";
	return text;
}

const std::vector<std::string>& modFolders() {
	static const std::vector<std::string> folders = {"bin", "cfg", "maps", "mapsrc", "materials", "models", "resource", "scripts", "sound", "particles"};
	return folders;
}

std::vector<std::byte> modResourceFile(const ModTemplate& mod) {
	std::vector<std::byte> out{std::byte{0xFF}, std::byte{0xFE}};   // BOM: UTF-16 little endian
	appendUtf16(out, "\"lang\"\r\n{\r\n\t\"Language\"\t\"English\"\r\n\t\"Tokens\"\r\n\t{\r\n");
	appendUtf16(out, "\t\t\"GameTitle\"\t\"" + quotedValue(mod.title) + "\"\r\n");
	appendUtf16(out, "\t}\r\n}\r\n");
	return out;
}

std::string modAutoexec(const ModTemplate& mod) {
	return "// autoexec.cfg of " + mod.title + " - runs every time the game starts.\n"
	       "// Handy while building maps; remove what you do not want players to have.\n"
	       "developer 0\n"
	       "// Cheats (impulse 101, noclip, god): Mapsmith's \"Run the game\" starts with +sv_cheats 1;\n"
	       "// in the console type sv_cheats 1.\n";
}

std::vector<std::pair<std::string, std::string>> mapbaseMounts(const std::filesystem::path& sourceModsDir) {
	std::vector<std::pair<std::string, std::string>> mounts;
	const auto shared = sourceModsDir / "mapbase_shared";
	std::error_code ec;
	if (!std::filesystem::is_directory(shared, ec)) {
		return mounts;
	}
	if (std::filesystem::is_directory(shared / "shared_misc", ec)) {
		mounts.push_back({"game+mod", "|gameinfo_path|../mapbase_shared/shared_misc"});
	}
	// VPKs are mounted by their name without "_dir" (shared_content_v7_0_dir.vpk -> shared_content_v7_0.vpk).
	std::vector<std::string> vpks;
	for (const auto& entry : std::filesystem::directory_iterator(shared, ec)) {
		const std::string name = entry.path().filename().string();
		if (entry.is_regular_file() && name.size() > 8 && name.ends_with("_dir.vpk")) {
			vpks.push_back(name.substr(0, name.size() - 8) + ".vpk");
		} else if (entry.is_regular_file() && name == "shared_misc.vpk") {
			vpks.push_back(name);
		}
	}
	std::sort(vpks.begin(), vpks.end());
	for (const auto& vpk : vpks) mounts.push_back({"game+mod", "|gameinfo_path|../mapbase_shared/" + vpk});
	return mounts;
}

CreatedMod createMod(const std::filesystem::path& sourceModsDir, const ModTemplate& mod, const std::vector<std::filesystem::path>& maps) {
	CreatedMod result;
	if (!isValidModFolder(mod.folder)) {
		result.error = "The folder name may only use letters, digits, '_' and '-'.";
		return result;
	}
	const auto folder = sourceModsDir / mod.folder;
	std::error_code ec;
	if (std::filesystem::exists(folder, ec)) {
		result.error = "A folder with this name is already there: " + folder.string();
		return result;
	}
	for (const auto& sub : modFolders()) {
		std::filesystem::create_directories(folder / sub, ec);
		if (ec) {
			result.error = "Could not create " + (folder / sub).string() + ": " + ec.message();
			return result;
		}
	}
	const std::string gameinfo = gameInfoText(mod);
	const std::string autoexec = modAutoexec(mod);
	const auto resource = modResourceFile(mod);
	if (!writeFile(folder / "gameinfo.txt", gameinfo.data(), gameinfo.size()) ||
	    !writeFile(folder / "cfg/autoexec.cfg", autoexec.data(), autoexec.size()) ||
	    !writeFile(folder / "resource" / (mod.folder + "_english.txt"), resource.data(), resource.size())) {
		result.error = "Could not write the files of the new mod in " + folder.string();
		return result;
	}
	for (const auto& map : maps) {
		const auto target = folder / "mapsrc" / map.filename();
		std::filesystem::copy_file(map, target, std::filesystem::copy_options::skip_existing, ec);
		if (ec) {
			result.error = "Could not copy " + map.string() + ": " + ec.message();
			return result;
		}
		result.copiedMaps.push_back(target);
	}
	result.folder = folder;
	return result;
}

} // namespace sme
