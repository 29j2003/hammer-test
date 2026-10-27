// mod_menu.cpp - Reading and writing what a mod shows in its main menu (see mod_menu.h).

#include <sme/mod_menu.h>

#include <sme/filesystem.h>
#include <sme/text.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace sme {

namespace {

// --- Small helpers ---------------------------------------------------------------------------

/// A token of a line: its text and where it stands in the line.
struct LineToken {
	std::string text;
	std::size_t begin = 0, end = 0;   ///< [begin, end) in the line, quotes included
};

/// Splits one line of KeyValues text into tokens (quoted or plain), stopping at a // comment.
/// Braces are tokens of their own.
std::vector<LineToken> lineTokens(std::string_view line) {
	std::vector<LineToken> out;
	std::size_t i = 0;
	while (i < line.size()) {
		const char c = line[i];
		if (c == ' ' || c == '\t' || c == '\r') {
			++i;
		} else if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
			break;
		} else if (c == '{' || c == '}') {
			out.push_back({std::string(1, c), i, i + 1});
			++i;
		} else if (c == '"') {
			const std::size_t start = i++;
			while (i < line.size() && line[i] != '"') ++i;
			const std::size_t close = std::min(i, line.size());
			out.push_back({std::string(line.substr(start + 1, close - start - 1)), start, std::min(close + 1, line.size())});
			i = close + 1;
		} else {
			const std::size_t start = i;
			while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '"' && line[i] != '{' && line[i] != '}') ++i;
			out.push_back({std::string(line.substr(start, i - start)), start, i});
		}
	}
	return out;
}

/// The text cut into lines; each keeps its line break so joining gives the text back exactly.
std::vector<std::string> splitKeepingBreaks(std::string_view text) {
	std::vector<std::string> lines;
	std::size_t start = 0;
	while (start < text.size()) {
		const std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos) {
			lines.emplace_back(text.substr(start));
			break;
		}
		lines.emplace_back(text.substr(start, end - start + 1));
		start = end + 1;
	}
	return lines;
}

/// The part of a line without its line break.
std::string_view withoutBreak(std::string_view line) {
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
	return line;
}

std::string quoted(std::string_view text) {
	return "\"" + std::string(text) + "\"";
}

/// Unicode code point appended to UTF-8 text.
void appendUtf8(std::string& out, char32_t c) {
	if (c < 0x80) {
		out += static_cast<char>(c);
	} else if (c < 0x800) {
		out += static_cast<char>(0xC0 | (c >> 6));
		out += static_cast<char>(0x80 | (c & 0x3F));
	} else if (c < 0x10000) {
		out += static_cast<char>(0xE0 | (c >> 12));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (c & 0x3F));
	} else {
		out += static_cast<char>(0xF0 | (c >> 18));
		out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (c & 0x3F));
	}
}

std::optional<std::vector<std::byte>> readBytes(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return std::nullopt;
	std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::vector<std::byte> out(chars.size());
	std::transform(chars.begin(), chars.end(), out.begin(), [](char c) { return static_cast<std::byte>(c); });
	return out;
}

/// Writes bytes; makes a ".bak" copy of an existing file first (once) and remembers both.
bool writeWithBackup(const std::filesystem::path& path, const std::vector<std::byte>& bytes, ModMenuSaveResult& result) {
	std::error_code ec;
	if (const auto existing = readBytes(path)) {
		if (*existing == bytes) return true;   // nothing changed: leave the file (and its date) alone
		auto backup = path;
		backup += ".bak";
		if (!std::filesystem::exists(backup, ec)) {
			std::filesystem::copy_file(path, backup, ec);
			if (ec) {
				result.error = "Could not back up " + displayPath(path) + ": " + ec.message();
				return false;
			}
			result.backups.push_back(backup);
		}
	}
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		result.error = "Could not write " + displayPath(path);
		return false;
	}
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!out) {
		result.error = "Could not write " + displayPath(path);
		return false;
	}
	result.written.push_back(path);
	return true;
}

std::vector<std::byte> utf8Bytes(std::string_view text) {
	std::vector<std::byte> out(text.size());
	std::transform(text.begin(), text.end(), out.begin(), [](char c) { return static_cast<std::byte>(c); });
	return out;
}

