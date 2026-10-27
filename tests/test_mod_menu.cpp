// test_mod_menu.cpp - The mod's main menu files: gameinfo keys, localization (UTF-16),
// chapters, chapter backgrounds and GameMenu.res, each read and written back.

#include <doctest/doctest.h>

#include <sme/mod_menu.h>
#include <sme/text.h>

#include <filesystem>
#include <fstream>

using namespace sme;

namespace {

const char* GAMEINFO = "\"GameInfo\"\n"
                       "{\n"
                       "\tgame\t\t\"My Mod\"   // shown in Steam\n"
                       "\ttitle\t\"MY MOD\"\n"
                       "\ttype\t\tsingleplayer_only\n"
                       "\n"
                       "\tFileSystem\n"
                       "\t{\n"
                       "\t\tSteamAppId\t\t243730\n"
                       "\t\tSearchPaths\n"
                       "\t\t{\n"
                       "\t\t\tgame+mod\t\t\t|gameinfo_path|.\n"
                       "\t\t}\n"
                       "\t}\n"
                       "}\n";

std::filesystem::path tempModDir(const std::string& name) {
	const auto dir = std::filesystem::temp_directory_path() / ("sme_menu_" + name) / "mymod";
	std::filesystem::remove_all(dir.parent_path());
	std::filesystem::create_directories(dir);
	return dir;
}

void writeFile(const std::filesystem::path& path, std::string_view text) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string readFile(const std::filesystem::path& path) {
	return readFileText(path).value_or(std::string());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
	const std::string text = readFile(path);
	std::vector<std::byte> out(text.size());
	for (std::size_t i = 0; i < text.size(); ++i) out[i] = static_cast<std::byte>(text[i]);
	return out;
}

} // namespace

TEST_CASE("gameinfo.txt: display keys change in place, everything else stays") {
	CHECK(gameInfoValue(GAMEINFO, "game") == "My Mod");
	CHECK(gameInfoValue(GAMEINFO, "TYPE") == "singleplayer_only");
	CHECK(gameInfoValue(GAMEINFO, "SteamAppId").empty());   // inside FileSystem, not a display key
	CHECK(gameInfoValue(GAMEINFO, "title2").empty());

	// Replace: the comment and the spacing stay, only that line differs.
	const std::string renamed = setGameInfoValue(GAMEINFO, "game", "Better Mod");
	CHECK(renamed.find("\tgame\t\t\"Better Mod\"   // shown in Steam\n") != std::string::npos);
	CHECK(gameInfoValue(renamed, "game") == "Better Mod");
	CHECK(renamed.size() == std::string(GAMEINFO).size() + 4);
	// Add: a new line at the top of the block.
	const std::string added = setGameInfoValue(GAMEINFO, "title2", "Episode One");
	CHECK(gameInfoValue(added, "title2") == "Episode One");
	CHECK(added.find("{\n\ttitle2\t\t\"Episode One\"\n\tgame") != std::string::npos);
	// Remove: an empty value takes the line out.
	const std::string removed = setGameInfoValue(GAMEINFO, "title", "");
	CHECK(gameInfoValue(removed, "title").empty());
	CHECK(removed.find("SteamAppId") != std::string::npos);
	// Unquoted values are replaced too; CRLF files keep CRLF for new lines.
	CHECK(gameInfoValue(setGameInfoValue(GAMEINFO, "type", "multiplayer_only"), "type") == "multiplayer_only");
	std::string crlf;
	for (const char c : std::string(GAMEINFO)) {
		if (c == '\n') crlf += '\r';
		crlf += c;
	}
	CHECK(setGameInfoValue(crlf, "icon", "resource/icon").find("\ticon\t\t\"resource/icon\"\r\n") != std::string::npos);
}

