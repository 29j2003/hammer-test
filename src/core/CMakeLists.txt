# smecore: everything that does not need Qt (file formats, games, map document, geometry, undo).
add_library(smecore STATIC
	src/assets.cpp
	src/brush.cpp
	src/checks.cpp
	src/editing.cpp
	src/entity_catalog.cpp
	src/filesystem.cpp
	src/game.cpp
	src/game_discovery.cpp
	src/gameinfo.cpp
	src/hammer_config.cpp
	src/history.cpp
	src/keyvalues.cpp
	src/map.cpp
	src/material.cpp
	src/steam.cpp
	src/studio_model.cpp
	src/text.cpp
	src/texture.cpp)
target_include_directories(smecore PUBLIC include)
target_link_libraries(smecore
	PUBLIC sourcepp::kvpp
	PRIVATE sourcepp::toolpp sourcepp::vpkpp sourcepp::vtfpp)
target_compile_features(smecore PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(smecore PRIVATE /W4 /permissive- /utf-8)
else()
	target_compile_options(smecore PRIVATE -Wall -Wextra -Wpedantic)
endif()
