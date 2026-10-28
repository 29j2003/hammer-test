# Third-party sound decoders (C, compiled as they are, without our warning flags).
add_library(smecodecs STATIC src/codecs.c src/codecs_mp3.c)
target_include_directories(smecodecs SYSTEM PRIVATE ${stb_vorbis_SOURCE_DIR} ${minimp3_SOURCE_DIR})
set_target_properties(smecodecs PROPERTIES POSITION_INDEPENDENT_CODE ON)

# smecore: everything that does not need Qt (file formats, games, map document, geometry, undo).
add_library(smecore STATIC
	src/asset_import.cpp
	src/assets.cpp
	src/audio.cpp
	src/brush.cpp
	src/checks.cpp
	src/layout_checks.cpp
	src/map_helpers.cpp
	src/compile_log.cpp
	src/compile_options.cpp
	src/csg.cpp
	src/cubemaps.cpp
	src/decals.cpp
	src/fidelity_effects.cpp
	src/displacement.cpp
	src/clipboard.cpp
	src/visibility.cpp
	src/fidelity_look.cpp
	src/sky_presets.cpp
	src/autosave.cpp
	src/entity_search.cpp
	src/asset_refs.cpp
	src/material_replace.cpp
	src/soundscape.cpp
	src/sound_categories.cpp
	src/sound_edit.cpp
	src/shipping.cpp
	src/visgroups.cpp
	src/instances.cpp
	src/mod_template.cpp
	src/mod_code.cpp
	src/mod_install.cpp
	src/mod_features.cpp
	src/mod_menu.cpp
	src/model_import.cpp
	src/localization.cpp
	src/editing.cpp
	src/entity_catalog.cpp
	src/filesystem.cpp
	src/game.cpp
	src/game_discovery.cpp
	src/gameinfo.cpp
	src/git_status.cpp
	src/hammer_config.cpp
	src/heightmap.cpp
	src/relief_pattern.cpp
	src/history.cpp
	src/keyvalues.cpp
	src/map.cpp
	src/map_budget.cpp
	src/map_tools.cpp
	src/recent_projects.cpp
	src/soundscape_area.cpp
	src/doors.cpp
	src/io_links.cpp
	src/particles.cpp
	src/material.cpp
	src/scene_export.cpp
	src/shapes.cpp
	src/steam.cpp
	src/studio_model.cpp src/studio_anim.cpp src/animation_use.cpp src/animation_import.cpp
	src/text.cpp
	src/texture.cpp
	src/version.cpp)
target_include_directories(smecore PUBLIC include)
target_include_directories(smecore SYSTEM PRIVATE ${stb_vorbis_SOURCE_DIR} ${minimp3_SOURCE_DIR})
target_link_libraries(smecore
	PUBLIC sourcepp::kvpp
	PRIVATE sourcepp::toolpp sourcepp::vpkpp sourcepp::vtfpp smecodecs)
target_compile_features(smecore PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(smecore PRIVATE /W4 /permissive- /utf-8)
else()
	target_compile_options(smecore PRIVATE -Wall -Wextra -Wpedantic)
endif()
