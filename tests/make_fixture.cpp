// make_fixture.cpp - Writes the test fixture (fake Steam with HL2, SDK base, Mapbase-style mod) plus the
// demo content and demo map (<mod>/mapsrc/demo.vmf) to a folder.
//   sme-make-fixture DIR    then e.g.: SME_STEAM_ROOT=DIR/Steam sme-probe games

#include "fixture.h"

#include <iostream>

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "usage: sme-make-fixture DIR\n";
		return 1;
	}
	sme::test::Fixture fixture;
	sme::test::createFixture(fixture, argv[1]);
	sme::test::createDemo(fixture);
	std::cout << "fixture written; Steam root: " << fixture.steam.generic_string() << "\n";
	return 0;
}
