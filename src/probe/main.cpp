// main.cpp - sme-probe: command line tool to check what the editor sees on this PC.
//
// "sme-probe report" writes a JSON report about Steam, games, mods, search paths, FGDs and
// Hammer configs (personal path parts masked). The other commands inspect single files.

#include "report.h"

#include <sme/assets.h>
#include <sme/audio.h>
#include <sme/brush.h>
#include <sme/entity_catalog.h>
#include <sme/game.h>
#include <sme/map.h>
#include <sme/scene_export.h>
#include <sme/studio_model.h>
#include <sme/material.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <iostream>
#include <string>
#include <vector>

using namespace sme;

namespace {

constexpr const char* USAGE = R"(sme-probe - shows what the map editor finds on this PC

Usage:
  sme-probe report [--out FILE]               JSON report of Steam, games, mods, FGDs (paths masked)
  sme-probe games                             list Source 1 games and mods
  sme-probe mounts  GAME                      search order of a game (folders and VPKs)
  sme-probe find    GAME PATH                 which mount provides a file
  sme-probe ls      GAME DIR [EXT]            list files, e.g.: ls hl2 materials/concrete .vmt
  sme-probe material GAME NAME                show a material (patch materials resolved)
  sme-probe model GAME PATH                   load a model (.mdl/.vvd/.vtx): meshes, materials, size, animations
  sme-probe export GAME FILE.vmf OUT          export the map as .gltf or .obj (with textures and models)
  sme-probe texture GAME NAME OUT.png [--max N]  decode a texture to PNG
  sme-probe fgd FILE.fgd... [--class NAME]    load FGDs, optionally show one class
  sme-probe vmf FILE.vmf                      check a map: load, save again, compare, find broken brushes
  sme-probe sound FILE [--out RAW]            decode a sound file (WAV/MP3/OGG): format, length, loop

GAME is a key from "games" (e.g. 220/hl2, sourcemods/mymod) or a folder with gameinfo.txt.
Options: --steam DIR uses another Steam folder (also: environment variable SME_STEAM_ROOT).
)";

struct Args {
	std::vector<std::string> positional;
	std::string steamRoot;
	std::string out;
	std::string className;
	unsigned maxSize = 0;
};

Args parseArgs(int argc, char** argv) {
	Args args;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		const auto next = [&]() -> std::string {
			return i + 1 < argc ? argv[++i] : std::string{};
		};
		if (arg == "--steam") {
			args.steamRoot = next();
		} else if (arg == "--out") {
			args.out = next();
		} else if (arg == "--class") {
			args.className = next();
		} else if (arg == "--max") {
			args.maxSize = static_cast<unsigned>(std::stoul(next()));
		} else {
			args.positional.push_back(arg);
		}
	}
	return args;
}

DiscoveryResult discover(const Args& args) {
	return discoverGames(args.steamRoot.empty() ? SteamInstallation::detect() : SteamInstallation::load(args.steamRoot));
}

/// Finds a game by key, or treats the argument as a folder with gameinfo.txt.
std::unique_ptr<LoadedGame> openGame(const DiscoveryResult& discovery, const std::string& id) {
	const GameEntry* entry = nullptr;
	for (const auto& game : discovery.games) {
		if (iequals(game.key(), id) || iequals(displayPath(game.gameDir), displayPath(toNativePath(id)))) {
			entry = &game;
			break;
		}
	}
	GameEntry adhoc;
	if (!entry) {
		adhoc.gameDir = toNativePath(id);
		adhoc.title = displayPath(adhoc.gameDir.filename());
		entry = &adhoc;
	}
	std::string error;
	auto game = loadGame(*entry, discovery.steam, entry->fgds, &error);
	if (!game) {
		std::cerr << "error: " << error << "\n";
	}
	return game;
}

