# The Qt editor application. Qt 6.4+ (CI builds with 6.8).
# Everything except main.cpp is a library so the UI tests can drive the real windows.
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Concurrent Network)
# Sound preview in the Sounds tab (optional: without it the editor builds, but cannot play sounds).
find_package(Qt6 QUIET COMPONENTS Multimedia)
set(CMAKE_AUTOMOC ON)

add_library(smeapp STATIC
	asset_browser.cpp
	compile.cpp
	compile_window.cpp
	copy_paste.cpp
	editor.cpp
	main_window.cpp
	material_cache.cpp
	map_look_panel.cpp
	material_editor.cpp
	material_preview.cpp
	model_cache.cpp
	model_thumbnails.cpp
	panels.cpp
	particle_browser.cpp
	properties_panel.cpp
	renderer.cpp
	renderer_shadows.cpp
	shortcut_dialog.cpp
	sound_browser.cpp
	sound_player.cpp
	style.cpp
	tool_panel.cpp
	toggle_switch.cpp
	updater.cpp
	viewport.cpp
	viewport_tools.cpp)
target_include_directories(smeapp PUBLIC .)
target_compile_definitions(smeapp PUBLIC SME_VERSION="${PROJECT_VERSION}" _USE_MATH_DEFINES)
target_link_libraries(smeapp PUBLIC smecore Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Concurrent Qt6::Network)
if(TARGET Qt6::Multimedia)
	target_link_libraries(smeapp PUBLIC Qt6::Multimedia)
	target_compile_definitions(smeapp PUBLIC SME_HAVE_MULTIMEDIA)
	message(STATUS "Sound preview: Qt Multimedia")
else()
	message(STATUS "Sound preview: off (Qt Multimedia not found)")
endif()

qt_add_executable(SourceMapEditor main.cpp)
set_target_properties(SourceMapEditor PROPERTIES
	WIN32_EXECUTABLE ON
	MACOSX_BUNDLE ON
	OUTPUT_NAME "SourceMapEditor")
target_link_libraries(SourceMapEditor PRIVATE smeapp)

foreach(target smeapp SourceMapEditor)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra)
	endif()
endforeach()
