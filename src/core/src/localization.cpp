// localization.cpp - Localization and caption files of a mod (see localization.h).

#include <sme/localization.h>

#include <sme/mod_menu.h>
#include <sme/text.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace sme {

namespace {

// --- Reading -----------------------------------------------------------------------------------

/// One piece of a localization file: a string (quoted or plain), a brace, a // comment or a
/// [$CONDITION]. `begin`/`end` are offsets into the text (for keeping unknown entries as written).
struct Piece {
	enum class Kind { String, Open, Close, Comment, Condition } kind;
	std::string text;
	std::size_t begin = 0, end = 0;
};

std::vector<Piece> split(std::string_view text) {
	std::vector<Piece> out;
	std::size_t pos = 0;
	while (pos < text.size()) {
		const char c = text[pos];
		if (std::isspace(static_cast<unsigned char>(c))) {
			++pos;
			continue;
		}
		const std::size_t begin = pos;
		if (text.compare(pos, 2, "//") == 0) {
			while (pos < text.size() && text[pos] != '\n') ++pos;
			std::string comment(text.substr(begin, pos - begin));
			while (!comment.empty() && (comment.back() == '\r' || comment.back() == ' ' || comment.back() == '\t')) comment.pop_back();
			out.push_back({Piece::Kind::Comment, std::move(comment), begin, pos});
			continue;
		}
		if (c == '{' || c == '}') {
			out.push_back({c == '{' ? Piece::Kind::Open : Piece::Kind::Close, std::string(1, c), begin, ++pos});
			continue;
		}
		if (c == '[') {
			while (pos < text.size() && text[pos] != ']' && text[pos] != '\n') ++pos;
			if (pos < text.size() && text[pos] == ']') ++pos;
			out.push_back({Piece::Kind::Condition, std::string(text.substr(begin, pos - begin)), begin, pos});
			continue;
		}
		std::string value;
		if (c == '"') {
			++pos;
			while (pos < text.size() && text[pos] != '"') {
				if (text[pos] == '\\' && pos + 1 < text.size()) {
					const char e = text[++pos];
					value += e == 'n' ? '\n' : e == 't' ? '\t' : e;   // \" \\ \n \t
				} else {
					value += text[pos];
				}
				++pos;
			}
			if (pos < text.size()) ++pos;
		} else {
			while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos])) && text[pos] != '{' && text[pos] != '}' && text[pos] != '"') {
				value += text[pos++];
			}
		}
		out.push_back({Piece::Kind::String, std::move(value), begin, pos});
	}
	return out;
}

std::string escape(std::string_view text) {
	std::string out;
	out.reserve(text.size());
	for (const char c : text) {
		if (c == '"') out += "\\\"";
		else if (c == '\\') out += "\\\\";
		else if (c == '\n') out += "\\n";
		else if (c == '\t') out += "\\t";
		else if (c != '\r') out += c;
	}
	return out;
}

std::string stripHash(std::string_view token) {
	if (token.starts_with('#')) token.remove_prefix(1);
	return std::string(token);
}

std::optional<std::vector<std::byte>> readBytes(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return std::nullopt;
	std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::vector<std::byte> out(chars.size());
	std::transform(chars.begin(), chars.end(), out.begin(), [](char c) { return static_cast<std::byte>(c); });
	return out;
}

bool writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes, std::string* error) {
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (out) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!out) {
		if (error) *error = "Could not write " + displayPath(path);
		return false;
	}
	return true;
}

/// UTF-8 to UTF-16 code units (no BOM, no terminator).
std::vector<char16_t> utf16(std::string_view utf8) {
	const auto bytes = encodeUtf16Le(utf8);   // with BOM
	std::vector<char16_t> out;
	out.reserve(bytes.size() / 2);
	for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
		out.push_back(static_cast<char16_t>(static_cast<unsigned>(bytes[i]) | (static_cast<unsigned>(bytes[i + 1]) << 8)));
	}
	return out;
}