int cmdGames(const Args& args) {
	const auto discovery = discover(args);
	if (!discovery.steam.found()) {
		std::cerr << "Steam not found (use --steam DIR)\n";
		return 1;
	}
	for (const auto& game : discovery.games) {
		std::cout << game.key() << "  \"" << game.title << "\"";
		if (!game.branch.empty()) {
			std::cout << "  [" << game.branch << "]";
		}
		std::cout << "\n    " << probe::maskPersonal(displayPath(game.gameDir)) << "\n";
	}
	if (discovery.games.empty()) {
		std::cout << "no Source 1 games found\n";
	}
	return 0;
}

int cmdMounts(const Args& args) {
	const auto discovery = discover(args);
	const auto game = openGame(discovery, args.positional.at(1));
	if (!game) {
		return 1;
	}
	const auto& mounts = game->files.mounts();
	for (std::size_t i = 0; i < mounts.size(); ++i) {
		const auto& mount = mounts[i];
		std::string ids;
		for (const auto& id : mount.pathIds) {
			ids += (ids.empty() ? "" : "+") + id;
		}
		std::cout << (i + 1) << ". " << (mount.kind == Mount::Kind::Vpk ? "vpk " : "dir ") << ids << "  "
		          << probe::maskPersonal(displayPath(mount.path));
		if (mount.present) {
			std::cout << "  (" << game->files.fileCount(i) << " files)";
		} else {
			std::cout << "  MISSING: " << mount.problem;
		}
		std::cout << "\n";
	}
	return 0;
}

int cmdFind(const Args& args) {
	const auto discovery = discover(args);
	const auto game = openGame(discovery, args.positional.at(1));
	if (!game) {
		return 1;
	}
	const auto index = game->files.locate(args.positional.at(2), "");
	if (!index) {
		std::cout << "not found\n";
		return 1;
	}
	std::cout << probe::maskPersonal(displayPath(game->files.mounts()[*index].path)) << "\n";
	return 0;
}

int cmdList(const Args& args) {
	const auto discovery = discover(args);
	const auto game = openGame(discovery, args.positional.at(1));
	if (!game) {
		return 1;
	}
	const auto extension = args.positional.size() > 3 ? args.positional[3] : std::string{};
	for (const auto& file : game->files.list(args.positional.at(2), extension)) {
		std::cout << file << "\n";
	}
	return 0;
}

int cmdMaterial(const Args& args) {
	const auto discovery = discover(args);
	const auto game = openGame(discovery, args.positional.at(1));
	if (!game) {
		return 1;
	}
	std::string error;
	const auto material = loadMaterial(game->files, args.positional.at(2), &error);
	if (!material) {
		std::cerr << "error: " << error << "\n";
		return 1;
	}
	std::cout << material->name << "\n  shader: " << material->shader << "\n  preview: " << material->previewTexture()
	          << "\n  tool: " << (material->isTool() ? "yes" : "no") << "\n";
	for (const auto& include : material->includeChain) {
		std::cout << "  patches: " << include << "\n";
	}
	for (const auto& [key, value] : material->params) {
		std::cout << "  " << key << " = " << value << "\n";
	}
	return 0;
}

int cmdModel(const Args& args) {
	const auto discovery = discover(args);
	const auto game = openGame(discovery, args.positional.at(1));
	if (!game) {
		return 1;
	}
	const auto& path = args.positional.at(2);
	std::string error;
	const auto model = loadModelGeometry(game->files, path, 0, &error);
	if (!model) {
		std::cerr << "error: " << error << "\n";
		return 1;
	}
	const Vec3 size = model->bounds.size();
	std::cout << path << "\n  vertices: " << model->vertices.size() << "\n  triangles: " << model->triangleCount() << "\n  size: " << size.x << " x "
	          << size.y << " x " << size.z << "\n";
	for (const auto& mesh : model->meshes) {
		std::cout << "  mesh: " << mesh.indexCount / 3 << " triangles, material " << mesh.material
		          << (game->files.exists("materials/" + mesh.material + ".vmt") ? "" : " (missing)") << "\n";
	}
	const auto sequences = modelSequences(game->files, path);
	std::cout << "  animations: " << sequences.size() << "\n";
	return 0;
}

