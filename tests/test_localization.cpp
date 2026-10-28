// test_localization.cpp - The mod's text files: localization files kept as written (comments,
// conditions, other entries), UTF-16 on disk, the files of a mod, caption tags and the compiled
// caption file (VCCD) byte for byte.

#include <doctest/doctest.h>

#include <sme/localization.h>
#include <sme/mod_menu.h>
#include <sme/text.h>

#include <filesystem>
#include <fstream>

using namespace sme;

namespace {

const char* FILE_TEXT = "// My mod's texts\n"
                        "\"lang\"\n"
                        "{\n"
                        "\t\"Language\"\t\"English\"\n"
                        "\t\"Tokens\"\n"
                        "\t{\n"
                        "\t\t// The menu\n"
                        "\t\t\"MYMOD_Chapter1_Title\"\t\"The \\\"Beginning\\\"\"\n"
                        "\t\t\"MyMod_Hint\"\t\"Press E\\nto use\"\n"
                        "\t\t\"MyMod_Console\"\t\"Pad text\"\t[$X360]\n"
                        "\t\t// trailing note\n"
                        "\t}\n"
                        "}\n";

std::uint32_t read32(const std::vector<std::byte>& bytes, std::size_t at) {
	return static_cast<std::uint32_t>(bytes[at]) | static_cast<std::uint32_t>(bytes[at + 1]) << 8 | static_cast<std::uint32_t>(bytes[at + 2]) << 16 |
	       static_cast<std::uint32_t>(bytes[at + 3]) << 24;
}

std::uint16_t read16(const std::vector<std::byte>& bytes, std::size_t at) {
	return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[at]) | static_cast<unsigned>(bytes[at + 1]) << 8);
}

} // namespace

TEST_CASE("localization files keep comments, conditions and order") {
	auto file = parseLocalizationFile(FILE_TEXT);
	CHECK_FALSE(file.broken);
	CHECK(file.language == "English");
	CHECK(file.headerComments == std::vector<std::string>{"// My mod's texts"});
	REQUIRE(file.tokens.size() == 3);
	CHECK(file.tokens[0].token == "MYMOD_Chapter1_Title");
	CHECK(file.tokens[0].text == "The \"Beginning\"");
	CHECK(file.tokens[0].comments == std::vector<std::string>{"// The menu"});
	CHECK(file.get("#mymod_hint") == "Press E\nto use");
	CHECK(file.tokens[2].condition == "[$X360]");
	CHECK(file.trailingComments == std::vector<std::string>{"// trailing note"});

	// Written back unchanged (Valve's layout), and again after a parse.
	CHECK(writeLocalizationFile(file) == FILE_TEXT);
	CHECK(writeLocalizationFile(parseLocalizationFile(writeLocalizationFile(file))) == FILE_TEXT);

	file.set("MyMod_Hint", "Hold E");
	file.set("MyMod_New", "New text");
	CHECK(file.rename("MyMod_New", "MyMod_Newer"));
	CHECK_FALSE(file.rename("MyMod_Newer", "mymod_hint"));   // taken
	CHECK(file.remove("MyMod_Console"));
	CHECK_FALSE(file.remove("MyMod_Console"));
	const auto again = parseLocalizationFile(writeLocalizationFile(file));
	REQUIRE(again.tokens.size() == 3);
	CHECK(again.tokens[1].text == "Hold E");
	CHECK(again.tokens[2].token == "MyMod_Newer");
	CHECK(again.tokens[0].comments.size() == 1);

	// Unknown entries of the root block survive.
	const auto extra = parseLocalizationFile("\"lang\" { \"Language\" \"German\" \"Note\" \"x\" \"Block\" { \"a\" \"b\" } \"Tokens\" { \"A\" \"B\" } }");
	CHECK(extra.language == "German");
	CHECK(extra.extra.size() == 2);
	CHECK(writeLocalizationFile(extra).find("\"Block\" { \"a\" \"b\" }") != std::string::npos);
	CHECK(extra.get("a") == "B");

	// Broken text keeps what was read.
	const auto broken = parseLocalizationFile("\"lang\" { \"Tokens\" { \"A\" \"one\" \"B\"");
	CHECK(broken.broken);
	CHECK(broken.get("A") == "one");
}