void put32(std::vector<std::byte>& out, std::uint32_t value) {
	for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
}

void put16(std::vector<std::byte>& out, std::uint16_t value) {
	out.push_back(static_cast<std::byte>(value & 0xFF));
	out.push_back(static_cast<std::byte>(value >> 8));
}

} // namespace

// --- LocalizationFile ----------------------------------------------------------------------------

int LocalizationFile::find(std::string_view token) const {
	if (token.starts_with('#')) token.remove_prefix(1);
	for (std::size_t i = 0; i < tokens.size(); ++i) {
		if (iequals(tokens[i].token, token)) return static_cast<int>(i);
	}
	return -1;
}

std::string LocalizationFile::get(std::string_view token) const {
	const int index = find(token);
	return index < 0 ? std::string() : tokens[static_cast<std::size_t>(index)].text;
}

void LocalizationFile::set(std::string_view token, std::string text) {
	const int index = find(token);
	if (index >= 0) {
		tokens[static_cast<std::size_t>(index)].text = std::move(text);
	} else {
		tokens.push_back({stripHash(token), std::move(text), {}, {}});
	}
}

bool LocalizationFile::remove(std::string_view token) {
	const int index = find(token);
	if (index < 0) return false;
	tokens.erase(tokens.begin() + index);
	return true;
}

bool LocalizationFile::rename(std::string_view from, std::string_view to) {
	const int index = find(from);
	const std::string name = stripHash(to);
	if (index < 0 || name.empty()) return false;
	const int existing = find(name);
	if (existing >= 0 && existing != index) return false;
	tokens[static_cast<std::size_t>(index)].token = name;
	return true;
}

LocalizationFile parseLocalizationFile(std::string_view text) {
	LocalizationFile file;
	const auto pieces = split(text);
	std::size_t i = 0;
	const auto at = [&](std::size_t n) -> const Piece* { return n < pieces.size() ? &pieces[n] : nullptr; };
	while (at(i) && at(i)->kind == Piece::Kind::Comment) file.headerComments.push_back(pieces[i++].text);
	if (!at(i) || at(i)->kind != Piece::Kind::String || !at(i + 1) || at(i + 1)->kind != Piece::Kind::Open) {
		file.broken = !pieces.empty();
		return file;
	}
	file.rootKey = pieces[i].text;
	i += 2;
	// Skips a block that starts at `open` (a '{'); returns the index after its '}'.
	const auto skipBlock = [&](std::size_t open) {
		int depth = 0;
		for (std::size_t n = open; n < pieces.size(); ++n) {
			if (pieces[n].kind == Piece::Kind::Open) ++depth;
			if (pieces[n].kind == Piece::Kind::Close && --depth == 0) return n + 1;
		}
		return pieces.size();
	};
	for (;;) {
		const Piece* piece = at(i);
		if (!piece) {
			file.broken = true;
			return file;
		}
		if (piece->kind == Piece::Kind::Close) break;
		if (piece->kind == Piece::Kind::Comment || piece->kind == Piece::Kind::Condition) {
			file.extra.push_back(piece->text);
			++i;
			continue;
		}
		if (piece->kind == Piece::Kind::Open) {   // a block without a name: keep it as written
			const std::size_t end = skipBlock(i);
			file.extra.emplace_back(text.substr(piece->begin, pieces[end - 1].end - piece->begin));
			i = end;
			continue;
		}
		const std::string key = piece->text;
		if (iequals(key, "Tokens") && at(i + 1) && at(i + 1)->kind == Piece::Kind::Open) {
			i += 2;
			std::vector<std::string> comments;
			for (;;) {
				const Piece* entry = at(i);
				if (!entry) {
					file.trailingComments = std::move(comments);
					file.broken = true;
					return file;
				}
				if (entry->kind == Piece::Kind::Close) {
					file.trailingComments = std::move(comments);
					++i;
					break;
				}
				if (entry->kind == Piece::Kind::Comment) {
					comments.push_back(entry->text);
					++i;
					continue;
				}
				if (entry->kind == Piece::Kind::Open) {   // stray block inside Tokens: drop it
					i = skipBlock(i);
					continue;
				}
				if (entry->kind != Piece::Kind::String || !at(i + 1) || at(i + 1)->kind != Piece::Kind::String) {
					++i;   // a key without its text, a lone condition ...
					continue;
				}
				LocalizedToken token{entry->text, pieces[i + 1].text, {}, std::move(comments)};
				comments.clear();
				i += 2;
				if (at(i) && at(i)->kind == Piece::Kind::Condition) token.condition = pieces[i++].text;
				file.tokens.push_back(std::move(token));
			}
			continue;
		}
		if (at(i + 1) && at(i + 1)->kind == Piece::Kind::Open) {
			const std::size_t end = skipBlock(i + 1);
			file.extra.emplace_back(text.substr(piece->begin, pieces[end - 1].end - piece->begin));
			i = end;
			continue;
		}
		if (at(i + 1) && at(i + 1)->kind == Piece::Kind::String) {
			if (iequals(key, "Language")) {
				file.language = pieces[i + 1].text;
				i += 2;
				if (at(i) && at(i)->kind == Piece::Kind::Condition) ++i;
			} else {
				std::size_t last = i + 1;
				if (at(i + 2) && at(i + 2)->kind == Piece::Kind::Condition) ++last;
				file.extra.emplace_back(text.substr(piece->begin, pieces[last].end - piece->begin));
				i = last + 1;
			}
			continue;
		}
		++i;
	}
	return file;
}