int cmdExport(const Args& args) {
	const auto discovery = discover(args);
	const auto game = openGame(discovery, args.positional.at(1));
	if (!game) {
		return 1;
	}
	const auto text = readFileText(toNativePath(args.positional.at(2)));
	if (!text) {
		std::cerr << "error: cannot read " << args.positional.at(2) << "\n";
		return 1;
	}
	try {
		const auto doc = MapDocument::parse(*text);
		ExportOptions options;
		const std::filesystem::path out = toNativePath(args.positional.at(3));
		options.format = endsWithI(displayPath(out), ".obj") ? ExportFormat::Obj : ExportFormat::Gltf;
		std::string error;
		const auto result = exportMap(doc, &game->entities, &game->files, out, options, &error);
		if (!result) {
			std::cerr << "error: " << error << "\n";
			return 1;
		}
		std::cout << result->triangles << " triangles, " << result->materials << " materials, " << result->textures << " textures, " << result->models
		          << " models\n";
		for (const auto& warning : result->warnings) {
			std::cout << "warning: " << warning << "\n";
		}
	} catch (const std::exception& e) {
		std::cerr << "error: " << e.what() << "\n";
		return 1;
	}
	return 0;
}

int cmdTexture(const Args& args) {
	const auto discovery = discover(args);
	const auto game = openGame(discovery, args.positional.at(1));
	if (!game) {
		return 1;
	}
	std::string error;
	const auto data = game->files.read("materials/" + normalizeMaterialName(args.positional.at(2)) + ".vtf");
	if (!data) {
		std::cerr << "error: texture not found\n";
		return 1;
	}
	if (const auto info = readTextureInfo(*data)) {
		std::cout << info->width << "x" << info->height << " " << info->format << ", " << info->mipCount << " mips, "
		          << info->frameCount << " frames\n";
	}
	const auto image = decodeTexture(*data, args.maxSize, &error);
	if (!image) {
		std::cerr << "error: " << error << "\n";
		return 1;
	}
	const auto png = encodePng(*image);
	std::ofstream out(toNativePath(args.positional.at(3)), std::ios::binary);
	out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
	std::cout << "wrote " << image->width << "x" << image->height << " PNG\n";
	return out ? 0 : 1;
}

int cmdFgd(const Args& args) {
	std::vector<std::filesystem::path> files;
	for (std::size_t i = 1; i < args.positional.size(); ++i) {
		files.push_back(toNativePath(args.positional[i]));
	}
	EntityCatalog catalog;
	std::vector<std::string> errors;
	catalog.load(files, &errors);
	std::cout << catalog.size() << " classes, " << catalog.placeable().size() << " placeable\n";
	for (const auto& error : errors) {
		std::cout << "  problem: " << error << "\n";
	}
	if (!args.className.empty()) {
		const auto* entity = catalog.find(args.className);
		if (!entity) {
			std::cerr << "class not found\n";
			return 1;
		}
		std::cout << entity->classType << " " << entity->name << "\n  " << entity->description << "\n";
		for (const auto& helper : entity->helpers) {
			std::cout << "  helper " << helper.name << "(" << helper.arguments << ")\n";
		}
		for (const auto& property : entity->properties) {
			std::cout << "  key " << property.name << " (" << property.type << ") \"" << property.displayName << "\" = " << property.defaultValue << "\n";
		}
		for (const auto& io : entity->inputs) {
			std::cout << "  input " << io.name << "(" << io.type << ")\n";
		}
		for (const auto& io : entity->outputs) {
			std::cout << "  output " << io.name << "(" << io.type << ")\n";
		}
	}
	return errors.empty() || catalog.size() > 0 ? 0 : 1;
}

