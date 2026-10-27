// shipping.cpp - Map dependencies, packages and Workshop items (see shipping.h).

#include <sme/shipping.h>

#include <sme/assets.h>
#include <sme/material.h>
#include <sme/text.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

namespace sme {

namespace {

// --- Dependencies ------------------------------------------------------------------------------

constexpr int MAX_DEPTH = 8;   // patch materials, include models: never loop

std::string lowerGeneric(const std::filesystem::path& path) {
	auto text = toLower(path.lexically_normal().generic_string());
	while (text.size() > 1 && text.ends_with('/')) text.pop_back();
	return text;
}

bool isInside(const std::filesystem::path& path, const std::filesystem::path& folder) {
	if (folder.empty()) return false;
	const auto p = lowerGeneric(path), f = lowerGeneric(folder);
	return p == f || (p.size() > f.size() && p.starts_with(f) && p[f.size()] == '/');
}

/// Values that are no file names: numbers, vectors, "env_cubemap", empty.
bool looksLikeName(std::string_view value) {
	value = trim(value);
	if (value.empty() || iequals(value, "env_cubemap")) return false;
	return std::any_of(value.begin(), value.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }) &&
	       value.find_first_of("[{") == std::string_view::npos;
}

class Collector {
public:
	Collector(const MapDocument& doc, const GameFileSystem& fs, std::filesystem::path modDir) : m_doc(doc), m_fs(fs), m_modDir(std::move(modDir)) {}

	MapDependencies run() {
		const auto& world = m_doc.world;
		for (const auto& solid : world.solids) solidMaterials(solid);
		if (const auto sky = world.get("skyname"); !sky.empty()) skybox(sky);
		if (const auto detail = world.get("detailmaterial"); !detail.empty()) material(detail, "detail sprites (worldspawn)", 0);
		if (const auto vbsp = world.get("detailvbsp"); !vbsp.empty()) add(normalizePath(vbsp), ShipFile::Kind::Other, "detail sprite rules (worldspawn)");
		for (const auto& entity : m_doc.entities) {
			for (const auto& solid : entity.solids) solidMaterials(solid);
			entityFiles(entity);
		}
		MapDependencies out;
		for (auto& [path, file] : m_files) {
			if (file.origin == ShipFile::Origin::Missing) {
				out.problems.push_back(path + " is missing (" + file.neededBy + ")");
			}
			out.files.push_back(std::move(file));
		}
		return out;
	}

private:
	const MapDocument& m_doc;
	const GameFileSystem& m_fs;
	std::filesystem::path m_modDir;
	std::map<std::string, ShipFile> m_files;   // sorted by path
	std::unordered_map<std::string, std::vector<std::string>> m_scriptWaves;
	bool m_scriptsRead = false;
	std::vector<Soundscape> m_soundscapes;
	bool m_soundscapesRead = false;
	std::vector<ParticleSystem> m_particles;
	bool m_particlesRead = false;

	/// Records a file once; returns false if it was already there (the caller then stops).
	bool add(const std::string& path, ShipFile::Kind kind, const std::string& neededBy, bool optional = false) {
		if (path.empty() || m_files.contains(path)) return false;
		ShipFile file;
		file.path = path;
		file.kind = kind;
		file.neededBy = neededBy;
		file.mount = m_fs.locate(path);
		if (!file.mount) {
			if (optional) return false;
			file.origin = ShipFile::Origin::Missing;
		} else {
			const auto& mount = m_fs.mounts()[*file.mount];
			const bool base = lowerGeneric(mount.path).find("/steamapps/common/") != std::string::npos;
			if (base) {
				file.origin = ShipFile::Origin::BaseGame;
			} else {
				file.origin = (m_modDir.empty() || isInside(mount.path, m_modDir)) ? ShipFile::Origin::Custom : ShipFile::Origin::Shared;
				file.size = fileSize(file);
			}
		}
		const bool found = file.origin != ShipFile::Origin::Missing;
		m_files.emplace(path, std::move(file));
		return found;
	}

	std::uint64_t fileSize(const ShipFile& file) const {
		if (const auto disk = fileOnDisk(m_fs, file)) {
			std::error_code ec;
			const auto size = std::filesystem::file_size(*disk, ec);
			if (!ec) return size;
		}
		const auto data = m_fs.read(file.path);
		return data ? data->size() : 0;
	}

