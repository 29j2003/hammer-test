# The Claude integration's shared part (Qt Core and Network only) and the bridge program
# mapsmith-mcp that Claude Desktop / Claude Code start (see claude_protocol.h).
find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(smeclaude STATIC claude_protocol.cpp)
target_include_directories(smeclaude PUBLIC .)
target_link_libraries(smeclaude PUBLIC Qt6::Core Qt6::Network)

qt_add_executable(mapsmith-mcp main.cpp)
target_compile_definitions(mapsmith-mcp PRIVATE SME_VERSION="${PROJECT_VERSION}")
target_link_libraries(mapsmith-mcp PRIVATE smeclaude)
# A console program (Claude talks to it over stdin/stdout), next to Mapsmith's executable so the
# editor finds it for "Connect Claude Desktop".
set_target_properties(mapsmith-mcp PROPERTIES
	WIN32_EXECUTABLE OFF
	MACOSX_BUNDLE OFF
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/app")

foreach(target smeclaude mapsmith-mcp)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra)
	endif()
endforeach()