/// Text of a file in the mod folder, or (when `fs` is given) of the game's file with that path.
std::optional<std::string> readModOrGame(const std::filesystem::path& modDir, std::string_view relative, const GameFileSystem* fs) {
	if (const auto file = resolveI(modDir, relative)) {
		if (const auto bytes = readBytes(*file)) return decodeTextFile(*bytes);
	}
	if (fs) {
		if (const auto bytes = fs->read(relative)) return decodeTextFile(*bytes);
	}
	return std::nullopt;
}

bool truthy(std::string_view value) {
	const auto v = trim(value);
	return !v.empty() && v != "0";
}

} // namespace

// --- gameinfo.txt ----------------------------------------------------------------------------

const std::vector<std::string>& gameInfoDisplayKeys() {
	static const std::vector<std::string> keys{"game", "title", "title2", "gamelogo", "icon", "type", "developer", "developer_url", "manual"};
	return keys;
}

namespace {

/// Where a key sits in gameinfo.txt: the line index and the token of its value (if any).
struct GameInfoKeyPlace {
	std::size_t line = 0;
	std::optional<LineToken> value;
};

/// Walks the lines of gameinfo.txt and finds `key` directly inside the first top-level block.
/// Also reports the line that opens that block (for adding keys) and a typical indentation.
std::optional<GameInfoKeyPlace> findGameInfoKey(const std::vector<std::string>& lines, std::string_view key, std::size_t* openLine, std::string* indent) {
	int depth = 0;
	bool opened = false;
	for (std::size_t i = 0; i < lines.size(); ++i) {
		const auto tokens = lineTokens(withoutBreak(lines[i]));
		// A key/value line at depth 1 (inside GameInfo, outside FileSystem).
		if (depth == 1 && tokens.size() >= 1 && tokens[0].text != "{" && tokens[0].text != "}") {
			if (indent && indent->empty()) {
				const auto line = withoutBreak(lines[i]);
				*indent = std::string(line.substr(0, line.find_first_not_of(" \t")));
			}
			if (iequals(tokens[0].text, key) && (tokens.size() < 2 || (tokens[1].text != "{" && tokens[1].text != "}"))) {
				GameInfoKeyPlace place;
				place.line = i;
				if (tokens.size() >= 2) place.value = tokens[1];
				return place;
			}
		}
		for (const auto& token : tokens) {
			if (token.text == "{") {
				++depth;
				if (!opened && depth == 1) {
					opened = true;
					if (openLine) *openLine = i;
				}
			} else if (token.text == "}") {
				--depth;
			}
		}
	}
	return std::nullopt;
}

} // namespace

std::string gameInfoValue(std::string_view gameinfoText, std::string_view key) {
	const auto lines = splitKeepingBreaks(gameinfoText);
	const auto place = findGameInfoKey(lines, key, nullptr, nullptr);
	return place && place->value ? place->value->text : std::string();
}

std::string setGameInfoValue(std::string_view gameinfoText, std::string_view key, std::string_view value) {
	auto lines = splitKeepingBreaks(gameinfoText);
	std::size_t openLine = std::string::npos;
	std::string indent;
	const auto place = findGameInfoKey(lines, key, &openLine, &indent);
	const bool crlf = gameinfoText.find("\r\n") != std::string_view::npos;
	const std::string lineBreak = crlf ? "\r\n" : "\n";
	if (place) {
		if (value.empty()) {
			lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(place->line));
		} else {
			auto& line = lines[place->line];
			if (place->value) {
				line.replace(place->value->begin, place->value->end - place->value->begin, quoted(value));
			} else {
				const auto body = withoutBreak(line);
				line = std::string(body) + "\t" + quoted(value) + line.substr(body.size());
			}
		}
	} else if (!value.empty() && openLine != std::string::npos) {
		if (indent.empty()) indent = "\t";
		lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(openLine) + 1, indent + std::string(key) + "\t\t" + quoted(value) + lineBreak);
	}
	std::string out;
	for (const auto& line : lines) out += line;
	return out;
}