	void solidMaterials(const Solid& solid) {
		for (const auto& side : solid.sides) material(side.material, "face material", 0);
	}

	void texture(std::string_view name, const std::string& neededBy) {
		const auto path = "materials/" + normalizeMaterialName(name) + ".vtf";
		add(path, ShipFile::Kind::Texture, neededBy);
	}

	void material(std::string_view rawName, const std::string& neededBy, int depth) {
		const auto name = normalizeMaterialName(rawName);
		if (name.empty() || depth > MAX_DEPTH) return;
		const auto path = "materials/" + name + ".vmt";
		if (!add(path, ShipFile::Kind::Material, neededBy)) return;
		const auto parsed = loadMaterial(m_fs, name);
		if (!parsed) return;
		const auto by = "material " + name;
		for (const auto& include : parsed->includeChain) material(include, by, depth + 1);
		// Every parameter that names a texture or another material that exists (covers $basetexture,
		// $bumpmap, $envmapmask, $detail, $blendmodulatetexture, Mapbase's and water's extras, ...).
		for (const auto& [key, value] : parsed->params) {
			if (!looksLikeName(value)) continue;
			const auto other = normalizeMaterialName(value);
			if (m_fs.exists("materials/" + other + ".vtf")) {
				texture(other, by);
			} else if (m_fs.exists("materials/" + other + ".vmt")) {
				material(other, by, depth + 1);
			} else if (key == "$basetexture" || key == "$bumpmap" || key == "$normalmap" || key == "$basetexture2" || key == "$detail") {
				texture(other, by);   // a texture it names but nobody has: missing
			}
		}
	}

	void skybox(const std::string& sky) {
		static const std::array<const char*, 6> FACES = {"rt", "lf", "bk", "ft", "up", "dn"};
		for (const char* face : FACES) material("skybox/" + sky + face, "sky " + sky, 0);
		if (m_fs.exists("materials/skybox/" + toLower(sky) + "_hdrup.vmt")) {
			for (const char* face : FACES) material("skybox/" + sky + "_hdr" + face, "sky " + sky + " (HDR)", 0);
		}
	}

