// mod_menu.h - What a Source mod shows in its main menu, read from and written back to the
// mod's own files:
//   - gameinfo.txt:            title, second title line, logo switch, window icon, developer ...
//   - cfg/chapterN.cfg:        the map each chapter of "New Game" starts ("map <name>")
//   - resource/<mod>_<lang>.txt: chapter titles (VGUI localization file, UTF-16 with BOM)
//   - scripts/ChapterBackgrounds.txt: the background map shown behind the menu per chapter
//   - resource/GameMenu.res:   the menu entries (label, command, when they show)
//
// Everything here is plain text work without Qt, so it can be tested on its own. Editing keeps
// what the editor does not know: other gameinfo keys and comments stay byte for byte, other
// localization tokens and unknown menu keys stay as they were.

#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/keyvalues.h>

namespace sme {

class GameFileSystem;

// --- gameinfo.txt --------------------------------------------------------------------------

/// The keys of gameinfo.txt's GameInfo block that change how the mod looks in the menu and in
/// Steam: game, title, title2, gamelogo, icon, type, developer, developer_url, manual.
[[nodiscard]] const std::vector<std::string>& gameInfoDisplayKeys();

/// Value of a key directly inside the GameInfo block (not inside FileSystem); empty if absent.
[[nodiscard]] std::string gameInfoValue(std::string_view gameinfoText, std::string_view key);

/// Returns the text with the key set: the value on its existing line is replaced (spacing and
/// comments stay); a missing key is added as the first line of the GameInfo block; an empty
/// value removes the line. Everything else stays as it was.
[[nodiscard]] std::string setGameInfoValue(std::string_view gameinfoText, std::string_view key, std::string_view value);

// --- Text encodings ------------------------------------------------------------------------

/// Bytes of a text file as UTF-8: UTF-16 LE/BE (with BOM, or LE recognised by its zero bytes)
/// is converted, a UTF-8 BOM is dropped, anything else is taken as UTF-8.
[[nodiscard]] std::string decodeTextFile(const std::vector<std::byte>& bytes);
/// UTF-8 text as UTF-16 LE with BOM - the encoding the game expects for localization files.
[[nodiscard]] std::vector<std::byte> encodeUtf16Le(std::string_view utf8);

// --- Localization (resource/<mod>_<language>.txt) -------------------------------------------

/// A VGUI localization file: its language and its tokens in file order.
struct Localization {
	std::string language = "English";
	std::vector<std::pair<std::string, std::string>> tokens;

	/// Text of a token (case-insensitive, without '#'); empty if absent.
	[[nodiscard]] std::string get(std::string_view token) const;
	/// Sets a token (keeps its place; new tokens go to the end). An empty text removes it.
	void set(std::string_view token, std::string text);
};

/// Reads a localization file (text already decoded to UTF-8). Escapes (\" \\ \n) are resolved.
/// Broken text gives what could be read.
[[nodiscard]] Localization parseLocalization(std::string_view text);
/// Writes the localization file as UTF-8 text (encode with encodeUtf16Le before saving).
[[nodiscard]] std::string writeLocalization(const Localization& localization);

// --- Chapters ------------------------------------------------------------------------------

/// The map a chapter config starts: the name after the first "map" command; empty if none.
[[nodiscard]] std::string chapterMap(std::string_view cfgText);
/// The chapter config with its map command set (other lines stay; added if missing).
[[nodiscard]] std::string setChapterMap(std::string_view cfgText, std::string_view map);
/// Localization token of a chapter title as the game looks it up: "<MODFOLDER>_Chapter<N>_Title".
[[nodiscard]] std::string chapterTitleToken(std::string_view modFolder, int chapter);

/// scripts/ChapterBackgrounds.txt: chapter number -> background map name.
[[nodiscard]] std::map<int, std::string> parseChapterBackgrounds(std::string_view text);
[[nodiscard]] std::string writeChapterBackgrounds(const std::map<int, std::string>& backgrounds);

// --- Main menu entries (resource/GameMenu.res) ---------------------------------------------

/// One entry of the main menu.
struct GameMenuItem {
	std::string label;              ///< Shown text, or a "#token" from a localization file
	std::string command;            ///< "OpenNewGameDialog", "Quit", "engine map mymap", ...
	bool onlyInGame = false;        ///< Only while a map is loaded (Resume, Save ...)
	bool notMulti = false;          ///< Hidden in multiplayer
	bool notSingle = false;         ///< Hidden in singleplayer
	std::vector<KvNode> extra;      ///< Keys the editor does not know (kept as they are)
};

/// Entries in their menu order. Broken text gives an empty list.
[[nodiscard]] std::vector<GameMenuItem> parseGameMenu(std::string_view text);
/// GameMenu.res text with the entries numbered 1, 2, 3 ... in this order.
[[nodiscard]] std::string writeGameMenu(const std::vector<GameMenuItem>& items);
/// The menu of a Source 2013 singleplayer game, for mods that have none yet.
[[nodiscard]] std::vector<GameMenuItem> defaultGameMenu();

/// Commands a menu entry can run, with a short English description each.
struct MenuCommand {
	std::string command;
	std::string description;
};
/// Commands offered in the menu editor (OpenNewGameDialog, Quit, ...), with what they do.
[[nodiscard]] const std::vector<MenuCommand>& commonMenuCommands();

/// English text of the game's own menu tokens ("#GameUI_GameMenu_NewGame" -> "New Game");
/// empty for other tokens.
[[nodiscard]] std::string builtInMenuLabel(std::string_view token);

// --- The whole menu of a mod ---------------------------------------------------------------

/// One chapter of "New Game".
struct ChapterInfo {
	int number = 1;
	std::string map;          ///< Map the chapter starts (without .bsp)
	std::string title;        ///< Its title (from the localization file)
	std::string background;   ///< Background map behind the menu for this chapter (may be empty)
};

/// Everything the menu editor works on.
struct ModMenu {
	std::map<std::string, std::string> info;   ///< gameinfo display keys (see gameInfoDisplayKeys)
	std::vector<ChapterInfo> chapters;         ///< In chapter order (numbers 1..n when saved)
	std::vector<GameMenuItem> items;           ///< Main menu entries
	Localization localization;                 ///< The mod's own localization file (titles live here)
	bool menuFromMod = false;                  ///< GameMenu.res came from the mod (not the base game / default)
};

/// Reads the menu of the mod in `modDir` (folder with gameinfo.txt). Files the mod lacks are
/// taken from the base game through `fs` when given (GameMenu.res, ChapterBackgrounds.txt), so
/// editing starts from what the player sees today.
[[nodiscard]] ModMenu loadModMenu(const std::filesystem::path& modDir, const GameFileSystem* fs = nullptr, std::string_view language = "english");

/// Result of saving: the files written (and the backups made), or what went wrong.
struct ModMenuSaveResult {
	std::vector<std::filesystem::path> written;
	std::vector<std::filesystem::path> backups;
	std::string error;   ///< Empty when everything was written
};

/// Writes the menu into the mod: gameinfo.txt (only the display keys change), cfg/chapterN.cfg
/// (and removes chapter configs past the last chapter), the localization file, the chapter
/// backgrounds and GameMenu.res. A file that already exists is copied to "<name>.bak" before it
/// is first overwritten (an existing .bak is never replaced).
ModMenuSaveResult saveModMenu(const std::filesystem::path& modDir, const ModMenu& menu, std::string_view language = "english");

} // namespace sme