std::string writeLocalizationFile(const LocalizationFile& file) {
	std::string out;
	for (const auto& comment : file.headerComments) out += comment + "\n";
	out += "\"" + escape(file.rootKey) + "\"\n{\n";
	out += "\t\"Language\"\t\"" + escape(file.language) + "\"\n";
	for (const auto& entry : file.extra) out += "\t" + entry + "\n";
	out += "\t\"Tokens\"\n\t{\n";
	for (const auto& token : file.tokens) {
		for (const auto& comment : token.comments) out += "\t\t" + comment + "\n";
		out += "\t\t\"" + escape(token.token) + "\"\t\"" + escape(token.text) + "\"";
		if (!token.condition.empty()) out += "\t" + token.condition;
		out += "\n";
	}
	for (const auto& comment : file.trailingComments) out += "\t\t" + comment + "\n";
	out += "\t}\n}\n";
	return out;
}

std::optional<LocalizationFile> loadLocalizationFile(const std::filesystem::path& path) {
	const auto bytes = readBytes(path);
	if (!bytes) return std::nullopt;
	return parseLocalizationFile(decodeTextFile(*bytes));
}

bool saveLocalizationFile(const std::filesystem::path& path, const LocalizationFile& file, std::string* error) {
	const auto bytes = encodeUtf16Le(writeLocalizationFile(file));
	std::error_code ec;
	if (const auto existing = readBytes(path)) {
		if (*existing == bytes) return true;   // unchanged: leave the file and its date alone
		auto backup = path;
		backup += ".bak";
		if (!std::filesystem::exists(backup, ec)) std::filesystem::copy_file(path, backup, ec);
	}
	return writeBytes(path, bytes, error);
}

// --- Files of a mod ------------------------------------------------------------------------------

const std::vector<std::string>& sourceLanguages() {
	static const std::vector<std::string> LANGUAGES = {
		"english", "brazilian", "bulgarian", "czech", "danish", "dutch", "finnish", "french", "german", "greek", "hungarian",
		"italian", "japanese", "koreana", "korean", "latam", "norwegian", "polish", "portuguese", "romanian", "russian",
		"schinese", "spanish", "swedish", "tchinese", "thai", "turkish", "ukrainian", "vietnamese",
	};
	return LANGUAGES;
}

std::string languageDisplayName(std::string_view language) {
	std::string out = toLower(language);
	if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
	return out;
}

