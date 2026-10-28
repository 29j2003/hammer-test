# Test sources contain UTF-8 text (umlauts in expected strings): MSVC must read them as UTF-8.
if(MSVC)
	add_compile_options(/utf-8)
endif()

# Unit tests (doctest). The fixture library also feeds sme-make-fixture,
# which writes the fake Steam installation to a folder for manual tests and screenshots.
add_library(sme_fixture STATIC demo.cpp fixture.cpp)
target_include_directories(sme_fixture PUBLIC .)
target_link_libraries(sme_fixture PUBLIC smecore PRIVATE sourcepp::vpkpp sourcepp::vtfpp)

add_executable(sme_tests main.cpp test_assets.cpp test_core.cpp test_editing.cpp
	test_csg.cpp test_displacement.cpp test_map.cpp test_shipping.cpp test_mod_template.cpp ../src/probe/report.cpp test_probe.cpp test_git.cpp test_workflow.cpp test_groups.cpp test_mod_menu.cpp test_sound_edit.cpp)
target_include_directories(sme_tests PRIVATE ../src/probe)
target_link_libraries(sme_tests PRIVATE sme_fixture smecore doctest::doctest)
add_test(NAME sme_tests COMMAND sme_tests)

add_executable(sme-make-fixture make_fixture.cpp)
target_link_libraries(sme-make-fixture PRIVATE sme_fixture)

# UI tests: drive the real editor window with simulated mouse input (needs OpenGL 3.3;
# on Linux CI they run under Xvfb with Mesa).
if(SME_BUILD_APP)
	find_package(Qt6 REQUIRED COMPONENTS Test)
	add_executable(sme_app_tests test_app.cpp)
	target_link_libraries(sme_app_tests PRIVATE smeapp sme_fixture doctest::doctest Qt6::Test)
	add_test(NAME sme_app_tests COMMAND sme_app_tests)
	set_tests_properties(sme_app_tests PROPERTIES LABELS "ui")
endif()

# Exports the demo map for the interactive browser preview (development tool, not shipped).
add_executable(sme-export-web export_web.cpp)
target_link_libraries(sme-export-web PRIVATE sme_fixture)
