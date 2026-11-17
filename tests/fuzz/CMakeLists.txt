# libFuzzer targets for every reader of foreign files (docs/ENTWICKLUNG.md, "Sicherheit").
# Only with -DSME_FUZZ=ON and clang; the top-level CMakeLists.txt adds the sanitizer flags.
# What each target reads: fuzz_targets.h (the unit tests replay regressions/<target>/ with it).
#
#   build/tests/fuzz/sme-fuzz-seeds corpus --media tests/samples_media
#   build/tests/fuzz/fuzz_vtf corpus/vtf -max_total_time=300

# target folder name -> function in fuzz_targets.h
set(SME_FUZZ_TARGETS vtf=vtf vpk=vpk studio=studio phy=phy pcf=pcf audio=audio image=image bsp=bsp vmf=vmf text=textFormats fgd=fgd zip=zip)
foreach(entry IN LISTS SME_FUZZ_TARGETS)
	string(REPLACE "=" ";" parts "${entry}")
	list(GET parts 0 target)
	list(GET parts 1 function)
	add_executable(fuzz_${target} fuzz_main.cpp)
	target_compile_definitions(fuzz_${target} PRIVATE SME_FUZZ_TARGET=${function})
	target_link_libraries(fuzz_${target} PRIVATE smecore)
	target_compile_options(fuzz_${target} PRIVATE ${SME_FUZZ_UB_FLAGS})
	target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
endforeach()

if(SME_BUILD_TESTS)
	add_executable(sme-fuzz-seeds make_seeds.cpp)
	target_include_directories(sme-fuzz-seeds PRIVATE ..)
	target_link_libraries(sme-fuzz-seeds PRIVATE sme_fixture smecore)
endif()
