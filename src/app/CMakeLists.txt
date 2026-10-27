# The Qt editor application. Qt 6.4+ (CI builds with 6.8).
# Everything except main.cpp is a library so the UI tests can drive the real windows.
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Concurrent)
set(CMAKE_AUTOMOC ON)

add_library(smeapp STATIC
	asset_browser.cpp
	compile.cpp
	editor.cpp
	main_window.cpp
	material_cache.cpp
	panels.cpp
	properties_panel.cpp
	renderer.cpp
	style.cpp
	viewport.cpp)
target_include_directories(smeapp PUBLIC .)
target_compile_definitions(smeapp PUBLIC SME_VERSION="${PROJECT_VERSION}" _USE_MATH_DEFINES)
target_link_libraries(smeapp PUBLIC smecore Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Concurrent)

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