// --- Text encodings --------------------------------------------------------------------------

std::string decodeTextFile(const std::vector<std::byte>& bytes) {
	const auto at = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
	bool utf16 = false, bigEndian = false;
	std::size_t start = 0;
	if (bytes.size() >= 2 && at(0) == 0xFF && at(1) == 0xFE) {
		utf16 = true;
		start = 2;
	} else if (bytes.size() >= 2 && at(0) == 0xFE && at(1) == 0xFF) {
		utf16 = bigEndian = true;
		start = 2;
	} else if (bytes.size() >= 4 && at(1) == 0 && at(3) == 0 && at(0) != 0) {
		utf16 = true;   // LE without BOM ("l\0a\0n\0g\0")
	}
	if (!utf16) {
		std::size_t skip = (bytes.size() >= 3 && at(0) == 0xEF && at(1) == 0xBB && at(2) == 0xBF) ? 3 : 0;
		return std::string(reinterpret_cast<const char*>(bytes.data()) + skip, bytes.size() - skip);
	}
	std::string out;
	out.reserve(bytes.size() / 2);
	for (std::size_t i = start; i + 1 < bytes.size(); i += 2) {
		char32_t unit = bigEndian ? (at(i) << 8 | at(i + 1)) : (at(i + 1) << 8 | at(i));
		if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < bytes.size()) {
			const char32_t low = bigEndian ? (at(i + 2) << 8 | at(i + 3)) : (at(i + 3) << 8 | at(i + 2));
			if (low >= 0xDC00 && low <= 0xDFFF) {
				unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
				i += 2;
			}
		}
		appendUtf8(out, unit);
	}
	return out;
}

