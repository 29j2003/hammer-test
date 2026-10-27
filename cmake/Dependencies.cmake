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
