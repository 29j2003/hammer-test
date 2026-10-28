// localization.h - The mod's text files ("Texts" on the Project page):
//   - resource/<name>_<language>.txt: VGUI localization files ("lang" { "Language" "English"
//     "Tokens" { "Token" "Text" ... } }), UTF-16 LE with BOM as the game writes them;
//   - resource/closecaption_<language>.txt: captions and subtitles, the same format, compiled
//     into closecaption_<language>.dat (VCCD) for the game - done here, no captioncompiler needed.
//
// Unlike the small reader in mod_menu.h this keeps a file as it was: comments stay with the
// token below them, [$X360]-style conditions stay on their token, other entries of the "lang"
// block stay as text, the order stays. Only edited tokens change when a file is written back.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// One token of a localization file.
struct LocalizedToken {
	std::string token;                  ///< Without '#'
	std::string text;                   ///< Escapes resolved (\n is a line break, \" a quote)
	std::string condition;              ///< "[$X360]" and the like, kept as written; empty if none
	std::vector<std::string> comments;  ///< "// ..." lines right above the token
};

/// A whole localization file (see the file comment).
struct LocalizationFile {
	std::vector<std::string> headerComments;   ///< Comments above the root block
	std::string rootKey = "lang";
	std::string language = "English";
	std::vector<LocalizedToken> tokens;
	std::vector<std::string> extra;            ///< Other entries of the root block, as text
	std::vector<std::string> trailingComments; ///< Comments after the last token
	bool broken = false;                       ///< The text ended early (what was read is kept)

	/// Index of a token (case-insensitive, '#' allowed), or -1.
	[[nodiscard]] int find(std::string_view token) const;
	/// Text of a token; empty if absent.
	[[nodiscard]] std::string get(std::string_view token) const;
	/// Sets a token's text (keeps its place; new tokens go to the end).
	void set(std::string_view token, std::string text);
	/// Removes a token (its comments go with it). False if it was not there.
	bool remove(std::string_view token);
	/// Renames a token, keeping text and place. False if `from` is missing or `to` exists.
	bool rename(std::string_view from, std::string_view to);
};

/// Reads a localization file (text already decoded to UTF-8, see decodeTextFile).
[[nodiscard]] LocalizationFile parseLocalizationFile(std::string_view text);
/// Writes it back as UTF-8 text with tabs like Valve's files (encode with encodeUtf16Le to save).
[[nodiscard]] std::string writeLocalizationFile(const LocalizationFile& file);

/// Reads a file from disk in any encoding the game accepts; nullopt if it cannot be read.
[[nodiscard]] std::optional<LocalizationFile> loadLocalizationFile(const std::filesystem::path& path);
/// Writes a file as UTF-16 LE with BOM (creating the folder). The first time an existing file
/// is replaced a copy "<name>.bak" is kept. False with `error` filled when writing failed.
bool saveLocalizationFile(const std::filesystem::path& path, const LocalizationFile& file, std::string* error = nullptr);

// --- Files of a mod --------------------------------------------------------------------------

/// Language names the game uses in file names ("english", "german", "schinese" ...).
[[nodiscard]] const std::vector<std::string>& sourceLanguages();
/// The display name the game writes into "Language" ("english" -> "English").
[[nodiscard]] std::string languageDisplayName(std::string_view language);

/// One set of localization files that belong together, e.g. mymod_english.txt + mymod_german.txt.
struct LocalizationSet {
	std::string name;                                        ///< "mymod", "closecaption", "subtitles" ...
	std::map<std::string, std::filesystem::path> languages;  ///< lowercase language -> file
	[[nodiscard]] bool captions() const { return name == "closecaption" || name == "subtitles"; }
};

/// The localization files in the mod's resource folder, grouped by name (sorted, captions last).
[[nodiscard]] std::vector<LocalizationSet> findLocalizationSets(const std::filesystem::path& modDir);
/// Path of a set's file for a language: "<modDir>/resource/<name>_<language>.txt".
[[nodiscard]] std::filesystem::path localizationPath(const std::filesystem::path& modDir, std::string_view name, std::string_view language);

/// What the mod's menu uses a token for ("Title of chapter 3"), from the mod folder name; empty
/// for other tokens. Chapter titles are "<MODFOLDER>_Chapter<N>_Title" (see chapterTitleToken).
[[nodiscard]] std::string menuUseOfToken(std::string_view token, std::string_view modFolder);

// --- Captions --------------------------------------------------------------------------------

/// A piece of a caption line with the look the tags before it gave it.
struct CaptionRun {
	std::string text;
	std::uint8_t r = 255, g = 255, b = 255;
	bool bold = false;
	bool italic = false;
	bool lineBreak = false;   ///< <cr>: a new line starts before this piece
};

/// A caption line split by its tags: <clr:r,g,b>, <playerclr:...> (its first colour), <B>, <I>,
/// <cr>; <sfx>, <len:N>, <delay:N>, <norepeat:N>, <sameline>, <linger> only set the flags below.
struct CaptionLine {
	std::vector<CaptionRun> runs;
	bool soundEffect = false;      ///< <sfx>: only shown with closed captions, not with subtitles
	std::optional<double> length;  ///< <len:N> seconds the line stays
	std::optional<double> delay;   ///< <delay:N>
	std::vector<std::string> unknownTags;
};
[[nodiscard]] CaptionLine parseCaption(std::string_view text);

/// Valve's CRC32 (the usual polynomial) of a caption token in lower case, as the .dat uses it.
[[nodiscard]] std::uint32_t captionHash(std::string_view token);

/// The compiled caption file (closecaption_<language>.dat, "VCCD" version 1): a header, a
/// directory sorted by token hash (hash, block, offset, length in bytes), then 8 KiB blocks of
/// UTF-16 LE strings with their terminating zero. Tokens that would not fit a block are left
/// out and named in `skipped`.
struct CompiledCaptions {
	std::vector<std::byte> bytes;
	int count = 0;
	std::vector<std::string> skipped;
};
[[nodiscard]] CompiledCaptions compileCaptions(const LocalizationFile& captions);
/// Compiles `<name>_<language>.txt` into the .dat next to it. False with `error` on failure.
bool compileCaptionFile(const std::filesystem::path& txtPath, std::string* error = nullptr, int* count = nullptr);

} // namespace sme