std::vector<std::byte> encodeUtf16Le(std::string_view utf8) {
	std::vector<std::byte> out{std::byte{0xFF}, std::byte{0xFE}};
	const auto put = [&](char16_t unit) {
		out.push_back(static_cast<std::byte>(unit & 0xFF));
		out.push_back(static_cast<std::byte>(unit >> 8));
	};
	for (std::size_t i = 0; i < utf8.size();) {
		const auto c = static_cast<unsigned char>(utf8[i]);
		char32_t code = c;
		std::size_t length = 1;
		if (c >= 0xF0 && i + 3 < utf8.size()) {
			code = ((c & 0x07) << 18) | ((utf8[i + 1] & 0x3F) << 12) | ((utf8[i + 2] & 0x3F) << 6) | (utf8[i + 3] & 0x3F);
			length = 4;
		} else if (c >= 0xE0 && i + 2 < utf8.size()) {
			code = ((c & 0x0F) << 12) | ((utf8[i + 1] & 0x3F) << 6) | (utf8[i + 2] & 0x3F);
			length = 3;
		} else if (c >= 0xC0 && i + 1 < utf8.size()) {
			code = ((c & 0x1F) << 6) | (utf8[i + 1] & 0x3F);
			length = 2;
		}
		if (code >= 0x10000) {
			code -= 0x10000;
			put(static_cast<char16_t>(0xD800 + (code >> 10)));
			put(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
		} else {
			put(static_cast<char16_t>(code));
		}
		i += length;
	}
	return out;
}

// --- Localization ----------------------------------------------------------------------------

std::string Localization::get(std::string_view token) const {
	if (token.starts_with('#')) token.remove_prefix(1);
	for (const auto& [key, text] : tokens) {
		if (iequals(key, token)) return text;
	}
	return {};
}

void Localization::set(std::string_view token, std::string text) {
	if (token.starts_with('#')) token.remove_prefix(1);
	const auto it = std::find_if(tokens.begin(), tokens.end(), [&](const auto& entry) { return iequals(entry.first, token); });
	if (text.empty()) {
		if (it != tokens.end()) tokens.erase(it);
	} else if (it != tokens.end()) {
		it->second = std::move(text);
	} else {
		tokens.emplace_back(std::string(token), std::move(text));
	}
}

namespace {

/// Tokenizer for localization files: quoted strings with escapes, plain words, braces,
/// // comments and [$PLATFORM] conditions (which are skipped).
class LocalizationReader {
public:
	explicit LocalizationReader(std::string_view text) : m_text(text) {}

	/// Next string or brace; empty optional at the end.
	std::optional<std::string> next() {
		for (;;) {
			while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
			if (m_pos >= m_text.size()) return std::nullopt;
			if (m_text.compare(m_pos, 2, "//") == 0) {
				while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
				continue;
			}
			if (m_text[m_pos] == '[') {   // condition such as [$X360]
				while (m_pos < m_text.size() && m_text[m_pos] != ']') ++m_pos;
				++m_pos;
				continue;
			}
			break;
		}
		const char c = m_text[m_pos];
		if (c == '{' || c == '}') {
			++m_pos;
			return std::string(1, c);
		}
		std::string out;
		if (c == '"') {
			++m_pos;
			while (m_pos < m_text.size() && m_text[m_pos] != '"') {
				if (m_text[m_pos] == '\\' && m_pos + 1 < m_text.size()) {
					const char e = m_text[++m_pos];
					out += e == 'n' ? '\n' : e == 't' ? '\t' : e;   // \" \\ \n \t
				} else {
					out += m_text[m_pos];
				}
				++m_pos;
			}
			++m_pos;
			return out;
		}
		while (m_pos < m_text.size() && !std::isspace(static_cast<unsigned char>(m_text[m_pos])) && m_text[m_pos] != '{' && m_text[m_pos] != '}' && m_text[m_pos] != '"') {
			out += m_text[m_pos++];
		}
		return out;
	}

private:
	std::string_view m_text;
	std::size_t m_pos = 0;
};

std::string escapeLocalization(std::string_view text) {
	std::string out;
	for (const char c : text) {
		if (c == '"') out += "\\\"";
		else if (c == '\\') out += "\\\\";
		else if (c == '\n') out += "\\n";
		else out += c;
	}
	return out;
}

} // namespace

Localization parseLocalization(std::string_view text) {
	Localization out;
	LocalizationReader reader(text);
	// "lang" { "Language" "English" "Tokens" { "key" "text" ... } }
	int depth = 0;
	bool inTokens = false;
	std::optional<std::string> pendingKey;
	while (const auto token = reader.next()) {
		if (*token == "{") {
			++depth;
			if (pendingKey && iequals(*pendingKey, "Tokens")) inTokens = true;
			pendingKey.reset();
			continue;
		}
		if (*token == "}") {
			if (inTokens && depth == 2) inTokens = false;
			--depth;
			pendingKey.reset();
			continue;
		}
		if (!pendingKey) {
			pendingKey = *token;
			continue;
		}
		if (inTokens) {
			out.tokens.emplace_back(*pendingKey, *token);
		} else if (iequals(*pendingKey, "Language")) {
			out.language = *token;
		}
		pendingKey.reset();
	}
	return out;
}

std::string writeLocalization(const Localization& localization) {
	std::string out = "\"lang\"\n{\n\t\"Language\"\t\"" + escapeLocalization(localization.language) + "\"\n\t\"Tokens\"\n\t{\n";
	for (const auto& [key, text] : localization.tokens) {
		out += "\t\t\"" + escapeLocalization(key) + "\"\t\"" + escapeLocalization(text) + "\"\n";
	}
	out += "\t}\n}\n";
	return out;
}

// --- Chapters --------------------------------------------------------------------------------

std::string chapterMap(std::string_view cfgText) {
	for (const auto& line : splitKeepingBreaks(cfgText)) {
		const auto tokens = lineTokens(withoutBreak(line));
		if (tokens.size() >= 2 && iequals(tokens[0].text, "map")) return tokens[1].text;
	}
	return {};
}

std::string setChapterMap(std::string_view cfgText, std::string_view map) {
	auto lines = splitKeepingBreaks(cfgText);
	for (auto& line : lines) {
		const auto tokens = lineTokens(withoutBreak(line));
		if (tokens.size() >= 2 && iequals(tokens[0].text, "map")) {
			line.replace(tokens[1].begin, tokens[1].end - tokens[1].begin, map);
			std::string out;
			for (const auto& l : lines) out += l;
			return out;
		}
	}
	std::string out(cfgText);
	if (!out.empty() && out.back() != '\n') out += '\n';
	return out + "map " + std::string(map) + "\n";
}

std::string chapterTitleToken(std::string_view modFolder, int chapter) {
	std::string folder(modFolder);
	std::transform(folder.begin(), folder.end(), folder.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return folder + "_Chapter" + std::to_string(chapter) + "_Title";
}

std::map<int, std::string> parseChapterBackgrounds(std::string_view text) {
	std::map<int, std::string> out;
	try {
		const auto root = parseKeyValues(text);
		for (const auto& block : root.children) {
			for (const auto& entry : block.children) {
				try {
					out[std::stoi(entry.key)] = entry.value;
				} catch (const std::exception&) {
					// a key that is no number is not a chapter
				}
			}
		}
	} catch (const std::exception&) {
	}
	return out;
}

std::string writeChapterBackgrounds(const std::map<int, std::string>& backgrounds) {
	std::string out = "\"chapters\"\n{\n";
	for (const auto& [chapter, map] : backgrounds) {
		if (!map.empty()) out += "\t" + std::to_string(chapter) + "\t\"" + map + "\"\n";
	}
	return out + "}\n";
}

// --- Main menu entries -----------------------------------------------------------------------

std::vector<GameMenuItem> parseGameMenu(std::string_view text) {
	std::vector<GameMenuItem> out;
	KvNode root;
	try {
		root = parseKeyValues(text);
	} catch (const std::exception&) {
		return out;
	}
	if (root.children.empty()) return out;
	// The entries are numbered blocks; the game shows them in the order of the file.
	for (const auto& block : root.children.front().children) {
		if (!block.block) continue;
		GameMenuItem item;
		for (const auto& value : block.children) {
			if (iequals(value.key, "label")) item.label = value.value;
			else if (iequals(value.key, "command")) item.command = value.value;
			else if (iequals(value.key, "OnlyInGame") && value.condition.empty()) item.onlyInGame = truthy(value.value);
			else if (iequals(value.key, "notmulti") && value.condition.empty()) item.notMulti = truthy(value.value);
			else if (iequals(value.key, "notsingle") && value.condition.empty()) item.notSingle = truthy(value.value);
			else item.extra.push_back(value);
		}
		out.push_back(std::move(item));
	}
	return out;
}

std::string writeGameMenu(const std::vector<GameMenuItem>& items) {
	KvNode root;
	auto menu = KvNode::makeBlock("\"GameMenu\"");
	int number = 1;
	for (const auto& item : items) {
		auto block = KvNode::makeBlock("\"" + std::to_string(number++) + "\"");
		block.children.push_back(KvNode::makeValue("label", item.label));
		block.children.push_back(KvNode::makeValue("command", item.command));
		if (item.onlyInGame) block.children.push_back(KvNode::makeValue("OnlyInGame", "1"));
		if (item.notMulti) block.children.push_back(KvNode::makeValue("notmulti", "1"));
		if (item.notSingle) block.children.push_back(KvNode::makeValue("notsingle", "1"));
		block.children.insert(block.children.end(), item.extra.begin(), item.extra.end());
		menu.children.push_back(std::move(block));
	}
	root.children.push_back(std::move(menu));
	return writeKeyValues(root);
}

std::vector<GameMenuItem> defaultGameMenu() {
	std::vector<GameMenuItem> items;
	items.push_back({"#GameUI_GameMenu_ResumeGame", "ResumeGame", true, false, false, {}});
	items.push_back({"#GameUI_GameMenu_NewGame", "OpenNewGameDialog", false, true, false, {}});
	items.push_back({"#GameUI_GameMenu_LoadGame", "OpenLoadGameDialog", false, true, false, {}});
	items.push_back({"#GameUI_GameMenu_SaveGame", "OpenSaveGameDialog", true, true, false, {}});
	items.push_back({"#GameUI_GameMenu_Options", "OpenOptionsDialog", false, false, false, {}});
	items.push_back({"#GameUI_GameMenu_Quit", "Quit", false, false, false, {}});
	return items;
}

const std::vector<MenuCommand>& commonMenuCommands() {
	static const std::vector<MenuCommand> commands{
		{"ResumeGame", "Back to the game (only while a map is loaded)"},
		{"OpenNewGameDialog", "New Game: pick a chapter"},
		{"OpenLoadGameDialog", "Load a saved game"},
		{"OpenSaveGameDialog", "Save the game"},
		{"OpenOptionsDialog", "Options"},
		{"OpenBonusMapsDialog", "Bonus maps"},
		{"OpenAchievementsDialog", "Achievements"},
		{"OpenCommentaryDialog", "Developer commentary"},
		{"OpenServerBrowser", "Find servers (multiplayer)"},
		{"OpenCreateMultiplayerGameDialog", "Create a server (multiplayer)"},
		{"OpenPlayerListDialog", "Mute players (multiplayer)"},
		{"Disconnect", "Leave the map"},
		{"Quit", "Quit the game"},
		{"engine map ", "Start a map directly (put its name after \"map\")"},
	};
	return commands;
}

std::string builtInMenuLabel(std::string_view token) {
	static const std::vector<std::pair<std::string, std::string>> labels{
		{"#GameUI_GameMenu_ResumeGame", "Resume Game"},
		{"#GameUI_GameMenu_NewGame", "New Game"},
		{"#GameUI_GameMenu_LoadGame", "Load Game"},
		{"#GameUI_GameMenu_SaveGame", "Save Game"},
		{"#GameUI_GameMenu_Options", "Options"},
		{"#GameUI_GameMenu_Quit", "Quit"},
		{"#GameUI_GameMenu_Disconnect", "Disconnect"},
		{"#GameUI_GameMenu_BonusMaps", "Bonus Maps"},
		{"#GameUI_GameMenu_Achievements", "Achievements"},
		{"#GameUI_GameMenu_Commentary", "Commentary"},
		{"#GameUI_GameMenu_PlayerList", "Mute Players"},
		{"#GameUI_GameMenu_FindServers", "Find Servers"},
		{"#GameUI_GameMenu_CreateServer", "Create Server"},
		{"#GameUI_GameMenu_Friends", "Friends"},
	};
	for (const auto& [key, text] : labels) {
		if (iequals(key, token)) return text;
	}
	return {};
}

// --- The whole menu --------------------------------------------------------------------------

ModMenu loadModMenu(const std::filesystem::path& modDir, const GameFileSystem* fs, std::string_view language) {
	ModMenu menu;
	const std::string folder = modDir.filename().string();
	if (const auto text = readModOrGame(modDir, "gameinfo.txt", nullptr)) {
		for (const auto& key : gameInfoDisplayKeys()) {
			if (auto value = gameInfoValue(*text, key); !value.empty()) menu.info[key] = std::move(value);
		}
	}
	const std::string localizationPath = "resource/" + toLower(folder) + "_" + std::string(language) + ".txt";
	if (const auto text = readModOrGame(modDir, localizationPath, nullptr)) {
		menu.localization = parseLocalization(*text);
	} else {
		menu.localization.language = std::string(language);
		if (!menu.localization.language.empty()) menu.localization.language[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(menu.localization.language[0])));
	}
	const auto backgrounds = readModOrGame(modDir, "scripts/ChapterBackgrounds.txt", fs);
	const auto backgroundOf = backgrounds ? parseChapterBackgrounds(*backgrounds) : std::map<int, std::string>{};
	// Chapters 1, 2, 3 ... as long as their configs exist in the mod.
	for (int number = 1; number < 1000; ++number) {
		const auto cfg = readModOrGame(modDir, "cfg/chapter" + std::to_string(number) + ".cfg", nullptr);
		if (!cfg) break;
		ChapterInfo chapter;
		chapter.number = number;
		chapter.map = chapterMap(*cfg);
		chapter.title = menu.localization.get(chapterTitleToken(folder, number));
		if (const auto it = backgroundOf.find(number); it != backgroundOf.end()) chapter.background = it->second;
		menu.chapters.push_back(std::move(chapter));
	}
	if (const auto own = readModOrGame(modDir, "resource/GameMenu.res", nullptr)) {
		menu.items = parseGameMenu(*own);
		menu.menuFromMod = true;
	} else if (const auto game = fs ? fs->readText("resource/GameMenu.res") : std::nullopt) {
		menu.items = parseGameMenu(*game);
	}
	if (menu.items.empty()) menu.items = defaultGameMenu();
	return menu;
}

ModMenuSaveResult saveModMenu(const std::filesystem::path& modDir, const ModMenu& menu, std::string_view language) {
	ModMenuSaveResult result;
	const std::string folder = modDir.filename().string();

	// gameinfo.txt: only the display keys change.
	const auto gameinfoPath = resolveI(modDir, "gameinfo.txt").value_or(modDir / "gameinfo.txt");
	if (const auto bytes = readBytes(gameinfoPath)) {
		std::string text = decodeTextFile(*bytes);
		for (const auto& key : gameInfoDisplayKeys()) {
			const auto it = menu.info.find(key);
			const std::string wanted = it == menu.info.end() ? std::string() : it->second;
			if (gameInfoValue(text, key) != wanted) text = setGameInfoValue(text, key, wanted);
		}
		if (!writeWithBackup(gameinfoPath, utf8Bytes(text), result)) return result;
	} else {
		result.error = "The mod has no gameinfo.txt: " + displayPath(gameinfoPath);
		return result;
	}

	// Chapters: configs 1..n (renumbered in list order), titles, backgrounds.
	Localization localization = menu.localization;
	std::map<int, std::string> backgrounds;
	const auto existingBackgrounds = readModOrGame(modDir, "scripts/ChapterBackgrounds.txt", nullptr);
	int number = 1;
	for (const auto& chapter : menu.chapters) {
		const auto cfgPath = resolveI(modDir, "cfg/chapter" + std::to_string(number) + ".cfg").value_or(modDir / "cfg" / ("chapter" + std::to_string(number) + ".cfg"));
		const std::string old = readModOrGame(modDir, "cfg/chapter" + std::to_string(number) + ".cfg", nullptr).value_or(std::string());
		if (!writeWithBackup(cfgPath, utf8Bytes(setChapterMap(old, chapter.map)), result)) return result;
		localization.set(chapterTitleToken(folder, number), chapter.title);
		if (!chapter.background.empty()) backgrounds[number] = chapter.background;
		++number;
	}
	// Chapter configs past the last chapter would still show in New Game: they go (with a backup).
	for (int extra = number; extra < 1000; ++extra) {
		const auto stale = resolveI(modDir, "cfg/chapter" + std::to_string(extra) + ".cfg");
		if (!stale) break;
		auto backup = *stale;
		backup += ".bak";
		std::error_code ec;
		if (!std::filesystem::exists(backup, ec)) {
			std::filesystem::copy_file(*stale, backup, ec);
			if (!ec) result.backups.push_back(backup);
		}
		std::filesystem::remove(*stale, ec);
		localization.set(chapterTitleToken(folder, extra), {});
	}
	if (!localization.tokens.empty() || resolveI(modDir, "resource/" + toLower(folder) + "_" + std::string(language) + ".txt")) {
		const auto path = resolveI(modDir, "resource/" + toLower(folder) + "_" + std::string(language) + ".txt")
		                      .value_or(modDir / "resource" / (toLower(folder) + "_" + std::string(language) + ".txt"));
		if (!writeWithBackup(path, encodeUtf16Le(writeLocalization(localization)), result)) return result;
	}
	if (!backgrounds.empty() || existingBackgrounds) {
		const auto path = resolveI(modDir, "scripts/ChapterBackgrounds.txt").value_or(modDir / "scripts" / "ChapterBackgrounds.txt");
		if (!writeWithBackup(path, utf8Bytes(writeChapterBackgrounds(backgrounds)), result)) return result;
	}

	// The menu entries.
	const auto menuPath = resolveI(modDir, "resource/GameMenu.res").value_or(modDir / "resource" / "GameMenu.res");
	writeWithBackup(menuPath, utf8Bytes(writeGameMenu(menu.items)), result);
	return result;
}

} // namespace sme
