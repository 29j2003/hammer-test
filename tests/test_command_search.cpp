// test_command_search.cpp - The command search (Ctrl+K): every word must fit, best entries first.

#include <doctest/doctest.h>

#include <sme/command_search.h>

using namespace sme;

TEST_CASE("command search: every word must appear") {
	CHECK(commandMatchScore("road", "Build road ...") > 0);
	CHECK(commandMatchScore("road bu", "Build road ...") > 0);
	CHECK(commandMatchScore("rd bld", "Build road ...") == -1);
	CHECK(commandMatchScore("", "Anything") == 0);
	CHECK(commandMatchScore("ROAD", "build road") > 0);
	// Where the entry lives counts too ("brushes fill" finds Brushes > Fill gap).
	CHECK(commandMatchScore("brushes fill", "Fill gap ...", "Brushes") > 0);
	CHECK(commandMatchScore("brushes fill", "Fill gap ...", "Map") == -1);
}

TEST_CASE("command search: the name's start and word starts rank first") {
	const std::vector<std::string> names{"Texture flow ...", "Road texture ...", "Build road ...", "Toolsnodraw road", "Undo"};
	const auto ranked = rankCommands("road", names);
	REQUIRE(ranked.size() == 3);
	CHECK(names[ranked[0]] == "Road texture ...");   // starts with it
	const auto exact = rankCommands("undo", {"Undo history", "Undo"});
	CHECK(exact.front() == 1);
	// Inside a word ranks below a word start.
	CHECK(commandMatchScore("ure", "Measure") < commandMatchScore("mea", "Measure"));
	// Limit and order of equal scores.
	const auto limited = rankCommands("a", {"a1", "a2", "a3"}, {}, 2);
	REQUIRE(limited.size() == 2);
	CHECK(limited[0] == 0);
	CHECK(limited[1] == 1);
}
