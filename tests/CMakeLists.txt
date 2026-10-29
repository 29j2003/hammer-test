# Test sources contain UTF-8 text (umlauts in expected strings): MSVC must read them as UTF-8.
if(MSVC)
	add_compile_options(/utf-8)
endif()

# Unit tests (doctest). The fixture library also feeds sme-make-fixture,
# which writes the fake Steam installation to a folder for manual tests and screenshots.
add_library(sme_fixture STATIC demo.cpp fixture.cpp)
target_include_directories(sme_fixture PUBLIC .)
target_link_libraries(sme_fixture PUBLIC smecore PRIVATE sourcepp::vpkpp sourcepp::vtfpp)

add_executable(sme_tests main.cpp test_assets.cpp test_samples.cpp test_fuzz.cpp test_performance.cpp test_studio_anim.cpp test_animation_import.cpp test_npc_scripts.cpp test_relief_pattern.cpp test_core.cpp test_editing.cpp
	test_csg.cpp test_asset_refs.cpp test_cubemaps.cpp test_fidelity_effects.cpp test_decals.cpp test_displacement.cpp test_map.cpp test_shipping.cpp test_mod_template.cpp ../src/probe/report.cpp test_probe.cpp test_git.cpp test_workflow.cpp test_groups.cpp test_mod_menu.cpp test_sound_edit.cpp test_localization.cpp test_doors.cpp test_map_tools.cpp test_mod_code.cpp test_asset_import.cpp test_mod_features.cpp test_mod_install.cpp test_layout.cpp)
target_include_directories(sme_tests PRIVATE ../src/probe)
target_link_libraries(sme_tests PRIVATE sme_fixture smecore doctest::doctest)
target_compile_definitions(sme_tests PRIVATE SME_SAMPLE_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples_media")
add_test(NAME sme_tests COMMAND sme_tests)

add_executable(sme-make-fixture make_fixture.cpp)
target_link_libraries(sme-make-fixture PRIVATE sme_fixture)

# The sample files that ship next to Mapsmith (sme/samples.h): generated into <build>/samples,
# where a build of Mapsmith finds them ("Use a sample ..."); the Windows package copies them.
add_executable(sme-make-samples make_samples.cpp)
target_link_libraries(sme-make-samples PRIVATE smecore)
file(GLOB SME_SAMPLE_MEDIA CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/samples_media/*)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/samples/README.txt
	COMMAND sme-make-samples ${CMAKE_BINARY_DIR}/samples --media ${CMAKE_CURRENT_SOURCE_DIR}/samples_media
	DEPENDS sme-make-samples ${SME_SAMPLE_MEDIA}
	COMMENT "Writing Mapsmith's sample files")
add_custom_target(samples ALL DEPENDS ${CMAKE_BINARY_DIR}/samples/README.txt)

# UI tests: drive the real editor window with simulated mouse input (needs OpenGL 3.3;
# on Linux CI they run under Xvfb with Mesa).
if(SME_BUILD_APP)
	find_package(Qt6 REQUIRED COMPONENTS Test)
	add_executable(sme_app_tests test_app.cpp)
	set_target_properties(sme_app_tests PROPERTIES AUTOMOC ON)   # ExternalOpenTrap
	target_link_libraries(sme_app_tests PRIVATE smeapp sme_fixture doctest::doctest Qt6::Test)
	add_test(NAME sme_app_tests COMMAND sme_app_tests)
	set_tests_properties(sme_app_tests PROPERTIES LABELS "ui")
endif()

# Exports the demo map for the interactive browser preview (development tool, not shipped).
add_executable(sme-export-web export_web.cpp)
target_link_libraries(sme-export-web PRIVATE sme_fixture)
