cmake_minimum_required(VERSION 3.25)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SME_VERSION)
string(STRIP "${SME_VERSION}" SME_VERSION)

project(source_map_editor
	VERSION ${SME_VERSION}
	DESCRIPTION "Modern 3D-first map editor for Source 1 games"
	LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC)
	# getenv & co. are used read-only and on purpose; keep the build log free of C4996 noise.
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
	# Release builds write a .pdb next to the .exe (same optimisation): the crash file lists frames as
	# module+offset, the CI keeps the .pdb ("Mapsmith-windows-pdb") to turn them into functions.
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo,Release>:ProgramDatabase>")
	add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

option(SME_BUILD_APP   "Build the Qt editor application" ON)
option(SME_BUILD_TESTS "Build the unit tests"            ON)
option(SME_FUZZ        "Build the libFuzzer targets in tests/fuzz (clang; AddressSanitizer + UBSan everywhere)" OFF)

if(SME_FUZZ)
	if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		message(FATAL_ERROR "SME_FUZZ needs clang (libFuzzer): -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++")
	endif()
	# Coverage and AddressSanitizer for everything (the readers of sourcepp too); UBSan stops at the
	# first finding in Mapsmith's own code and the fuzz targets (third-party code only reports it).
	add_compile_options(-fsanitize=fuzzer-no-link,address -fno-omit-frame-pointer -g)
	add_link_options(-fsanitize=address,undefined)
	set(SME_FUZZ_UB_FLAGS -fsanitize=undefined -fno-sanitize-recover=undefined -fno-sanitize=vptr)
endif()

include(cmake/Dependencies.cmake)

add_subdirectory(src/core)
if(SME_FUZZ)
	target_compile_options(smecore PRIVATE ${SME_FUZZ_UB_FLAGS})
endif()
add_subdirectory(src/probe)
if(SME_BUILD_APP)
	add_subdirectory(src/claude)
	add_subdirectory(src/app)
endif()

if(SME_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
if(SME_FUZZ)
	add_subdirectory(tests/fuzz)
endif()