std::vector<LocalizationSet> findLocalizationSets(const std::filesystem::path& modDir) {
	std::map<std::string, LocalizationSet> sets;
	const auto resource = findChildI(modDir, "resource");
	std::error_code ec;
	if (resource && std::filesystem::is_directory(*resource, ec)) {
		for (const auto& entry : std::filesystem::directory_iterator(*resource, ec)) {
			if (!entry.is_regular_file(ec)) continue;
			const std::string name = toLower(entry.path().filename().string());
			if (!name.ends_with(".txt")) continue;
			const std::string stem = name.substr(0, name.size() - 4);
			for (const auto& language : sourceLanguages()) {
				const std::string suffix = "_" + language;
				if (stem.size() > suffix.size() && stem.ends_with(suffix)) {
					const std::string base = stem.substr(0, stem.size() - suffix.size());
					auto& set = sets[base];
					set.name = base;
					set.languages[language] = entry.path();
					break;
				}
			}
		}
	}
	std::vector<LocalizationSet> out;
	for (auto& [name, set] : sets) out.push_back(std::move(set));
	std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return !a.captions() && b.captions(); });
	return out;
}

std::filesystem::path localizationPath(const std::filesystem::path& modDir, std::string_view name, std::string_view language) {
	const auto resource = findChildI(modDir, "resource").value_or(modDir / "resource");
	const std::string file = toLower(name) + "_" + toLower(language) + ".txt";
	return findChildI(resource, file).value_or(resource / file);
}