int cmdSound(const Args& args) {
	std::ifstream file(toNativePath(args.positional.at(1)), std::ios::binary);
	if (!file) {
		std::cerr << "error: cannot read file\n";
		return 1;
	}
	const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::string error;
	const auto audio = decodeAudio(std::as_bytes(std::span(bytes.data(), bytes.size())), args.positional.at(1), &error);
	if (!audio) {
		std::cerr << "error: " << error << "\n";
		return 1;
	}
	std::cout << audio->format << ", " << audio->sampleRate << " Hz, " << audio->channels << " channel(s), " << audio->seconds() << " s";
	if (audio->loopStart) {
		std::cout << ", loops from sample " << *audio->loopStart;
	}
	std::cout << "\n";
	if (!args.out.empty()) {
		// Raw 16-bit little-endian samples, interleaved (for comparing with other decoders).
		std::ofstream raw(toNativePath(args.out), std::ios::binary);
		raw.write(reinterpret_cast<const char*>(audio->samples.data()), static_cast<std::streamsize>(audio->samples.size() * 2));
	}
	return 0;
}

int cmdVmf(const Args& args) {
	const auto text = readFileText(toNativePath(args.positional.at(1)));
	if (!text) {
		std::cerr << "error: cannot read file\n";
		return 1;
	}
	VmfLoadReport report;
	const auto doc = MapDocument::parse(*text, &report);
	for (const auto& note : report.notes) {
		std::cout << "  repaired: " << note << "\n";
	}
	std::size_t sides = 0, displacements = 0, invalid = 0, connections = doc.world.connections.size();
	const auto scan = [&](const Entity& entity) {
		for (const auto& solid : entity.solids) {
			sides += solid.sides.size();
			for (const auto& side : solid.sides) {
				displacements += side.dispInfo() ? 1 : 0;
			}
			if (!isConvexValid(solid)) {
				++invalid;
				std::cout << "  brush " << solid.id << " is not a valid convex solid\n";
			}
		}
	};
	scan(doc.world);
	for (const auto& entity : doc.entities) {
		scan(entity);
		connections += entity.connections.size();
	}
	// Saving without an edit must give the file back: byte for byte, or at least every key and value.
	const auto written = doc.serialize();
	const bool identical = written == *text;
	std::vector<std::string> differences;
	try {
		differences = keyValueDifferences(*text, written);
	} catch (const std::exception&) {
		differences.push_back("the file itself does not read as KeyValues (repaired while loading, see above)");
	}
	const bool stable = differences.empty() && MapDocument::parse(written).serialize() == written;
	std::cout << doc.solidCount() << " brushes, " << sides << " faces, " << displacements << " displacements, "
	          << doc.entities.size() << " entities, " << connections << " outputs\n"
	          << invalid << " invalid brushes\n"
	          << "save/load round trip: "
	          << (identical ? "stable (identical, byte for byte)" : stable ? "stable (same keys and values; spacing differs)" : "CHANGES DATA")
	          << "\n";
	for (const auto& line : differences) {
		std::cout << "  " << line << "\n";
	}
	return stable ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
	const auto args = parseArgs(argc, argv);
	if (args.positional.empty()) {
		std::cout << USAGE;
		return 1;
	}
	const auto& command = args.positional[0];
	const auto need = [&](std::size_t count) {
		if (args.positional.size() < count) {
			std::cerr << USAGE;
			return false;
		}
		return true;
	};
	try {
		if (command == "report") {
			const auto report = probe::buildReport(discover(args));
			if (args.out.empty()) {
				std::cout << report;
			} else {
				std::ofstream(toNativePath(args.out), std::ios::binary) << report;
				std::cout << "report written to " << args.out << "\n";
			}
			return 0;
		}
		if (command == "games") return cmdGames(args);
		if (command == "mounts" && need(2)) return cmdMounts(args);
		if (command == "find" && need(3)) return cmdFind(args);
		if (command == "ls" && need(3)) return cmdList(args);
		if (command == "material" && need(3)) return cmdMaterial(args);
		if (command == "model" && need(3)) return cmdModel(args);
		if (command == "export" && need(4)) return cmdExport(args);
		if (command == "texture" && need(4)) return cmdTexture(args);
		if (command == "fgd" && need(2)) return cmdFgd(args);
		if (command == "vmf" && need(2)) return cmdVmf(args);
		if (command == "sound" && need(2)) return cmdSound(args);
	} catch (const std::exception& e) {
		std::cerr << "error: " << e.what() << "\n";
		return 2;
	}
	std::cerr << USAGE;
	return 1;
}
