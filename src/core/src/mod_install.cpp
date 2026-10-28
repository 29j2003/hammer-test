// mod_install.cpp - The mod's features installed from Mapsmith (see mod_install.h).

#include <sme/mod_install.h>

#include <sme/text.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>

namespace sme {

namespace fs = std::filesystem;

namespace {

// --- SHA-256 (FIPS 180-4) -------------------------------------------------------------------

constexpr std::array<std::uint32_t, 64> K = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
	0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
	0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
	0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
	0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t rotr(std::uint32_t x, int n) {
	return (x >> n) | (x << (32 - n));
}

class Sha256 {
public:
	void add(const unsigned char* data, std::size_t size) {
		m_length += size;
		while (size > 0) {
			const std::size_t take = std::min(size, std::size_t(64) - m_fill);
			std::memcpy(m_block.data() + m_fill, data, take);
			m_fill += take;
			data += take;
			size -= take;
			if (m_fill == 64) {
				block();
				m_fill = 0;
			}
		}
	}
	std::string hex() {
		const std::uint64_t bits = m_length * 8;
		const unsigned char one = 0x80, zero = 0;
		add(&one, 1);
		while (m_fill != 56) add(&zero, 1);
		unsigned char length[8];
		for (int i = 0; i < 8; ++i) length[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
		add(length, 8);
		static const char* digits = "0123456789abcdef";
		std::string out;
		for (const auto word : m_h) {
			for (int shift = 28; shift >= 0; shift -= 4) out += digits[(word >> shift) & 0xf];
		}
		return out;
	}

private:
	void block() {
		std::array<std::uint32_t, 64> w{};
		for (int i = 0; i < 16; ++i) {
			w[i] = std::uint32_t(m_block[i * 4]) << 24 | std::uint32_t(m_block[i * 4 + 1]) << 16 | std::uint32_t(m_block[i * 4 + 2]) << 8 |
			       std::uint32_t(m_block[i * 4 + 3]);
		}
		for (int i = 16; i < 64; ++i) {
			const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		auto [a, b, c, d, e, f, g, h] = m_h;
		for (int i = 0; i < 64; ++i) {
			const std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
			const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		const std::array<std::uint32_t, 8> add{a, b, c, d, e, f, g, h};
		for (int i = 0; i < 8; ++i) m_h[i] += add[i];
	}

	std::array<std::uint32_t, 8> m_h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	std::array<unsigned char, 64> m_block{};
	std::size_t m_fill = 0;
	std::uint64_t m_length = 0;
};

// --- Files ------------------------------------------------------------------------------------

/// A relative path that stays inside its folder ("a/b.dll"; no "..", no root, no drive).
bool safeRelative(std::string_view path) {
	if (path.empty() || path.front() == '/' || path.front() == '\\' || path.find(':') != std::string_view::npos) return false;
	for (const auto& part : splitTrimmed(path, '/')) {
		if (part == ".." || part.find('\\') != std::string::npos) return false;
	}
	return true;
}

fs::path below(const fs::path& root, std::string_view relative) {
	return root / fs::path(std::string(relative)).make_preferred();
}

bool writeText(const fs::path& file, std::string_view text) {
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	const fs::path temp = file.string() + ".mapsmith-new";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out.write(text.data(), std::streamsize(text.size()));
		if (!out) return false;
	}
	fs::rename(temp, file, ec);
	if (ec) fs::remove(temp, ec);
	return !ec;
}

/// Copies through a temporary file next to the target, so a target is never half written.
bool copyOver(const fs::path& from, const fs::path& to, std::string* error) {
	std::error_code ec;
	fs::create_directories(to.parent_path(), ec);
	const fs::path temp = to.string() + ".mapsmith-new";
	fs::copy_file(from, temp, fs::copy_options::overwrite_existing, ec);
	if (!ec) fs::rename(temp, to, ec);
	if (ec) {
		std::error_code ignored;
		fs::remove(temp, ignored);
		if (error) *error = ec.message();
		return false;
	}
	return true;
}

std::string newlineOf(std::string_view text) {
	return text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
}

std::vector<std::string> lines(std::string_view text) {
	std::vector<std::string> out;
	std::size_t start = 0;
	while (start < text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos) end = text.size();
		std::string line(text.substr(start, end - start));
		if (!line.empty() && line.back() == '\r') line.pop_back();
		out.push_back(std::move(line));
		start = end + 1;
	}
	return out;
}

const std::regex& execFidelity() {
	static const std::regex re(R"(^\s*exec\s+"?fidelity"?\s*(//.*)?$)", std::regex::icase);
	return re;
}

/// autoexec.cfg with the exec line (nothing changes when some line already runs fidelity).
std::optional<std::string> withExecLine(std::string_view autoexec) {
	for (const auto& line : lines(autoexec)) {
		if (std::regex_match(line, execFidelity())) return std::nullopt;
	}
	const std::string nl = newlineOf(autoexec);
	std::string out(autoexec);
	if (!out.empty() && out.back() != '\n') out += nl;
	return out + std::string(FIDELITY_EXEC_LINE) + nl;
}

/// autoexec.cfg without the exec line deploy.ps1 or the install wrote (other lines stay).
std::string withoutExecLine(std::string_view autoexec) {
	const std::string nl = newlineOf(autoexec);
	std::string out;
	for (const auto& line : lines(autoexec)) {
		if (std::regex_match(line, execFidelity()) && line.find("// mapbase-fidelity") != std::string::npos) continue;
		out += line + nl;
	}
	return out;
}

constexpr std::array<const char*, 2> VRAD_FILES = {"vrad.exe", "vrad_dll.dll"};
constexpr std::string_view VRAD_BACKUP = "fidelity_backup";
constexpr std::string_view VRAD_BACKUP_LIST = "fidelity_backup.txt";

/// VRAD into the SDK's bin, the files there before kept once in bin/fidelity_backup (the folder
/// and list update.ps1 uses: "saved <file>" or "ours <file>").
bool installVrad(const FeatureBundle& bundle, const fs::path& sdkBin, std::string* error) {
	std::error_code ec;
	const fs::path backup = sdkBin / std::string(VRAD_BACKUP);
	if (!fs::exists(backup / std::string(VRAD_BACKUP_LIST), ec)) {
		const fs::path temp = sdkBin / (std::string(VRAD_BACKUP) + "_new");
		fs::remove_all(temp, ec);
		fs::remove_all(backup, ec);   // left incomplete earlier
		fs::create_directories(temp, ec);
		std::string list;
		for (const char* name : VRAD_FILES) {
			const fs::path original = sdkBin / name;
			if (!fs::exists(original, ec)) continue;
			const auto* ours = bundle.find(std::string("vrad/") + name);
			if (ours && fileSha256(original) == ours->sha256) {
				list += std::string("ours ") + name + "\r\n";
				continue;
			}
			fs::copy_file(original, temp / name, fs::copy_options::overwrite_existing, ec);
			if (ec) {
				if (error) *error = "could not keep the VRAD there before (" + ec.message() + ")";
				fs::remove_all(temp, ec);
				return false;
			}
			list += std::string("saved ") + name + "\r\n";
		}
		if (!writeText(temp / std::string(VRAD_BACKUP_LIST), list)) {
			if (error) *error = "could not write the VRAD backup list";
			return false;
		}
		fs::rename(temp, backup, ec);
		if (ec) {
			if (error) *error = "could not keep the VRAD there before (" + ec.message() + ")";
			return false;
		}
	}
	for (const char* name : VRAD_FILES) {
		const auto* file = bundle.find(std::string("vrad/") + name);
		const fs::path target = sdkBin / name;
		if (fileSha256(target) == file->sha256) continue;
		std::string why;
		if (!copyOver(below(bundle.root, file->path), target, &why)) {
			if (error) *error = "could not copy " + std::string(name) + " (" + why + ") - close Hammer and running map compiles";
			return false;
		}
	}
	return true;
}

/// Puts back what bin/fidelity_backup holds and removes it.
bool restoreVrad(const fs::path& sdkBin, std::string* error) {
	std::error_code ec;
	const fs::path backup = sdkBin / std::string(VRAD_BACKUP);
	const auto list = readFileText(backup / std::string(VRAD_BACKUP_LIST));
	if (!list) return true;   // nothing kept (or update.ps1 put it back already)
	for (const auto& line : lines(*list)) {
		const auto parts = splitTrimmed(line, ' ');
		if (parts.size() != 2 || parts[0] != "saved") continue;
		if (std::find(VRAD_FILES.begin(), VRAD_FILES.end(), parts[1]) == VRAD_FILES.end()) continue;
		std::string why;
		if (!copyOver(backup / parts[1], sdkBin / parts[1], &why)) {
			if (error) *error = "could not put back " + parts[1] + " (" + why + ") - close Hammer and running map compiles";
			return false;
		}
	}
	fs::remove_all(backup, ec);
	return true;
}

} // namespace

std::string sha256Hex(std::string_view data) {
	Sha256 hash;
	hash.add(reinterpret_cast<const unsigned char*>(data.data()), data.size());
	return hash.hex();
}

std::optional<std::string> fileSha256(const fs::path& file) {
	std::ifstream in(file, std::ios::binary);
	if (!in) return std::nullopt;
	Sha256 hash;
	std::array<char, 65536> buffer{};
	while (in) {
		in.read(buffer.data(), buffer.size());
		hash.add(reinterpret_cast<const unsigned char*>(buffer.data()), std::size_t(in.gcount()));
	}
	if (in.bad()) return std::nullopt;
	return hash.hex();
}

const FeatureBundleFile* FeatureBundle::find(std::string_view path) const {
	for (const auto& file : files) {
		if (iequals(file.path, path)) return &file;
	}
	return nullptr;
}

std::optional<FeatureBundle> readFeatureBundle(const fs::path& root, std::string* error) {
	const auto fail = [error](const std::string& text) -> std::optional<FeatureBundle> {
		if (error) *error = text;
		return std::nullopt;
	};
	const auto text = readFileText(root / std::string(BUNDLE_MANIFEST));
	if (!text) return fail("no " + std::string(BUNDLE_MANIFEST) + " in " + displayPath(root));
	FeatureBundle bundle;
	bundle.root = root;
	bool header = false;
	for (const auto& line : lines(*text)) {
		const std::string_view clean = trim(line);
		if (clean.empty()) continue;
		if (!header) {
			if (clean != "mapsmith-features 1") return fail("not a feature bundle manifest");
			header = true;
		} else if (clean.starts_with("version ")) {
			bundle.version = std::string(trim(clean.substr(8)));
		} else {
			// "<sha256>  <path>"
			const auto space = clean.find(' ');
			if (space != 64) return fail("broken line in the manifest: " + std::string(clean));
			FeatureBundleFile file;
			file.sha256 = toLower(clean.substr(0, 64));
			file.path = std::string(trim(clean.substr(space)));
			std::replace(file.path.begin(), file.path.end(), '\\', '/');
			if (!safeRelative(file.path)) return fail("the manifest names a file outside the bundle: " + file.path);
			if (file.sha256.find_first_not_of("0123456789abcdef") != std::string::npos) return fail("broken checksum in the manifest");
			bundle.files.push_back(std::move(file));
		}
	}
	if (!header) return fail("the manifest is empty");
	if (bundle.version.empty()) return fail("the manifest has no version");
	return bundle;
}

std::string writeBundleManifest(std::string_view version, const std::vector<FeatureBundleFile>& files) {
	std::string out = "mapsmith-features 1\nversion " + std::string(version) + "\n";
	for (const auto& file : files) out += file.sha256 + "  " + file.path + "\n";
	return out;
}

bool verifyFeatureBundle(const FeatureBundle& bundle, std::string* error) {
	for (const auto& file : bundle.files) {
		const auto hash = fileSha256(below(bundle.root, file.path));
		if (hash != file.sha256) {
			if (error) *error = (hash ? "damaged file in Mapsmith's features: " : "missing file in Mapsmith's features: ") + file.path;
			return false;
		}
	}
	return true;
}

ModCodeScripts bundledScripts(const FeatureBundle& bundle) {
	ModCodeScripts scripts;
	scripts.root = bundle.root;
	if (bundle.find("fidelity/deploy.ps1")) scripts.deploy = below(bundle.root, "fidelity/deploy.ps1");
	if (bundle.find("fidelity/report.ps1")) scripts.report = below(bundle.root, "fidelity/report.ps1");
	return scripts;
}

ModBranch modBranch(std::string_view gameinfo) {
	static const std::regex word(R"(\b(ep2|episodic)\b)", std::regex::icase);
	for (const auto& line : lines(gameinfo)) {
		const std::string code = line.substr(0, line.find("//"));
		if (std::regex_search(code, word)) return ModBranch::Episodic;
	}
	return ModBranch::Hl2;
}

std::string_view modBranchName(ModBranch branch) {
	return branch == ModBranch::Episodic ? "episodic" : "hl2";
}

std::optional<InstalledFeatures> readInstalledFeatures(const fs::path& modDir) {
	const auto text = readFileText(below(modDir, INSTALLED_FILE));
	if (!text) return std::nullopt;
	InstalledFeatures installed;
	for (const auto& line : lines(*text)) {
		const std::string_view clean = trim(line);
		if (clean.empty() || clean.starts_with("//")) continue;
		const auto space = clean.find(' ');
		const std::string_view key = clean.substr(0, space);
		const std::string_view rest = space == std::string_view::npos ? std::string_view() : trim(clean.substr(space));
		if (key == "version") {
			installed.version = std::string(rest);
		} else if (key == "branch") {
			installed.branch = rest == "hl2" ? ModBranch::Hl2 : ModBranch::Episodic;
		} else if (key == "file") {
			// "file <sha256> <path>"
			const auto gap = rest.find(' ');
			if (gap != 64) continue;
			const std::string path(trim(rest.substr(gap)));
			if (safeRelative(path)) installed.files[path] = std::string(rest.substr(0, 64));
		} else if (key == "backup") {
			if (safeRelative(rest)) installed.backups.emplace_back(rest);
		} else if (key == "autoexec") {
			installed.autoexecLine = rest == "1";
		} else if (key == "vrad") {
			installed.vradDir = fs::path(std::string(rest));
		}
	}
	if (installed.version.empty()) return std::nullopt;
	return installed;
}

std::string writeInstalledFeatures(const InstalledFeatures& installed) {
	std::string out = "// mapsmith: the mod's features (mapbase-fidelity) installed by Mapsmith - Project > Features keeps this list\n";
	out += "version " + installed.version + "\n";
	out += "branch " + std::string(modBranchName(installed.branch)) + "\n";
	for (const auto& [path, hash] : installed.files) out += "file " + hash + " " + path + "\n";
	for (const auto& path : installed.backups) out += "backup " + path + "\n";
	out += std::string("autoexec ") + (installed.autoexecLine ? "1" : "0") + "\n";
	if (!installed.vradDir.empty()) out += "vrad " + installed.vradDir.string() + "\n";
	return out;
}

std::vector<FeatureCopy> featureInstallPlan(const FeatureBundle& bundle, ModBranch branch) {
	std::vector<FeatureCopy> plan;
	const std::string dlls = "bin/" + std::string(modBranchName(branch)) + "/";
	for (const auto& file : bundle.files) {
		const std::string& path = file.path;
		if (startsWithI(path, dlls)) {
			plan.push_back({path, "bin/" + path.substr(dlls.size())});
		} else if (iequals(path, "bin/game_shader_dx6.dll")) {
			plan.push_back({path, path});
		} else if (startsWithI(path, "fidelity/game/")) {
			plan.push_back({path, path.substr(std::string_view("fidelity/game/").size())});
		}
	}
	return plan;
}

FeatureInstallResult installFeatures(const FeatureBundle& bundle, const fs::path& modDir, const fs::path& sdkBin) {
	FeatureInstallResult result;
	const auto gameinfo = readFileText(modDir / "gameinfo.txt");
	if (!gameinfo) {
		result.error = "this folder has no gameinfo.txt - it is not a mod";
		return result;
	}
	if (!verifyFeatureBundle(bundle, &result.error)) return result;
	const auto previous = readInstalledFeatures(modDir);
	InstalledFeatures installed;
	installed.version = bundle.version;
	installed.branch = modBranch(*gameinfo);
	if (previous) {
		installed.backups = previous->backups;
		installed.autoexecLine = previous->autoexecLine;
		installed.vradDir = previous->vradDir;
	}
	const auto plan = featureInstallPlan(bundle, installed.branch);
	const auto save = [&] {
		if (!installed.files.empty() || previous) (void)writeText(below(modDir, INSTALLED_FILE), writeInstalledFeatures(installed));
	};
	std::set<std::string> planned;
	std::error_code ec;
	for (const auto& copy : plan) {
		planned.insert(copy.to);
		const auto* file = bundle.find(copy.from);
		const fs::path target = below(modDir, copy.to);
		const auto current = fileSha256(target);
		if (current == file->sha256) {
			installed.files[copy.to] = file->sha256;
			continue;
		}
		if (current) {
			// Not ours (Mapbase's DLL, a build of one's own, deploy.bat's copy): kept once.
			const bool ours = previous && previous->files.count(copy.to) && previous->files.at(copy.to) == *current;
			const fs::path backup = target.string() + std::string(BACKUP_SUFFIX);
			if (!ours && !fs::exists(backup, ec)) {
				fs::copy_file(target, backup, ec);
				if (ec) {
					result.error = "could not keep " + copy.to + " (" + ec.message() + ")";
					save();
					return result;
				}
				installed.backups.push_back(copy.to);
				result.backedUp.push_back(copy.to);
			}
		}
		std::string why;
		if (!copyOver(below(bundle.root, file->path), target, &why)) {
			result.error = "could not write " + copy.to + " (" + why + ") - close the game and try again";
			save();
			return result;
		}
		installed.files[copy.to] = file->sha256;
		result.written.push_back(copy.to);
		result.changed = true;
	}
	// Files an older version installed that this one no longer has.
	if (previous) {
		for (const auto& [path, hash] : previous->files) {
			if (planned.count(path)) continue;
			const fs::path target = below(modDir, path);
			const fs::path backup = target.string() + std::string(BACKUP_SUFFIX);
			if (fs::exists(backup, ec)) {
				fs::rename(backup, target, ec);
				std::erase(installed.backups, path);
			} else if (fileSha256(target) == hash) {
				fs::remove(target, ec);
			}
			result.changed = true;
		}
	}
	// The cfg runs from autoexec.cfg (the line deploy.ps1 writes).
	const fs::path autoexec = below(modDir, "cfg/autoexec.cfg");
	if (const auto text = withExecLine(readFileText(autoexec).value_or(std::string()))) {
		if (!writeText(autoexec, *text)) {
			result.error = "could not write cfg/autoexec.cfg";
			save();
			return result;
		}
		installed.autoexecLine = true;
		result.written.emplace_back("cfg/autoexec.cfg");
		result.changed = true;
	}
	// VRAD with baked ambient occlusion, into the SDK.
	if (!sdkBin.empty() && fs::is_directory(sdkBin, ec) && bundle.find("vrad/vrad.exe") && bundle.find("vrad/vrad_dll.dll")) {
		std::string why;
		if (installVrad(bundle, sdkBin, &why)) {
			installed.vradDir = sdkBin;
			result.vrad = true;
		} else {
			result.error = "VRAD: " + why;
			save();
			return result;
		}
	}
	save();
	result.ok = true;
	return result;
}

FeatureInstallResult removeFeatures(const fs::path& modDir) {
	FeatureInstallResult result;
	const auto installed = readInstalledFeatures(modDir);
	if (!installed) {
		result.ok = true;   // nothing installed by Mapsmith
		return result;
	}
	std::error_code ec;
	for (const auto& [path, hash] : installed->files) {
		const fs::path target = below(modDir, path);
		const fs::path backup = target.string() + std::string(BACKUP_SUFFIX);
		if (fs::exists(backup, ec)) {
			std::string why;
			if (!copyOver(backup, target, &why)) {
				result.error = "could not put back " + path + " (" + why + ") - close the game and try again";
				return result;
			}
			fs::remove(backup, ec);
			result.written.push_back(path);
		} else if (fileSha256(target) == hash) {
			fs::remove(target, ec);
			if (ec) {
				result.error = "could not remove " + path + " (" + ec.message() + ") - close the game and try again";
				return result;
			}
			result.written.push_back(path);
		}
		result.changed = true;
	}
	if (installed->autoexecLine) {
		const fs::path autoexec = below(modDir, "cfg/autoexec.cfg");
		if (const auto text = readFileText(autoexec)) {
			const std::string without = withoutExecLine(*text);
			if (without != *text) {
				(void)writeText(autoexec, without);
				result.written.emplace_back("cfg/autoexec.cfg");
			}
		}
	}
	if (!installed->vradDir.empty()) {
		std::string why;
		if (!restoreVrad(installed->vradDir, &why)) {
			result.error = "VRAD: " + why;
			return result;
		}
		result.vrad = true;
	}
	fs::remove(below(modDir, INSTALLED_FILE), ec);
	result.ok = true;
	return result;
}

bool featuresOutdated(const FeatureBundle& bundle, const InstalledFeatures& installed) {
	return bundle.version != installed.version;
}

} // namespace sme
