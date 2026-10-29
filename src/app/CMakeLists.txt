# The Qt editor application. Qt 6.4+ (CI builds with 6.8).
# Everything except main.cpp is a library so the UI tests can drive the real windows.
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Concurrent Network)
# Sound preview in the Sounds browser (optional: without it the editor builds, but cannot play sounds).
find_package(Qt6 QUIET COMPONENTS Multimedia)
set(CMAKE_AUTOMOC ON)

add_library(smeapp STATIC
	asset_browser.cpp
	autosaver.cpp
	compile.cpp
	compile_window.cpp
	copy_paste.cpp
	editor.cpp
	enhancements.cpp
	find_dialog.cpp
	history_panel.cpp
	main_window.cpp
	material_cache.cpp
	map_look_panel.cpp
	maps_panel.cpp
	menu_editor.cpp
	texts_panel.cpp
	mod_code_panel.cpp
	features_panel.cpp
	mod_page.cpp
	new_mod_dialog.cpp
	icons.cpp
	material_editor.cpp
	material_preview.cpp
	model_cache.cpp
	model_thumbnails.cpp
	model_viewer.cpp
	models_page.cpp animations_page.cpp
	panels.cpp
	particle_browser.cpp
	properties_panel.cpp
	class_picker.cpp samples_folder.cpp wheel_guard.cpp dock_layout.cpp startup_trace.cpp lazy_refresh.cpp dialog_fit.cpp import_dialog.cpp text_copy.cpp start_page.cpp
	replace_materials_dialog.cpp
	properties_instance.cpp
	properties_door.cpp
	connect_popup.cpp
	renderer.cpp
	renderer_cubemaps.cpp
	renderer_shadows.cpp
	ship_panel.cpp
	shortcut_dialog.cpp
	sound_browser.cpp
	sound_player.cpp
	soundscape_editor.cpp
	source_control_panel.cpp
	style.cpp
	tool_panel.cpp
	toggle_switch.cpp
	updater.cpp
	viewport.cpp
	viewport_tools.cpp
	viewport_doors.cpp
	viewport_ropes.cpp
	editor_helpers.cpp
	viewport_displace.cpp
	visgroups_panel.cpp)
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

# Small pictures the style sheet uses (the arrow of drop-down boxes), drawn at twice the size
# they are shown at so they stay sharp on high DPI screens.
qt_add_resources(smeapp "ui"
	PREFIX "/ui"
	BASE resources
	FILES resources/chevron-down.png resources/chevron-down-hover.png)

qt_add_executable(Mapsmith main.cpp)
if(WIN32)
	target_sources(Mapsmith PRIVATE resources/mapsmith.rc)   # program icon
endif()
set_target_properties(Mapsmith PROPERTIES
	WIN32_EXECUTABLE ON
	MACOSX_BUNDLE ON
	OUTPUT_NAME "Mapsmith")
target_link_libraries(Mapsmith PRIVATE smeapp)

foreach(target smeapp Mapsmith)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra)
	endif()
endforeach()
