// make_samples.cpp - Writes Mapsmith's sample files (sme/samples.h) to a folder: the "samples"
// folder that ships next to Mapsmith.
//   sme-make-samples DIR [--media DIR]     the samples; MP3/Ogg copied from --media
//   sme-make-samples --media-sources DIR   the WAVs tools/make_sample_media.sh encodes to MP3/Ogg

#include <sme/samples.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
	std::filesystem::path out, media, sources;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--media" && i + 1 < argc) media = argv[++i];
		else if (arg == "--media-sources" && i + 1 < argc) sources = argv[++i];
		else if (out.empty() && !arg.starts_with("--")) out = arg;
		else {
			std::cerr << "usage: sme-make-samples DIR [--media DIR] | --media-sources DIR\n";
			return 1;
		}
	}
	if (!sources.empty()) {
		std::filesystem::create_directories(sources);
		for (const auto& file : sme::sampleMediaSources()) {
			std::ofstream(sources / file.path, std::ios::binary).write(reinterpret_cast<const char*>(file.data.data()), std::streamsize(file.data.size()));
		}
		std::cout << "media sources written to " << sources.generic_string() << "\n";
		return 0;
	}
	if (out.empty()) {
		std::cerr << "usage: sme-make-samples DIR [--media DIR] | --media-sources DIR\n";
		return 1;
	}
	std::string error;
	if (!sme::writeSamples(out, media, &error)) {
		std::cerr << error << "\n";
		return 1;
	}
	std::cout << "samples written to " << out.generic_string() << "\n";
	return 0;
}