TEST_CASE("localization files on disk: UTF-16 with BOM, other encodings, the files of a mod") {
	const auto dir = std::filesystem::temp_directory_path() / "sme_localization_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir / "resource");
	{
		// UTF-8 (as some tools write it) is read too.
		std::ofstream out(dir / "resource" / "mymod_english.txt", std::ios::binary);
		out << "\xEF\xBB\xBF" << FILE_TEXT;
	}
	{
		std::ofstream out(dir / "resource" / "closecaption_english.txt", std::ios::binary);
		out << "\"lang\" { \"Language\" \"English\" \"Tokens\" { \"npc.hello\" \"<clr:255,0,0>Hello\" } }";
	}
	{
		std::ofstream out(dir / "resource" / "notes.txt");
		out << "not a localization file";
	}
	auto file = loadLocalizationFile(dir / "resource" / "mymod_english.txt");
	REQUIRE(file);
	CHECK(file->tokens.size() == 3);
	file->set("MyMod_Umlaut", "Grüße");
	std::string error;
	const auto german = localizationPath(dir, "mymod", "german");
	CHECK(german == dir / "resource" / "mymod_german.txt");
	file->language = languageDisplayName("german");
	CHECK(file->language == "German");
	REQUIRE(saveLocalizationFile(german, *file, &error));
	{
		std::ifstream in(german, std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		REQUIRE(bytes.size() > 4);
		CHECK(static_cast<unsigned char>(bytes[0]) == 0xFF);
		CHECK(static_cast<unsigned char>(bytes[1]) == 0xFE);
		CHECK(bytes[2] == '/');
		CHECK(bytes[3] == '\0');
	}
	const auto reread = loadLocalizationFile(german);
	REQUIRE(reread);
	CHECK(reread->get("MyMod_Umlaut") == "Grüße");
	CHECK(reread->language == "German");
	CHECK(writeLocalizationFile(*reread) == writeLocalizationFile(*file));
	// The first overwrite keeps a backup.
	file->set("MyMod_Umlaut", "Hallo");
	REQUIRE(saveLocalizationFile(german, *file, &error));
	CHECK(std::filesystem::exists(dir / "resource" / "mymod_german.txt.bak"));

	const auto sets = findLocalizationSets(dir);
	REQUIRE(sets.size() == 2);
	CHECK(sets[0].name == "mymod");
	CHECK(sets[0].languages.size() == 2);
	CHECK(sets[0].languages.count("german") == 1);
	CHECK(sets[1].name == "closecaption");
	CHECK(sets[1].captions());

	CHECK(menuUseOfToken("#MYMOD_Chapter12_Title", "mymod") == "Title of chapter 12 (Menu)");
	CHECK(menuUseOfToken("MYMOD_Chapter_Title", "mymod").empty());
	CHECK(menuUseOfToken("Other_Chapter1_Title", "mymod").empty());
	std::filesystem::remove_all(dir);
}

TEST_CASE("caption tags") {
	const auto line = parseCaption("<sfx><len:2.5><clr:255,128,0>Fire <B>now<B>!<cr><I>quiet</I><norepeat:5>");
	CHECK(line.soundEffect);
	REQUIRE(line.length);
	CHECK(*line.length == doctest::Approx(2.5));
	REQUIRE(line.runs.size() == 4);
	CHECK(line.runs[0].text == "Fire ");
	CHECK(line.runs[0].r == 255);
	CHECK(line.runs[0].g == 128);
	CHECK(line.runs[0].b == 0);
	CHECK(line.runs[1].text == "now");
	CHECK(line.runs[1].bold);
	CHECK_FALSE(line.runs[2].bold);
	CHECK(line.runs[3].lineBreak);
	CHECK(line.runs[3].italic);
	CHECK(line.unknownTags == std::vector<std::string>{"/I"});
	const auto plain = parseCaption("Just text");
	CHECK_FALSE(plain.soundEffect);
	REQUIRE(plain.runs.size() == 1);
	CHECK(plain.runs[0].r == 255);
}

TEST_CASE("compiled captions (VCCD) byte for byte") {
	CHECK(captionHash("123456789") == 0xCBF43926u);
	CHECK(captionHash("A") == captionHash("a"));
	CHECK(captionHash("a") == 0xE8B7BE43u);
	CHECK(captionHash("b") == 0x71BEEFF9u);

	LocalizationFile file;
	file.set("a", "Hi");
	file.set("B", "Yo");
	const auto compiled = compileCaptions(file);
	CHECK(compiled.count == 2);
	CHECK(compiled.skipped.empty());
	// Hand-made: header, directory sorted by hash (b before a), padding to 512, one 8 KiB block.
	std::vector<std::byte> expected;
	const auto u32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i) expected.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
	};
	const auto u16 = [&](std::uint16_t v) {
		expected.push_back(static_cast<std::byte>(v & 0xFF));
		expected.push_back(static_cast<std::byte>(v >> 8));
	};
	for (const char c : {'V', 'C', 'C', 'D'}) expected.push_back(static_cast<std::byte>(c));
	u32(1);       // version
	u32(1);       // blocks
	u32(8192);    // block size
	u32(2);       // directory entries
	u32(512);     // data offset
	u32(0x71BEEFF9u);
	u32(0);
	u16(6);       // "Yo" after "Hi\0"
	u16(6);
	u32(0xE8B7BE43u);
	u32(0);
	u16(0);
	u16(6);
	expected.resize(512, std::byte{0});
	for (const char16_t c : {u'H', u'i', u'\0', u'Y', u'o', u'\0'}) u16(static_cast<std::uint16_t>(c));
	expected.resize(512 + 8192, std::byte{0});
	CHECK(compiled.bytes == expected);
	CHECK(read32(compiled.bytes, 12) == 8192);
	CHECK(read16(compiled.bytes, 24 + 10) == 6);

	// A full block starts the next one; a line too long for any block is left out; the same token twice counts once.
	LocalizationFile big;
	big.set("one", std::string(3000, 'x'));
	big.set("two", std::string(3000, 'y'));
	big.set("huge", std::string(5000, 'z'));
	big.tokens.push_back({"ONE", "again", {}, {}});
	const auto many = compileCaptions(big);
	CHECK(many.count == 2);
	CHECK(many.skipped == std::vector<std::string>{"huge", "ONE"});
	CHECK(read32(many.bytes, 8) == 2);   // two blocks: 6002 bytes do not fit next to 6002
	CHECK(many.bytes.size() == 512 + 2 * 8192);

	// From a file on disk to the .dat next to it.
	const auto dir = std::filesystem::temp_directory_path() / "sme_caption_test";
	std::filesystem::create_directories(dir);
	std::string error;
	REQUIRE(saveLocalizationFile(dir / "closecaption_english.txt", file, &error));
	int count = 0;
	REQUIRE(compileCaptionFile(dir / "closecaption_english.txt", &error, &count));
	CHECK(count == 2);
	CHECK(std::filesystem::file_size(dir / "closecaption_english.dat") == 512 + 8192);
	std::filesystem::remove_all(dir);
}

