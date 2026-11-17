// fuzz_main.cpp - The libFuzzer entry point of one target: built once per target with
// SME_FUZZ_TARGET set to a function of fuzz_targets.h (see tests/fuzz/CMakeLists.txt).

#include "fuzz_targets.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	sme::fuzz::SME_FUZZ_TARGET(data, size);
	return 0;
}