TEST_CASE("localization files: UTF-16 with BOM, escapes, other tokens stay") {
	const std::string text = "\"lang\"\n{\n\"Language\" \"English\"\n\"Tokens\"\n{\n"
	                         "// comment\n"
	                         "\"MYMOD_Chapter1_Title\"\t\"WAKE UP\"\n"
	                         "\"MyMod_Quote\"\t\"He said \\\"hi\\\"\\nthen left\"\n"
	                         "\"MyMod_Umlaut\"\t\"Gr\xC3\xBC\xC3\x9F" "e\"\n"
	                         "\"[english]MyMod_Old\"\t\"old\"\n"
	                         "}\n}\n";
	const auto bytes = encodeUtf16Le(text);
	REQUIRE(bytes.size() > 4);
	CHECK(bytes[0] == std::byte{0xFF});
	CHECK(bytes[1] == std::byte{0xFE});
	CHECK(decodeTextFile(bytes) == text);   // round trip, umlauts included

	auto loc = parseLocalization(decodeTextFile(bytes));
	CHECK(loc.language == "English");
	REQUIRE(loc.tokens.size() == 4);
	CHECK(loc.get("#mymod_chapter1_title") == "WAKE UP");
	CHECK(loc.get("MyMod_Quote") == "He said \"hi\"\nthen left");
	CHECK(loc.get("MyMod_Umlaut") == "Gr\xC3\xBC\xC3\x9F" "e");

	loc.set("MYMOD_Chapter1_Title", "RISE AND SHINE");
	loc.set("MYMOD_Chapter2_Title", "THE CITY");
	loc.set("MyMod_Umlaut", "");
	const auto again = parseLocalization(writeLocalization(loc));
	CHECK(again.tokens.size() == 4);   // one removed, one added
	CHECK(again.tokens.front().first == "MYMOD_Chapter1_Title");   // keeps its place
	CHECK(again.get("MYMOD_Chapter1_Title") == "RISE AND SHINE");
	CHECK(again.get("MYMOD_Chapter2_Title") == "THE CITY");
	CHECK(again.get("MyMod_Quote") == "He said \"hi\"\nthen left");
	CHECK(again.get("[english]MyMod_Old") == "old");
	// UTF-8 with BOM is read as UTF-8.
	CHECK(decodeTextFile({std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF}, std::byte{'a'}}) == "a");
}

TEST_CASE("chapters, backgrounds and menu entries") {
	CHECK(chapterMap("// start\nmap d1_trainstation_01\n") == "d1_trainstation_01");
	CHECK(chapterMap("echo nothing\n").empty());
	CHECK(setChapterMap("// start\nmap old_map\nsv_cheats 0\n", "new_map") == "// start\nmap new_map\nsv_cheats 0\n");
	CHECK(setChapterMap("", "first") == "map first\n");
	CHECK(chapterTitleToken("mymod", 3) == "MYMOD_Chapter3_Title");

	const auto backgrounds = parseChapterBackgrounds("\"chapters\"\n{\n\t1\t\"background01\"\n\t2\t\"background02\"\n\tbad\t\"x\"\n}\n");
	REQUIRE(backgrounds.size() == 2);
	CHECK(backgrounds.at(2) == "background02");
	CHECK(parseChapterBackgrounds(writeChapterBackgrounds(backgrounds)) == backgrounds);

	const std::string res = "\"GameMenu\"\n{\n"
	                        "\t\"1\"\n\t{\n\t\t\"label\" \"#GameUI_GameMenu_ResumeGame\"\n\t\t\"command\" \"ResumeGame\"\n\t\t\"OnlyInGame\" \"1\"\n\t}\n"
	                        "\t\"2\"\n\t{\n\t\t\"label\" \"#GameUI_GameMenu_NewGame\"\n\t\t\"command\" \"OpenNewGameDialog\"\n\t\t\"notmulti\" \"1\"\n\t\t\"ConsoleOnly\" \"1\" [$X360]\n\t}\n"
	                        "\t\"3\"\n\t{\n\t\t\"label\" \"Play my map\"\n\t\t\"command\" \"engine map mymap\"\n\t}\n"
	                        "}\n";
	auto items = parseGameMenu(res);
	REQUIRE(items.size() == 3);
	CHECK(items[0].onlyInGame);
	CHECK(items[1].notMulti);
	REQUIRE(items[1].extra.size() == 1);   // the unknown key stays
	CHECK(items[2].command == "engine map mymap");
	CHECK(builtInMenuLabel("#gameui_gamemenu_newgame") == "New Game");
	CHECK(builtInMenuLabel("#Something_Else").empty());

	// Reorder and write: numbered again, unknown keys kept.
	std::swap(items[1], items[2]);
	const auto written = writeGameMenu(items);
	const auto back = parseGameMenu(written);
	REQUIRE(back.size() == 3);
	CHECK(back[1].label == "Play my map");
	CHECK(back[2].notMulti);
	REQUIRE(back[2].extra.size() == 1);
	CHECK(back[2].extra[0].condition == "[$X360]");
	CHECK(written.find("\"2\"") != std::string::npos);
	CHECK(defaultGameMenu().size() == 6);
	CHECK_FALSE(commonMenuCommands().empty());
	CHECK(parseGameMenu("not { valid").empty());
}