	void model(std::string_view rawPath, const std::string& neededBy, int depth) {
		auto path = normalizePath(rawPath);
		if (path.empty() || depth > MAX_DEPTH) return;
		if (!path.starts_with("models/")) path = "models/" + path;
		if (!add(path, ShipFile::Kind::Model, neededBy)) return;
		const auto stem = path.substr(0, path.size() - 4);
		const auto by = "model " + path;
		add(stem + ".vvd", ShipFile::Kind::Model, by, true);
		// The engine needs one of the meshes; the others are optional.
		bool anyVtx = false;
		for (const char* vtx : {".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".vtx"}) {
			anyVtx = add(stem + vtx, ShipFile::Kind::Model, by, true) || anyVtx || m_files.contains(stem + vtx);
		}
		add(stem + ".phy", ShipFile::Kind::Model, by, true);
		add(stem + ".ani", ShipFile::Kind::Model, by, true);
		const auto data = m_fs.read(path);
		if (!data) return;
		const auto info = readModelInfo(*data);
		if (!info) return;
		if (!anyVtx && !info->materials.empty()) add(stem + ".dx90.vtx", ShipFile::Kind::Model, by);   // reported missing
		for (const auto& include : info->includeModels) model(include, by, depth + 1);
		for (const auto& textureName : info->materials) {
			// Like the engine: the first $cdmaterials folder that holds it.
			std::string found;
			for (const auto& dir : info->materialDirs) {
				const auto candidate = normalizeMaterialName(dir + "/" + textureName);
				if (m_fs.exists("materials/" + candidate + ".vmt")) {
					found = candidate;
					break;
				}
			}
			if (found.empty()) {
				found = normalizeMaterialName((info->materialDirs.empty() ? std::string() : info->materialDirs.front() + "/") + textureName);
			}
			material(found, by, 0);
		}
	}

	void sound(std::string_view value, const std::string& neededBy) {
		const auto text = std::string(trim(value));
		if (text.empty()) return;
		if (endsWithI(text, ".wav") || endsWithI(text, ".mp3") || endsWithI(text, ".ogg")) {
			add("sound/" + normalizePath(stripSoundChars(text)), ShipFile::Kind::Sound, neededBy);
			return;
		}
		if (!m_scriptsRead) {
			m_scriptsRead = true;
			for (const auto& entry : soundScripts(m_fs)) m_scriptWaves[toLower(entry.name)] = entry.waves;
		}
		if (const auto it = m_scriptWaves.find(toLower(text)); it != m_scriptWaves.end()) {
			for (const auto& wave : it->second) add("sound/" + normalizePath(stripSoundChars(wave)), ShipFile::Kind::Sound, "sound " + text);
		}
	}

	void soundscape(const std::string& name, const std::string& neededBy, int depth) {
		if (name.empty() || depth > MAX_DEPTH) return;
		if (!m_soundscapesRead) {
			m_soundscapesRead = true;
			m_soundscapes = soundscapes(m_fs);
		}
		for (const auto& scape : m_soundscapes) {
			if (!iequals(scape.name, name)) continue;
			add(normalizePath(scape.file), ShipFile::Kind::Soundscape, neededBy);
			const auto by = "soundscape " + scape.name;
			for (const auto& s : scape.sounds) sound(s, by);
			for (const auto& include : scape.includes) soundscape(include, by, depth + 1);
			return;
		}
	}

	void particles(const std::string& effect, const std::string& neededBy) {
		if (effect.empty()) return;
		if (!m_particlesRead) {
			m_particlesRead = true;
			m_particles = particleSystems(m_fs);
		}
		for (const auto& system : m_particles) {
			if (iequals(system.name, effect)) {
				add(normalizePath(system.file), ShipFile::Kind::Particles, neededBy);
				return;
			}
		}
	}

	void entityFiles(const Entity& entity) {
		const auto classname = toLower(entity.classname());
		const auto who = classname + (entity.get("targetname").empty() ? std::string() : " \"" + entity.get("targetname") + "\"");
		for (const auto& [rawKey, value] : entity.keyValues) {
			const auto key = toLower(rawKey);
			if (key == "classname" || key == "targetname" || key == "origin" || key == "angles" || key == "target" || key == "parentname") continue;
			if (value.empty()) continue;
			if (endsWithI(value, ".mdl")) {
				model(value, who, 0);
			} else if (endsWithI(value, ".vmt") || endsWithI(value, ".spr")) {
				material(value, who, 0);
			} else if (key == "material" || key == "texture" || key == "decalname") {
				material(value, who, 0);   // overlays, decals
			} else if (key == "texturename") {
				texture(value, who);   // env_projectedtexture
			} else if (key == "effect_name") {
				particles(value, who);
			} else if (key == "soundscape" && classname.starts_with("env_soundscape")) {
				soundscape(value, who, 0);
			} else if (looksLikeName(value)) {
				sound(value, who);   // ambient_generic "message", door sounds, ...
			}
		}
	}
};

// --- ZIP ---------------------------------------------------------------------------------------

std::uint32_t crc32(std::uint32_t crc, const std::byte* data, std::size_t size) {
	static const auto TABLE = [] {
		std::array<std::uint32_t, 256> table{};
		for (std::uint32_t i = 0; i < 256; ++i) {
			std::uint32_t c = i;
			for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		return table;
	}();
	crc = ~crc;
	for (std::size_t i = 0; i < size; ++i) crc = TABLE[(crc ^ static_cast<std::uint8_t>(data[i])) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

void put16(std::string& out, std::uint16_t v) {
	out.push_back(static_cast<char>(v & 0xFF));
	out.push_back(static_cast<char>(v >> 8));
}

void put32(std::string& out, std::uint32_t v) {
	for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

std::uint32_t get32(const std::string& in, std::size_t at) {
	std::uint32_t v = 0;
	for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<std::uint8_t>(in[at + static_cast<std::size_t>(i)]);
	return v;
}

std::uint16_t get16(const std::string& in, std::size_t at) {
	return static_cast<std::uint16_t>(static_cast<std::uint8_t>(in[at]) | (static_cast<std::uint8_t>(in[at + 1]) << 8));
}

std::pair<std::uint16_t, std::uint16_t> dosTime() {
	const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &now);
#else
	localtime_r(&now, &tm);
#endif
	const auto time = static_cast<std::uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
	const auto date = static_cast<std::uint16_t>(((std::max(tm.tm_year, 80) - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
	return {time, date};
}

std::optional<std::vector<std::byte>> readAll(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return std::nullopt;
	std::vector<std::byte> data;
	in.seekg(0, std::ios::end);
	const auto size = in.tellg();
	if (size < 0) return std::nullopt;
	data.resize(static_cast<std::size_t>(size));
	in.seekg(0);
	in.read(reinterpret_cast<char*>(data.data()), size);
	return in ? std::optional(std::move(data)) : std::nullopt;
}

bool safeEntryName(std::string_view name) {
	if (name.empty() || name.front() == '/' || name.find('\\') != std::string_view::npos || name.find(':') != std::string_view::npos) return false;
	for (const auto& part : splitTrimmed(name, '/')) {
		if (part == "..") return false;
	}
	return true;
}

} // namespace

std::size_t MapDependencies::count(ShipFile::Origin origin) const {
	return static_cast<std::size_t>(std::count_if(files.begin(), files.end(), [&](const ShipFile& f) { return f.origin == origin; }));
}

std::uint64_t MapDependencies::bytes(ShipFile::Origin origin) const {
	std::uint64_t total = 0;
	for (const auto& f : files) {
		if (f.origin == origin) total += f.size;
	}
	return total;
}

MapDependencies mapDependencies(const MapDocument& doc, const GameFileSystem& fs, const std::filesystem::path& modDir) {
	return Collector(doc, fs, modDir).run();
}

std::optional<std::filesystem::path> fileOnDisk(const GameFileSystem& fs, const ShipFile& file) {
	if (!file.mount || *file.mount >= fs.mounts().size()) return std::nullopt;
	const auto& mount = fs.mounts()[*file.mount];
	if (mount.kind != Mount::Kind::Directory) return std::nullopt;
	auto found = resolveI(mount.path, file.path);
	if (found && std::filesystem::is_regular_file(*found)) return found;
	return std::nullopt;
}

std::string bspzipFileList(const std::vector<std::pair<std::string, std::filesystem::path>>& files) {
	std::string out;
	for (const auto& [inside, disk] : files) {
		out += inside;
		out += '\n';
		out += disk.lexically_normal().make_preferred().string();
		out += '\n';
	}
	return out;
}

bool writeZip(const std::filesystem::path& zip, const std::vector<PackageEntry>& entries, std::string* error,
              const std::function<void(std::size_t, std::size_t)>& progress) {
	const auto fail = [&](const std::string& why) {
		if (error) *error = why;
		return false;
	};
	if (entries.size() > 0xFFFF) return fail("too many files for a ZIP (65535 at most)");
	std::error_code ec;
	if (zip.has_parent_path()) std::filesystem::create_directories(zip.parent_path(), ec);
	std::ofstream out(zip, std::ios::binary | std::ios::trunc);
	if (!out) return fail("cannot write " + displayPath(zip));
	const auto [time, date] = dosTime();
	std::string central;
	std::uint64_t offset = 0;
	std::set<std::string> names;
	for (std::size_t i = 0; i < entries.size(); ++i) {
		const auto& entry = entries[i];
		if (!safeEntryName(entry.name)) return fail("bad name in the package: " + entry.name);
		if (!names.insert(toLower(entry.name)).second) return fail("twice in the package: " + entry.name);
		std::vector<std::byte> loaded;
		const std::vector<std::byte>* data = &entry.data;
		if (!entry.file.empty()) {
			auto read = readAll(entry.file);
			if (!read) return fail("cannot read " + displayPath(entry.file));
			loaded = std::move(*read);
			data = &loaded;
		}
		if (data->size() >= 0xFFFFFFFFull || offset >= 0xFFFFFFFFull) return fail("the package would be larger than 4 GB");
		const auto crc = crc32(0, data->data(), data->size());
		const auto size = static_cast<std::uint32_t>(data->size());
		std::string header;
		put32(header, 0x04034b50);
		put16(header, 10);        // version needed
		put16(header, 0x0800);    // UTF-8 names
		put16(header, 0);         // stored
		put16(header, time);
		put16(header, date);
		put32(header, crc);
		put32(header, size);
		put32(header, size);
		put16(header, static_cast<std::uint16_t>(entry.name.size()));
		put16(header, 0);
		header += entry.name;
		out.write(header.data(), static_cast<std::streamsize>(header.size()));
		out.write(reinterpret_cast<const char*>(data->data()), static_cast<std::streamsize>(data->size()));
		if (!out) return fail("cannot write " + displayPath(zip) + " (disk full?)");

		put32(central, 0x02014b50);
		put16(central, 20);       // made by
		put16(central, 10);
		put16(central, 0x0800);
		put16(central, 0);
		put16(central, time);
		put16(central, date);
		put32(central, crc);
		put32(central, size);
		put32(central, size);
		put16(central, static_cast<std::uint16_t>(entry.name.size()));
		put16(central, 0);
		put16(central, 0);
		put16(central, 0);
		put16(central, 0);
		put32(central, 0);
		put32(central, static_cast<std::uint32_t>(offset));
		central += entry.name;
		offset += header.size() + data->size();
		if (progress) progress(i + 1, entries.size());
	}
	if (offset >= 0xFFFFFFFFull) return fail("the package would be larger than 4 GB");
	std::string end;
	put32(end, 0x06054b50);
	put16(end, 0);
	put16(end, 0);
	put16(end, static_cast<std::uint16_t>(entries.size()));
	put16(end, static_cast<std::uint16_t>(entries.size()));
	put32(end, static_cast<std::uint32_t>(central.size()));
	put32(end, static_cast<std::uint32_t>(offset));
	put16(end, 0);
	out.write(central.data(), static_cast<std::streamsize>(central.size()));
	out.write(end.data(), static_cast<std::streamsize>(end.size()));
	out.close();
	if (!out) return fail("cannot write " + displayPath(zip) + " (disk full?)");
	return true;
}

std::vector<std::string> zipEntryNames(const std::filesystem::path& zip) {
	const auto text = readFileText(zip);
	if (!text || text->size() < 22) return {};
	const auto& in = *text;
	// End of central directory: search backwards (a comment may follow it).
	std::size_t endAt = std::string::npos;
	for (std::size_t i = in.size() - 22 + 1; i-- > 0 && in.size() - i <= 22 + 0xFFFF;) {
		if (get32(in, i) == 0x06054b50) {
			endAt = i;
			break;
		}
	}
	if (endAt == std::string::npos) return {};
	const std::size_t count = get16(in, endAt + 10);
	std::size_t at = get32(in, endAt + 16);
	std::vector<std::string> names;
	for (std::size_t i = 0; i < count; ++i) {
		if (at + 46 > in.size() || get32(in, at) != 0x02014b50) return {};
		const std::size_t nameLength = get16(in, at + 28), extraLength = get16(in, at + 30), commentLength = get16(in, at + 32);
		if (at + 46 + nameLength > in.size()) return {};
		names.push_back(in.substr(at + 46, nameLength));
		at += 46 + nameLength + extraLength + commentLength;
	}
	return names;
}

bool writeFolder(const std::filesystem::path& folder, const std::vector<PackageEntry>& entries, std::string* error) {
	std::error_code ec;
	for (const auto& entry : entries) {
		if (!safeEntryName(entry.name)) {
			if (error) *error = "bad name in the package: " + entry.name;
			return false;
		}
		const auto target = folder / toNativePath(entry.name);
		std::filesystem::create_directories(target.parent_path(), ec);
		if (!entry.file.empty()) {
			std::filesystem::copy_file(entry.file, target, std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				if (error) *error = "cannot copy " + displayPath(entry.file) + ": " + ec.message();
				return false;
			}
		} else {
			std::ofstream out(target, std::ios::binary | std::ios::trunc);
			out.write(reinterpret_cast<const char*>(entry.data.data()), static_cast<std::streamsize>(entry.data.size()));
			if (!out) {
				if (error) *error = "cannot write " + displayPath(target);
				return false;
			}
		}
	}
	return true;
}

std::vector<PackageEntry> sharePackage(const std::filesystem::path& bsp, const MapDependencies& deps, const GameFileSystem& fs, std::string_view readme) {
	std::vector<PackageEntry> out;
	PackageEntry map;
	map.name = "maps/" + toLower(bsp.filename().string());
	map.file = bsp;
	out.push_back(std::move(map));
	for (const auto& file : deps.files) {
		if (file.origin != ShipFile::Origin::Custom) continue;
		PackageEntry entry;
		entry.name = file.path;
		if (const auto disk = fileOnDisk(fs, file)) {
			entry.file = *disk;
		} else if (auto data = fs.read(file.path)) {
			entry.data = std::move(*data);   // from a VPK of the mod
		} else {
			continue;
		}
		out.push_back(std::move(entry));
	}
	if (!trim(readme).empty()) {
		PackageEntry text;
		text.name = "readme.txt";
		for (const char c : readme) text.data.push_back(static_cast<std::byte>(c));
		out.push_back(std::move(text));
	}
	return out;
}

std::string developmentFileReason(const std::filesystem::path& relative) {
	const auto path = lowerGeneric(relative);
	const auto first = path.substr(0, path.find('/'));
	const auto name = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
	const auto ext = std::filesystem::path(name).extension().string();
	if (first == "mapsrc" || first == "materialsrc" || first == "modelsrc") return "sources";
	if (first == "screenshots") return "screenshots";
	if (first == "save") return "saved games";
	if (first == "downloads" || first == "download") return "downloads";
	if (first == ".git" || name == ".gitignore" || name == ".gitattributes") return "version control";
	if (ext == ".vmf" || ext == ".vmx") return "map sources";
	if (ext == ".log") return "log";
	if (ext == ".prt" || ext == ".lin") return "compile leftovers";
	if (ext == ".cache") return "cache";
	if (ext == ".bak" || ext == ".tmp" || ext == ".pdb") return "temporary";
	if (path == "cfg/config.cfg") return "your own settings";
	if (name == "thumbs.db" || name == "desktop.ini" || name == ".ds_store") return "system file";
	return {};
}

ModPackagePlan planModPackage(const std::filesystem::path& modDir) {
	ModPackagePlan plan;
	std::error_code ec;
	std::filesystem::recursive_directory_iterator it(modDir, std::filesystem::directory_options::skip_permission_denied, ec);
	for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
		const auto relative = it->path().lexically_relative(modDir);
		if (it->is_directory(ec)) {
			// Whole development folders stay out at once.
			if (const auto why = developmentFileReason(relative); !why.empty() && relative.begin() != relative.end() &&
			                                                           std::next(relative.begin()) == relative.end()) {
				plan.excluded.push_back({relative, why});
				it.disable_recursion_pending();
			}
			continue;
		}
		if (!it->is_regular_file(ec)) continue;
		if (const auto why = developmentFileReason(relative); !why.empty()) {
			plan.excluded.push_back({relative, why});
			continue;
		}
		plan.files.push_back(relative);
		plan.bytes += it->file_size(ec);
	}
	std::sort(plan.files.begin(), plan.files.end());
	std::sort(plan.excluded.begin(), plan.excluded.end());
	return plan;
}

std::vector<PackageEntry> modPackage(const std::filesystem::path& modDir, const ModPackagePlan& plan) {
	std::vector<PackageEntry> out;
	const auto folder = modDir.lexically_normal().filename().empty() ? modDir.lexically_normal().parent_path().filename() : modDir.filename();
	for (const auto& file : plan.files) {
		PackageEntry entry;
		entry.name = (folder / file).generic_string();
		entry.file = modDir / file;
		out.push_back(std::move(entry));
	}
	return out;
}

std::string workshopItemVdf(const WorkshopItem& item) {
	const auto quote = [](std::string_view text) {
		std::string out = "\"";
		for (const char c : text) {
			if (c == '"' || c == '\\') out += '\\';
			out += c;
		}
		return out + "\"";
	};
	// Forward slashes: steamcmd takes them on every system and they need no escaping.
	const auto path = [&](const std::filesystem::path& p) { return quote(p.empty() ? std::string() : p.generic_string()); };
	std::string out = "\"workshopitem\"\n{\n";
	const auto line = [&](std::string_view key, const std::string& value) { out += "\t\"" + std::string(key) + "\"\t\t" + value + "\n"; };
	line("appid", quote(std::to_string(item.appId)));
	line("publishedfileid", quote(std::to_string(item.publishedFileId)));
	line("contentfolder", path(item.contentFolder));
	line("previewfile", path(item.previewFile));
	line("visibility", quote(std::to_string(std::clamp(item.visibility, 0, 3))));
	line("title", quote(item.title));
	line("description", quote(item.description));
	line("changenote", quote(item.changeNote));
	out += "}\n";
	return out;
}

} // namespace sme
