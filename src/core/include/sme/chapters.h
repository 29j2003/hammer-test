// chapters.h - The chapters of a singleplayer mod ("New Game") with everything that belongs to
// them, read from and written back to the mod's own files:
//   - cfg/chapterN.cfg                      the map chapter N starts ("map <name>")
//   - resource/<mod>_<language>.txt         its title per language (<MOD>_Chapter<N>_Title)
//   - materials/vgui/chapters/chapterN      its picture in New Game (256x128 texture, the
//                                           picture in the top-left 152x86)
//   - scripts/ChapterBackgrounds.txt        the background map behind the menu per chapter
//   - materials/console/<background>[_widescreen]  the loading picture of that background map
//   - scripts/chapters.txt                  Mapbase: which maps belong to which chapter - unlocks
//                                           chapters in New Game and names saves
//                                           (game/shared/mapbase/mapbase_shared.cpp)
//
// mod_menu.h edits gameinfo.txt and the menu entries; this is the chapter part, with pictures.
// Every file Mapsmith writes is listed in cfg/mapsmith_chapters.txt; an existing file is copied
// to "<file>.bak" before it is first replaced, so restoreChapters() puts the mod back as it was.
// Files of the mod are untrusted: sizes and names are checked, map names never leave maps/.

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sme/texture.h>

namespace sme {

class GameFileSystem;

/// Size of a chapter picture texture and of the part New Game shows (HL2's own pictures).
inline constexpr unsigned CHAPTER_PICTURE_WIDTH = 256, CHAPTER_PICTURE_HEIGHT = 128;
inline constexpr unsigned CHAPTER_PICTURE_SHOWN_WIDTH = 152, CHAPTER_PICTURE_SHOWN_HEIGHT = 86;
/// Size of a loading picture (materials/console/<background>, both variants).
inline constexpr unsigned LOADING_PICTURE_SIZE = 1024;

/// A map name the files may hold: letters, digits, '_', '-', '.'; no folders, at most 63 chars.
[[nodiscard]] bool isSafeMapName(std::string_view name);

/// One chapter of New Game.
struct Chapter {
	std::string map;                              ///< First map (cfg/chapterN.cfg), without .bsp
	std::vector<std::string> moreMaps;            ///< Other maps (or map name beginnings) of the chapter (scripts/chapters.txt)
	std::map<std::string, std::string> titles;    ///< Lower-case language ("english") -> title
	std::string background;                       ///< Background map behind the menu (may be empty)
	std::vector<std::byte> picture;               ///< VTF of its picture; empty: none
	bool pictureFromGame = false;                 ///< The picture is the base game's (not in the mod)
	int pictureNumber = 0;                        ///< chapterN the picture was read from (0: new)

	/// Title in a language, else the English one.
	[[nodiscard]] std::string title(std::string_view language = "english") const;
};

/// The chapters of a mod and what goes with them.
struct ChapterSet {
	std::vector<Chapter> chapters;                ///< In order; numbers 1..n when saved
	std::vector<std::string> languages;           ///< Languages the mod has a file for ("english" first, always there)
	std::map<std::string, ImageRgba> newLoadingPictures;   ///< Background map -> new loading picture, written on save
	bool chapterList = false;                     ///< scripts/chapters.txt was there when loading
};

/// Reads the chapters of the mod in `modDir`. Pictures and background lists the mod lacks come
/// from the base game through `fs` when given (that is what the player sees today).
[[nodiscard]] ChapterSet loadChapters(const std::filesystem::path& modDir, const GameFileSystem* fs = nullptr);

/// What saving wrote.
struct ChapterSaveResult {
	std::vector<std::filesystem::path> written;
	std::vector<std::filesystem::path> removed;
	std::string error;   ///< Empty when everything was written
};

/// Writes the chapters into the mod: configs 1..n (and removes configs past the last chapter),
/// titles in every language of the set, pictures (moved along when chapters are reordered),
/// ChapterBackgrounds.txt, new loading pictures and scripts/chapters.txt.
ChapterSaveResult saveChapters(const std::filesystem::path& modDir, const ChapterSet& set);

/// Puts back every file saveChapters changed (from the .bak copies) and removes the ones it
/// created. False with `error` if a file could not be changed.
bool restoreChapters(const std::filesystem::path& modDir, std::string* error = nullptr);
/// Whether the mod has chapter changes from Mapsmith to restore.
[[nodiscard]] bool hasChapterChanges(const std::filesystem::path& modDir);
/// <mod>/cfg/mapsmith_chapters.txt
[[nodiscard]] std::filesystem::path chapterLogFile(const std::filesystem::path& modDir);

// --- scripts/chapters.txt (Mapbase) --------------------------------------------------------------

/// Mapbase's chapter list: chapter numbers with their title token, and map name beginnings with
/// the title token of their chapter (the first match wins, like the game's own table).
struct ChapterList {
	std::vector<std::pair<int, std::string>> chapters;          ///< 1 -> "#MYMOD_Chapter1_Title"
	std::vector<std::pair<std::string, std::string>> maps;      ///< "mymod_01" -> "#MYMOD_Chapter1_Title"
};
/// Reads scripts/chapters.txt; broken text gives what could be read.
[[nodiscard]] ChapterList parseChapterList(std::string_view text);
[[nodiscard]] std::string writeChapterList(const ChapterList& list);
/// The list for the chapters: every chapter's first map and its other maps, longer names first
/// so "map_1a" is not taken by "map_1".
[[nodiscard]] ChapterList chapterListFor(const std::vector<Chapter>& chapters, std::string_view modFolder);

// --- Pictures ------------------------------------------------------------------------------------

/// A picture for New Game: the photo cropped to 152:86 and scaled, in the top-left of a
/// transparent 256x128 picture.
[[nodiscard]] ImageRgba chapterPictureImage(const ImageRgba& photo);
/// A loading picture: the photo cropped to 4:3 (or 16:9 for the widescreen one) and stretched to
/// 1024x1024 - the game stretches it back over the screen.
[[nodiscard]] ImageRgba loadingPictureImage(const ImageRgba& photo, bool widescreen);
/// A VTF for menu pictures: no mipmaps, no LOD, clamped; DXT1 when `compress` and opaque, else BGRA8888.
[[nodiscard]] std::optional<std::vector<std::byte>> encodeMenuVtf(const ImageRgba& image, bool compress, std::string* error = nullptr);
/// The material of chapter picture N / of a loading picture ("background01", "background01_widescreen").
[[nodiscard]] std::string chapterPictureVmt(int chapter);
[[nodiscard]] std::string loadingPictureVmt(std::string_view textureName);

// --- Checks --------------------------------------------------------------------------------------

/// Where a map is.
enum class MapState { Compiled, SourceOnly, Missing };

/// One thing to look at before shipping.
struct ChapterProblem {
	int chapter = 0;          ///< 1-based; 0 for the whole set
	bool error = false;       ///< New Game breaks (no map, missing map); else a warning/hint
	std::string text;
};

/// Checks the chapters: maps set and compiled, titles in every language, pictures and their size,
/// background maps and their loading pictures, the same map twice. `mapState` tells where a map
/// is; `hasLoadingPicture(background)` whether its console picture exists.
[[nodiscard]] std::vector<ChapterProblem> checkChapters(const ChapterSet& set, const std::function<MapState(const std::string&)>& mapState,
                                                        const std::function<bool(const std::string&)>& hasLoadingPicture);

} // namespace sme