TEST_CASE("the whole menu of a mod: load, change, save with backups") {
	const auto mod = tempModDir("whole");
	writeFile(mod / "gameinfo.txt", GAMEINFO);
	writeFile(mod / "cfg/chapter1.cfg", "map first_map\n");
	writeFile(mod / "cfg/chapter2.cfg", "map second_map\n");
	writeFile(mod / "cfg/chapter3.cfg", "map third_map\n");
	{
		Localization loc;
		loc.set("MYMOD_Chapter1_Title", "ONE");
		loc.set("MYMOD_Other", "keep me");
		writeBytes(mod / "resource/mymod_english.txt", encodeUtf16Le(writeLocalization(loc)));
	}

	auto menu = loadModMenu(mod);
	CHECK(menu.info.at("game") == "My Mod");
	REQUIRE(menu.chapters.size() == 3);
	CHECK(menu.chapters[0].title == "ONE");
	CHECK(menu.chapters[2].map == "third_map");
	CHECK_FALSE(menu.menuFromMod);
	CHECK(menu.items.size() == 6);   // no GameMenu.res: the default menu

	// Change: new title, drop chapter 2, a background for chapter 1, a menu entry.
	menu.info["title"] = "BETTER MOD";
	menu.chapters.erase(menu.chapters.begin() + 1);
	menu.chapters[0].background = "background01";
	menu.chapters[1].title = "THE END";
	menu.items.push_back({"Play my map", "engine map third_map", false, false, false, {}});
	const auto result = saveModMenu(mod, menu);
	CHECK(result.error.empty());
	CHECK_FALSE(result.written.empty());

	// gameinfo: the title changed, the rest stayed; the original is backed up once.
	const std::string gameinfo = readFile(mod / "gameinfo.txt");
	CHECK(gameInfoValue(gameinfo, "title") == "BETTER MOD");
	CHECK(gameinfo.find("SteamAppId\t\t243730") != std::string::npos);
	CHECK(readFile(mod / "gameinfo.txt.bak") == GAMEINFO);
	// Chapters renumbered: 1 = first_map, 2 = third_map; the old third config is gone (backed up).
	CHECK(chapterMap(readFile(mod / "cfg/chapter2.cfg")) == "third_map");
	CHECK_FALSE(std::filesystem::exists(mod / "cfg/chapter3.cfg"));
	CHECK(std::filesystem::exists(mod / "cfg/chapter3.cfg.bak"));
	// Titles in the UTF-16 file; other tokens stayed.
	const auto locBytes = readBytes(mod / "resource/mymod_english.txt");
	REQUIRE(locBytes.size() > 2);
	CHECK(locBytes[0] == std::byte{0xFF});
	const auto loc = parseLocalization(decodeTextFile(locBytes));
	CHECK(loc.get("MYMOD_Chapter2_Title") == "THE END");
	CHECK(loc.get("MYMOD_Chapter3_Title").empty());
	CHECK(loc.get("MYMOD_Other") == "keep me");
	CHECK(parseChapterBackgrounds(readFile(mod / "scripts/ChapterBackgrounds.txt")).at(1) == "background01");
	const auto items = parseGameMenu(readFile(mod / "resource/GameMenu.res"));
	REQUIRE(items.size() == 7);
	CHECK(items.back().command == "engine map third_map");

	// Load again: the same; saving again makes no second backup of gameinfo.
	const auto again = loadModMenu(mod);
	CHECK(again.menuFromMod);
	CHECK(again.info.at("title") == "BETTER MOD");
	REQUIRE(again.chapters.size() == 2);
	CHECK(again.chapters[1].title == "THE END");
	auto changed = again;
	changed.info["title"] = "THIRD TITLE";
	CHECK(saveModMenu(mod, changed).error.empty());
	CHECK(readFile(mod / "gameinfo.txt.bak") == GAMEINFO);   // the first backup stays
	std::filesystem::remove_all(mod.parent_path());
}
