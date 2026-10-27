// test_probe.cpp - The setup report must be valid-looking JSON and never contain personal path parts.

#include <doctest/doctest.h>

#include "fixture.h"
#include "report.h"

#include <cstdlib>

using namespace sme;

TEST_CASE("probe report masks user names and describes games") {
	CHECK(probe::maskPersonal("C:/Users/Julius/Desktop/x") == "C:/Users/<user>/Desktop/x");
	CHECK(probe::maskPersonal("C:\\Users\\julius\\x") == "C:\\Users\\<user>\\x");
	CHECK(probe::maskPersonal("/home/someone/.steam") == "/home/<user>/.steam");

	test::Fixture f;
	test::createFixture(f);
	const auto report = probe::buildReport(discoverGames(SteamInstallation::load(f.steam)));
	CHECK(report.front() == '{');
	CHECK(report.find("\"sourcemods/mymod\"") != std::string::npos);
	CHECK(report.find("\"mapbase\": true") != std::string::npos);
	CHECK(report.find("\"nodraw_decoded\": \"32x32\"") != std::string::npos);
	CHECK(report.find("shared_misc") != std::string::npos);
	if (const char* home = std::getenv("HOME"); home && *home && std::string(home).size() > 1) {
		CHECK(report.find(std::string(home) + "/") == std::string::npos);
	}
}