TEST_CASE("the menu editor changes only chapter titles in the localization file") {
	const auto dir = std::filesystem::temp_directory_path() / "sme_menu_texts" / "mymod";
	std::filesystem::remove_all(dir.parent_path());
	std::filesystem::create_directories(dir / "cfg");
	std::filesystem::create_directories(dir / "resource");
	{
		std::ofstream out(dir / "gameinfo.txt");
		out << "\"GameInfo\"\n{\n\tgame \"My Mod\"\n\tFileSystem\n\t{\n\t}\n}\n";
	}
	{
		std::ofstream out(dir / "cfg" / "chapter1.cfg");
		out << "map start\n";
	}
	std::string error;
	REQUIRE(saveLocalizationFile(dir / "resource" / "mymod_english.txt", parseLocalizationFile(FILE_TEXT), &error));
	auto menu = loadModMenu(dir);
	REQUIRE(menu.chapters.size() == 1);
	CHECK(menu.chapters[0].title == "The \"Beginning\"");
	// Meanwhile the Texts tab adds a token.
	auto file = *loadLocalizationFile(dir / "resource" / "mymod_english.txt");
	file.set("MyMod_Later", "Added later");
	REQUIRE(saveLocalizationFile(dir / "resource" / "mymod_english.txt", file, &error));
	menu.chapters[0].title = "Arrival";
	const auto result = saveModMenu(dir, menu);
	CHECK(result.error.empty());
	const auto saved = *loadLocalizationFile(dir / "resource" / "mymod_english.txt");
	CHECK(saved.get("MYMOD_Chapter1_Title") == "Arrival");
	CHECK(saved.get("MyMod_Later") == "Added later");
	CHECK(saved.tokens[0].comments == std::vector<std::string>{"// The menu"});
	REQUIRE(saved.find("MyMod_Console") >= 0);
	CHECK(saved.tokens[static_cast<std::size_t>(saved.find("MyMod_Console"))].condition == "[$X360]");
	std::filesystem::remove_all(dir.parent_path());
}
