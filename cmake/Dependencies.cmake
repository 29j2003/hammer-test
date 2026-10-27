# Third-party dependencies, pinned to exact commits (never a branch or a movable tag).
# sourcepp pins its own dependencies to commits as well.
#
# For offline or faster local builds, point FetchContent at an existing checkout:
#   cmake -DFETCHCONTENT_SOURCE_DIR_SOURCEPP=/path/to/sourcepp ...

include(FetchContent)

# sourcepp: Valve file formats (VPK, VTF, KeyValues, FGD, Steam libraries). MIT license.
set(SOURCEPP_LIBS_START_ENABLED OFF CACHE INTERNAL "")
set(SOURCEPP_USE_KVPP   ON CACHE INTERNAL "")
set(SOURCEPP_USE_TOOLPP ON CACHE INTERNAL "")
set(SOURCEPP_USE_VPKPP  ON CACHE INTERNAL "")
set(SOURCEPP_USE_VTFPP  ON CACHE INTERNAL "")
set(SOURCEPP_VTFPP_SUPPORT_EXR  OFF CACHE INTERNAL "")
set(SOURCEPP_VTFPP_SUPPORT_JXL  OFF CACHE INTERNAL "")
set(SOURCEPP_VTFPP_SUPPORT_WEBP OFF CACHE INTERNAL "")
set(SOURCEPP_BUILD_TESTS OFF CACHE INTERNAL "")
# Not needed for VPK/VTF; keeps one more downloaded library out of the build.
set(MZ_BZIP2 OFF CACHE INTERNAL "")
FetchContent_Declare(sourcepp
	GIT_REPOSITORY https://github.com/craftablescience/sourcepp.git
	GIT_TAG        1540277f99bacee288f6786a66ee182aab6c146b
	GIT_SHALLOW    FALSE
	GIT_SUBMODULES_RECURSE TRUE
	SYSTEM)
FetchContent_MakeAvailable(sourcepp)

# Sound decoders for the sound preview: single files, pinned to a commit and checked by hash.
# stb_vorbis (Ogg Vorbis): public domain / MIT. minimp3 (MP3): CC0.
FetchContent_Declare(stb_vorbis
	URL      https://raw.githubusercontent.com/nothings/stb/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5/stb_vorbis.c
	URL_HASH SHA256=4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636
	DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_Declare(minimp3
	URL      https://raw.githubusercontent.com/lieff/minimp3/ea99364f61c14656440e8d77e9c233ccf3124633/minimp3.h
	URL_HASH SHA256=57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad
	DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(stb_vorbis minimp3)

if(SME_BUILD_TESTS)
	# doctest: single-header unit test framework. MIT license.
	FetchContent_Declare(doctest
		GIT_REPOSITORY https://github.com/doctest/doctest.git
		GIT_TAG        1da23a3e8119ec5cce4f9388e91b065e20bf06f5 # v2.4.12
		GIT_SHALLOW    FALSE
		SYSTEM)
	set(DOCTEST_NO_INSTALL ON CACHE INTERNAL "")
	FetchContent_MakeAvailable(doctest)
endif()
