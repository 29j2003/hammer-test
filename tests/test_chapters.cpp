// test_chapters.cpp - Chapters of a mod: read with titles in every language, pictures, background
// maps and Mapbase's chapter list; reordered and saved (pictures move along); checked; restored.

#include <doctest/doctest.h>

#include <sme/chapters.h>
#include <sme/localization.h>
#include <sme/mod_menu.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <filesystem>
#include <fstream>

using namespace sme;

namespace {

std::filesystem::path tempModDir(const std::string& name) {
	const auto dir = std::filesystem::temp_directory_path() / ("sme_chapters_" + name) / "mymod";
	std::filesystem::remove_all(dir.parent_path());
	std::filesystem::create_directories(dir);
	return dir;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeFile(const std::filesystem::path& path, std::string_view text) {
	std::vector<std::byte> bytes(text.size());
	std::transform(text.begin(), text.end(), bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
	writeBytes(path, bytes);
}

std::string readFile(const std::filesystem::path& path) {
	return readFileText(path).value_or(std::string());
}

ImageRgba solid(unsigned w, unsigned h, unsigned char r, unsigned char g, unsigned char b) {
	ImageRgba image;
	image.width = static_cast<std::uint16_t>(w);
	image.height = static_cast<std::uint16_t>(h);
	image.pixels.resize(std::size_t(w) * h * 4);
	for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
		image.pixels[i] = std::byte{r};
		image.pixels[i + 1] = std::byte{g};
		image.pixels[i + 2] = std::byte{b};
		image.pixels[i + 3] = std::byte{255};
	}
	return image;
}

std::vector<std::byte> pictureVtf(unsigned char r) {
	return encodeMenuVtf(chapterPictureImage(solid(320, 180, r, 10, 10)), false).value();
}

LocalizationFile titles(std::string language, std::vector<std::pair<std::string, std::string>> tokens) {
	LocalizationFile file;
	file.language = std::move(language);
	for (auto& [token, text] : tokens) file.set(token, std::move(text));
	return file;
}

} // namespace

TEST_CASE("chapters: map names are checked, the chapter list is read and written") {
	CHECK(isSafeMapName("d1_trainstation_01"));
	CHECK(isSafeMapName("my-map.v2"));
	CHECK_FALSE(isSafeMapName(""));
	CHECK_FALSE(isSafeMapName("../cfg/autoexec"));
	CHECK_FALSE(isSafeMapName("maps/x"));
	CHECK_FALSE(isSafeMapName("a b"));
	CHECK_FALSE(isSafeMapName(std::string(64, 'a')));

	const auto list = parseChapterList("\"ChapterList\"\n{\n\t\"Chapters\" { \"1\" \"#MYMOD_Chapter1_Title\" \"2\" \"#MYMOD_Chapter2_Title\" \"x\" \"bad\" }\n"
	                                   "\t\"mymod_01\" \"#MYMOD_Chapter1_Title\"\n\t\"mymod_02\" \"#MYMOD_Chapter2_Title\"\n}\n");
	REQUIRE(list.chapters.size() == 2);
	CHECK(list.chapters[1] == std::pair<int, std::string>{2, "#MYMOD_Chapter2_Title"});
	REQUIRE(list.maps.size() == 2);
	CHECK(list.maps[0].first == "mymod_01");
	const auto again = parseChapterList(writeChapterList(list));
	CHECK(again.chapters == list.chapters);
	CHECK(again.maps == list.maps);
	CHECK(parseChapterList("not { valid").chapters.empty());

	// Longer map names first: "a_1b" must not be taken by "a_1".
	std::vector<Chapter> chapters(2);
	chapters[0].map = "a_1";
	chapters[1].map = "a_2";
	chapters[1].moreMaps = {"a_1b", "a_1"};   // a_1 again: listed once
	const auto made = chapterListFor(chapters, "mymod");
	REQUIRE(made.maps.size() == 3);
	CHECK(made.maps[0] == std::pair<std::string, std::string>{"a_1b", "#MYMOD_Chapter2_Title"});
	CHECK(made.chapters[0].second == "#MYMOD_Chapter1_Title");
}

TEST_CASE("chapters: pictures are made in the game's sizes") {
	const auto picture = chapterPictureImage(solid(1920, 1080, 200, 100, 50));
	REQUIRE(picture.width == CHAPTER_PICTURE_WIDTH);
	REQUIRE(picture.height == CHAPTER_PICTURE_HEIGHT);
	const auto alphaAt = [&](unsigned x, unsigned y) { return std::to_integer<int>(picture.pixels[(std::size_t(y) * picture.width + x) * 4 + 3]); };
	CHECK(alphaAt(0, 0) == 255);
	CHECK(alphaAt(151, 85) == 255);
	CHECK(alphaAt(152, 0) == 0);   // outside the shown part: transparent
	CHECK(alphaAt(0, 86) == 0);
	const auto vtf = encodeMenuVtf(picture, false);
	REQUIRE(vtf);
	const auto info = readTextureInfo(*vtf);
	REQUIRE(info);
	CHECK(info->width == 256);
	CHECK(info->height == 128);

	const auto loading = loadingPictureImage(solid(1600, 900, 1, 2, 3), true);
	CHECK(loading.width == LOADING_PICTURE_SIZE);
	CHECK(loading.height == LOADING_PICTURE_SIZE);
	CHECK(encodeMenuVtf(loading, true));
	CHECK(loadingPictureImage(ImageRgba{}, false).pixels.empty());
	CHECK_FALSE(encodeMenuVtf(ImageRgba{}, true));
	CHECK(chapterPictureVmt(3).find("vgui/chapters/chapter3") != std::string::npos);
	CHECK(loadingPictureVmt("background02_widescreen").find("console/background02_widescreen") != std::string::npos);
}

TEST_CASE("chapters: read, reorder, save with titles in every language and pictures, check and restore") {
	const auto mod = tempModDir("roundtrip");
	writeFile(mod / "gameinfo.txt", "\"GameInfo\" { game \"My Mod\" }\n");
	writeFile(mod / "cfg/chapter1.cfg", "// first\nmap mymod_01\n");
	writeFile(mod / "cfg/chapter2.cfg", "map mymod_02\n");
	writeFile(mod / "cfg/chapter3.cfg", "map mymod_03\n");
	saveLocalizationFile(mod / "resource/mymod_english.txt",
	                     titles("English", {{"MYMOD_Chapter1_Title", "ONE"}, {"MYMOD_Chapter2_Title", "TWO"}, {"MYMOD_Chapter3_Title", "THREE"}, {"MYMOD_Other", "keep"}}));
	saveLocalizationFile(mod / "resource/mymod_german.txt", titles("German", {{"MYMOD_Chapter1_Title", "EINS"}}));
	writeFile(mod / "scripts/ChapterBackgrounds.txt", "\"chapters\"\n{\n\t1\t\"background01\"\n}\n");
	writeFile(mod / "scripts/chapters.txt", "\"ChapterList\" { \"Chapters\" { \"1\" \"#MYMOD_Chapter1_Title\" } \"mymod_01\" \"#MYMOD_Chapter1_Title\" "
	                                         "\"mymod_01b\" \"#MYMOD_Chapter1_Title\" }\n");
	const auto red = pictureVtf(250), blue = pictureVtf(5);
	writeBytes(mod / "materials/vgui/chapters/chapter1.vtf", red);
	writeFile(mod / "materials/vgui/chapters/chapter1.vmt", chapterPictureVmt(1));
	writeBytes(mod / "materials/vgui/chapters/chapter3.vtf", blue);
	writeFile(mod / "materials/vgui/chapters/chapter3.vmt", chapterPictureVmt(3));

	auto set = loadChapters(mod);
	REQUIRE(set.chapters.size() == 3);
	CHECK(set.languages == std::vector<std::string>{"english", "german"});
	CHECK(set.chapterList);
	CHECK(set.chapters[0].map == "mymod_01");
	CHECK(set.chapters[0].title() == "ONE");
	CHECK(set.chapters[0].title("german") == "EINS");
	CHECK(set.chapters[1].title("german") == "TWO");   // falls back to English
	CHECK(set.chapters[0].background == "background01");
	CHECK(set.chapters[0].moreMaps == std::vector<std::string>{"mymod_01b"});
	CHECK(set.chapters[0].picture == red);
	CHECK(set.chapters[1].picture.empty());
	CHECK(set.chapters[2].picture == blue);

	// Checks: a missing map, an uncompiled one, a missing German title, no picture.
	const auto mapState = [](const std::string& map) {
		if (map == "mymod_02") return MapState::SourceOnly;
		if (map == "mymod_03") return MapState::Missing;
		return MapState::Compiled;
	};
	auto problems = checkChapters(set, mapState, [](const std::string&) { return false; });
	const auto has = [&](int chapter, std::string_view part, bool error) {
		return std::any_of(problems.begin(), problems.end(),
		                   [&](const ChapterProblem& p) { return p.chapter == chapter && p.error == error && p.text.find(part) != std::string::npos; });
	};
	CHECK(has(3, "not found", true));
	CHECK(has(2, "not compiled", false));
	CHECK(has(2, "German", false));
	CHECK(has(2, "No picture", false));
	CHECK(has(1, "loading picture", false));
	CHECK_FALSE(has(1, "German", false));

	// Reorder (3 first), drop the old chapter 2, change a title, a new German title, a new loading picture.
	std::vector<Chapter> reordered{set.chapters[2], set.chapters[0]};
	set.chapters = reordered;
	set.chapters[0].titles["english"] = "THE START";
	set.chapters[1].titles["german"] = "ZWEI";
	set.chapters[1].background = "background02";
	set.newLoadingPictures["background02"] = solid(800, 600, 9, 9, 9);
	const auto result = saveChapters(mod, set);
	REQUIRE(result.error.empty());

	CHECK(chapterMap(readFile(mod / "cfg/chapter1.cfg")) == "mymod_03");
	CHECK(chapterMap(readFile(mod / "cfg/chapter2.cfg")) == "mymod_01");
	CHECK_FALSE(std::filesystem::exists(mod / "cfg/chapter3.cfg"));
	const auto english = loadLocalizationFile(mod / "resource/mymod_english.txt").value();
	CHECK(english.get("MYMOD_Chapter1_Title") == "THE START");
	CHECK(english.get("MYMOD_Chapter2_Title") == "ONE");
	CHECK(english.find("MYMOD_Chapter3_Title") < 0);
	CHECK(english.get("MYMOD_Other") == "keep");
	const auto german = loadLocalizationFile(mod / "resource/mymod_german.txt").value();
	CHECK(german.get("MYMOD_Chapter2_Title") == "ZWEI");
	CHECK(german.find("MYMOD_Chapter1_Title") < 0);   // chapter 3 had no German title
	// The pictures moved with their chapters.
	const auto bytes = [](const std::filesystem::path& p) {
		const auto text = readFile(p);
		std::vector<std::byte> out(text.size());
		std::transform(text.begin(), text.end(), out.begin(), [](char c) { return static_cast<std::byte>(c); });
		return out;
	};
	CHECK(bytes(mod / "materials/vgui/chapters/chapter1.vtf") == blue);
	CHECK(bytes(mod / "materials/vgui/chapters/chapter2.vtf") == red);
	CHECK_FALSE(std::filesystem::exists(mod / "materials/vgui/chapters/chapter3.vtf"));
	CHECK(parseChapterBackgrounds(readFile(mod / "scripts/ChapterBackgrounds.txt")).at(2) == "background02");
	CHECK(std::filesystem::exists(mod / "materials/console/background02.vtf"));
	CHECK(std::filesystem::exists(mod / "materials/console/background02_widescreen.vmt"));
	const auto list = parseChapterList(readFile(mod / "scripts/chapters.txt"));
	REQUIRE(list.chapters.size() == 2);
	CHECK(std::count(list.maps.begin(), list.maps.end(), std::pair<std::string, std::string>{"mymod_01b", "#MYMOD_Chapter2_Title"}) == 1);
	CHECK(std::filesystem::exists(mod / "cfg/chapter1.cfg.bak"));
	CHECK(hasChapterChanges(mod));

	// Reading again gives the same.
	const auto again = loadChapters(mod);
	REQUIRE(again.chapters.size() == 2);
	CHECK(again.chapters[0].title() == "THE START");
	CHECK(again.chapters[1].picture == red);
	CHECK(again.chapters[1].moreMaps == std::vector<std::string>{"mymod_01b"});
	// Saving unchanged writes nothing.
	CHECK(saveChapters(mod, again).written.empty());

	// Unsafe names never reach the disk.
	auto bad = again;
	bad.chapters[0].map = "../../evil";
	CHECK_FALSE(saveChapters(mod, bad).error.empty());

	// Restore: everything as it was.
	std::string error;
	REQUIRE(restoreChapters(mod, &error));
	CHECK(error.empty());
	CHECK(chapterMap(readFile(mod / "cfg/chapter1.cfg")) == "mymod_01");
	CHECK(chapterMap(readFile(mod / "cfg/chapter3.cfg")) == "mymod_03");
	CHECK(bytes(mod / "materials/vgui/chapters/chapter1.vtf") == red);
	CHECK(bytes(mod / "materials/vgui/chapters/chapter3.vtf") == blue);
	CHECK_FALSE(std::filesystem::exists(mod / "materials/vgui/chapters/chapter2.vtf"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/console/background02.vtf"));
	CHECK(loadLocalizationFile(mod / "resource/mymod_german.txt").value().get("MYMOD_Chapter1_Title") == "EINS");
	CHECK_FALSE(std::filesystem::exists(mod / "cfg/chapter1.cfg.bak"));
	CHECK_FALSE(hasChapterChanges(mod));
	CHECK(loadChapters(mod).chapters.size() == 3);
}

TEST_CASE("chapters: a new mod gets its files; no chapters at all is a hint") {
	const auto mod = tempModDir("new");
	ChapterSet set;
	CHECK(checkChapters(set, {}, {}).size() == 1);
	set.languages = {"english"};
	Chapter chapter;
	chapter.map = "start";
	chapter.titles["english"] = "BEGIN";
	chapter.picture = pictureVtf(100);
	set.chapters.push_back(chapter);
	REQUIRE(saveChapters(mod, set).error.empty());
	CHECK(chapterMap(readFile(mod / "cfg/chapter1.cfg")) == "start");
	CHECK(loadLocalizationFile(mod / "resource/mymod_english.txt").value().get("MYMOD_Chapter1_Title") == "BEGIN");
	CHECK(std::filesystem::exists(mod / "materials/vgui/chapters/chapter1.vmt"));
	CHECK(std::filesystem::exists(mod / "scripts/chapters.txt"));
	CHECK_FALSE(std::filesystem::exists(mod / "scripts/ChapterBackgrounds.txt"));   // no backgrounds: not written
	REQUIRE(restoreChapters(mod));
	CHECK_FALSE(std::filesystem::exists(mod / "cfg/chapter1.cfg"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/vgui/chapters/chapter1.vtf"));
}