std::string menuUseOfToken(std::string_view token, std::string_view modFolder) {
	if (token.starts_with('#')) token.remove_prefix(1);
	const std::string prefix = std::string(modFolder) + "_Chapter";
	if (modFolder.empty() || !startsWithI(token, prefix) || !endsWithI(token, "_Title")) return {};
	const std::string_view number = token.substr(prefix.size(), token.size() - prefix.size() - 6);
	if (number.empty() || !std::all_of(number.begin(), number.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) return {};
	return "Title of chapter " + std::string(number) + " (Menu)";
}

// --- Captions ------------------------------------------------------------------------------------

CaptionLine parseCaption(std::string_view text) {
	CaptionLine line;
	CaptionRun run;
	const auto flush = [&] {
		if (!run.text.empty()) {
			line.runs.push_back(run);
			run.text.clear();
			run.lineBreak = false;
		}
	};
	const auto color = [](std::string_view args, CaptionRun& target) {
		const auto parts = splitTrimmed(args.substr(0, args.find(':')), ',');
		if (parts.size() != 3) return;
		target.r = static_cast<std::uint8_t>(std::clamp(std::atoi(parts[0].c_str()), 0, 255));
		target.g = static_cast<std::uint8_t>(std::clamp(std::atoi(parts[1].c_str()), 0, 255));
		target.b = static_cast<std::uint8_t>(std::clamp(std::atoi(parts[2].c_str()), 0, 255));
	};
	for (std::size_t pos = 0; pos < text.size();) {
		if (text[pos] == '<') {
			const std::size_t close = text.find('>', pos);
			if (close != std::string_view::npos) {
				const std::string_view tag = text.substr(pos + 1, close - pos - 1);
				const std::size_t colon = tag.find(':');
				const std::string name = toLower(tag.substr(0, colon));
				const std::string_view args = colon == std::string_view::npos ? std::string_view() : tag.substr(colon + 1);
				pos = close + 1;
				if (name == "clr" || name == "playerclr") {
					flush();
					color(args, run);
				} else if (name == "b") {
					flush();
					run.bold = !run.bold;
				} else if (name == "i") {
					flush();
					run.italic = !run.italic;
				} else if (name == "cr") {
					flush();
					run.lineBreak = true;
				} else if (name == "sfx") {
					line.soundEffect = true;
				} else if (name == "len") {
					line.length = std::atof(std::string(args).c_str());
				} else if (name == "delay") {
					line.delay = std::atof(std::string(args).c_str());
				} else if (name != "norepeat" && name != "sameline" && name != "linger") {
					line.unknownTags.emplace_back(tag);
				}
				continue;
			}
		}
		run.text += text[pos++];
	}
	flush();
	return line;
}

std::uint32_t captionHash(std::string_view token) {
	static const auto TABLE = [] {
		std::array<std::uint32_t, 256> table{};
		for (std::uint32_t i = 0; i < 256; ++i) {
			std::uint32_t c = i;
			for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		return table;
	}();
	std::uint32_t crc = 0xFFFFFFFFu;
	for (const char c : token) {
		const auto byte = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(c)));
		crc = TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFFu;
}

CompiledCaptions compileCaptions(const LocalizationFile& captions) {
	constexpr std::uint32_t BLOCK_SIZE = 8192;
	struct Entry {
		std::uint32_t hash;
		std::uint32_t block;
		std::uint16_t offset;
		std::uint16_t length;
	};
	CompiledCaptions out;
	std::vector<Entry> directory;
	std::vector<std::vector<std::byte>> blocks;
	std::vector<std::uint32_t> seen;
	for (const auto& token : captions.tokens) {
		const std::uint32_t hash = captionHash(token.token);
		if (std::find(seen.begin(), seen.end(), hash) != seen.end()) {
			out.skipped.push_back(token.token);   // the same token twice (or a hash clash): the first one counts
			continue;
		}
		auto units = utf16(token.text);
		units.push_back(0);
		const std::size_t length = units.size() * 2;
		if (length > BLOCK_SIZE) {
			out.skipped.push_back(token.token);
			continue;
		}
		if (blocks.empty() || blocks.back().size() + length > BLOCK_SIZE) blocks.emplace_back();
		auto& block = blocks.back();
		directory.push_back({hash, static_cast<std::uint32_t>(blocks.size() - 1), static_cast<std::uint16_t>(block.size()), static_cast<std::uint16_t>(length)});
		for (const char16_t unit : units) put16(block, static_cast<std::uint16_t>(unit));
		seen.push_back(hash);
	}
	// The game looks tokens up by binary search over the hashes.
	std::sort(directory.begin(), directory.end(), [](const Entry& a, const Entry& b) { return a.hash < b.hash; });
	const std::uint32_t headerSize = 24 + 12 * static_cast<std::uint32_t>(directory.size());
	const std::uint32_t dataOffset = (headerSize + 511) / 512 * 512;
	auto& bytes = out.bytes;
	for (const char c : {'V', 'C', 'C', 'D'}) bytes.push_back(static_cast<std::byte>(c));
	put32(bytes, 1);   // version
	put32(bytes, static_cast<std::uint32_t>(blocks.size()));
	put32(bytes, BLOCK_SIZE);
	put32(bytes, static_cast<std::uint32_t>(directory.size()));
	put32(bytes, dataOffset);
	for (const auto& entry : directory) {
		put32(bytes, entry.hash);
		put32(bytes, entry.block);
		put16(bytes, entry.offset);
		put16(bytes, entry.length);
	}
	bytes.resize(dataOffset, std::byte{0});
	for (auto& block : blocks) {
		block.resize(BLOCK_SIZE, std::byte{0});
		bytes.insert(bytes.end(), block.begin(), block.end());
	}
	out.count = static_cast<int>(directory.size());
	return out;
}

bool compileCaptionFile(const std::filesystem::path& txtPath, std::string* error, int* count) {
	const auto file = loadLocalizationFile(txtPath);
	if (!file) {
		if (error) *error = "Could not read " + displayPath(txtPath);
		return false;
	}
	const auto compiled = compileCaptions(*file);
	auto datPath = txtPath;
	datPath.replace_extension(".dat");
	if (!writeBytes(datPath, compiled.bytes, error)) return false;
	if (count) *count = compiled.count;
	return true;
}

} // namespace sme
