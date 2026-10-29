// assets.cpp - Material categories, MDL header reading, sound scripts (see assets.h).

#include <sme/assets.h>

#include <sme/audio.h>
#include <sme/filesystem.h>
#include <sme/keyvalues.h>
#include <sme/text.h>

#include <kvpp/DMX.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace sme {

namespace {

/// Bounds-checked reads from the model file.
class Reader {
public:
	explicit Reader(std::span<const std::byte> data) : m_data(data) {}

	[[nodiscard]] bool fits(std::size_t offset, std::size_t size) const {
		return offset <= m_data.size() && size <= m_data.size() - offset;
	}
	[[nodiscard]] int i32(std::size_t offset) const {
		if (!fits(offset, 4)) {
			return 0;
		}
		std::int32_t v;
		std::memcpy(&v, m_data.data() + offset, 4);
		return v;
	}
	[[nodiscard]] float f32(std::size_t offset) const {
		if (!fits(offset, 4)) {
			return 0;
		}
		float v;
		std::memcpy(&v, m_data.data() + offset, 4);
		return v;
	}
	/// Zero-terminated string at an absolute offset (empty if out of range).
	[[nodiscard]] std::string str(long long offset, std::size_t maxLength = 512) const {
		if (offset < 0 || static_cast<std::size_t>(offset) >= m_data.size()) {
			return {};
		}
		std::string out;
		for (auto i = static_cast<std::size_t>(offset); i < m_data.size() && out.size() < maxLength; ++i) {
			const char c = static_cast<char>(m_data[i]);
			if (c == '\0') {
				break;
			}
			out.push_back(c);
		}
		return out;
	}
	[[nodiscard]] std::size_t size() const { return m_data.size(); }

private:
	std::span<const std::byte> m_data;
};

// studiohdr_t offsets (public/studio.h, Source SDK 2013).
constexpr std::size_t HDR_VERSION = 4;
constexpr std::size_t HDR_NAME = 12;
constexpr std::size_t HDR_HULL_MIN = 104;
constexpr std::size_t HDR_FLAGS = 152;
constexpr std::size_t HDR_NUM_BONES = 156;
constexpr std::size_t HDR_NUM_SEQ = 188;
constexpr std::size_t HDR_SEQ_INDEX = 192;
constexpr std::size_t HDR_NUM_TEXTURES = 204;
constexpr std::size_t HDR_TEXTURE_INDEX = 208;
constexpr std::size_t HDR_NUM_CDTEXTURES = 212;
constexpr std::size_t HDR_CDTEXTURE_INDEX = 216;
constexpr std::size_t HDR_NUM_SKINFAMILIES = 224;
constexpr std::size_t HDR_NUM_BODYPARTS = 232;
constexpr std::size_t HDR_BODYPART_INDEX = 236;
constexpr std::size_t HDR_SURFACEPROP = 308;
constexpr std::size_t HDR_KEYVALUE_INDEX = 312;
constexpr std::size_t HDR_KEYVALUE_SIZE = 316;
constexpr std::size_t HDR_MASS = 328;
constexpr std::size_t BODYPART_SIZE = 16;     // sznameindex, nummodels, base, modelindex
constexpr std::size_t STUDIO_MODEL_SIZE = 148;
constexpr int STUDIOHDR_FLAGS_STATIC_PROP = 0x10;
constexpr std::size_t HDR_NUM_INCLUDES = 336;
constexpr std::size_t HDR_INCLUDE_INDEX = 340;
constexpr std::size_t HDR_SIZE = 408;
constexpr std::size_t SEQDESC_SIZE = 212;     // mstudioseqdesc_t
constexpr std::size_t TEXTURE_SIZE = 64;      // mstudiotexture_t
constexpr std::size_t MODELGROUP_SIZE = 8;    // mstudiomodelgroup_t
constexpr int MAX_ITEMS = 65536;

void collectSequences(const GameFileSystem& fs, const std::string& path, std::set<std::string>& seen, std::vector<std::string>& out, int depth) {
	const auto normalized = normalizePath(path);
	if (depth > 8 || !seen.insert(normalized).second) {
		return;
	}
	const auto data = fs.read(normalized);
	if (!data) {
		return;
	}
	const auto info = readModelInfo(*data);
	if (!info) {
		return;
	}
	for (const auto& sequence : info->sequences) {
		if (std::find_if(out.begin(), out.end(), [&](const std::string& s) { return iequals(s, sequence); }) == out.end()) {
			out.push_back(sequence);
		}
	}
	for (const auto& include : info->includeModels) {
		collectSequences(fs, include, seen, out, depth + 1);
	}
}

} // namespace

MaterialCategory classifyMaterial(const Material& m) {
	MaterialCategory c;
	const auto shader = toLower(m.shader);
	c.tool = m.isTool();
	c.blend = shader == "worldvertextransition" || shader == "lightmapped_4wayblend" || !m.param("$basetexture2").empty();
	c.model = shader == "vertexlitgeneric" || shader == "eyerefract" || shader == "eyes" || shader == "teeth" || m.name.starts_with("models/");
	c.water = shader == "water" || m.flag("%compilewater");
	c.translucent = m.flag("$translucent") || m.flag("$alphatest") || m.flag("$additive") || c.water;
	c.decal = m.flag("$decal") || m.name.starts_with("decals/") || m.name.starts_with("overlays/");
	c.sky = m.name.starts_with("skybox/");
	// materials/editor/ holds the editor's own sprites and helpers, not wall textures.
	c.brush = !c.model && !c.sky && !c.decal && !m.name.starts_with("editor/") && shader != "sprite" && shader != "spritecard" && shader != "refract" && shader != "unlittwotexture";
	return c;
}

bool isWaterMaterial(const GameFileSystem& fs, std::string_view name) {
	const auto material = loadMaterial(fs, name);
	// VBSP treats every shader whose name starts with "Water" as water (Water, Water_DX60, mod shaders).
	return material && (classifyMaterial(*material).water || startsWithI(material->shader, "water"));
}

std::string pickWaterMaterial(const GameFileSystem& fs, std::string_view current, const std::vector<std::string>& names) {
	if (!current.empty() && isWaterMaterial(fs, current)) {
		return std::string(current);
	}
	// Names with "water" first; then the others, but not every material of a big game (each is a file read).
	std::size_t budget = 2000;
	for (const bool named : {true, false}) {
		for (const auto& name : names) {
			if ((toLower(name).find("water") != std::string::npos) != named) continue;
			if (!named && budget-- == 0) return {};
			if (isWaterMaterial(fs, name)) return name;
		}
	}
	return {};
}

std::optional<ModelInfo> readModelInfo(std::span<const std::byte> data, std::string* error) {
	const Reader r(data);
	const auto fail = [&](const char* message) -> std::optional<ModelInfo> {
		if (error) {
			*error = message;
		}
		return std::nullopt;
	};
	if (r.size() < HDR_SIZE || std::memcmp(data.data(), "IDST", 4) != 0) {
		return fail("not a Source model (IDST header missing)");
	}
	ModelInfo info;
	info.version = r.i32(HDR_VERSION);
	if (info.version < 44 || info.version > 49) {
		return fail("unsupported model version");
	}
	info.name = r.str(HDR_NAME, 64);
	info.hullMin = {r.f32(HDR_HULL_MIN), r.f32(HDR_HULL_MIN + 4), r.f32(HDR_HULL_MIN + 8)};
	info.hullMax = {r.f32(HDR_HULL_MIN + 12), r.f32(HDR_HULL_MIN + 16), r.f32(HDR_HULL_MIN + 20)};

	const int numSeq = r.i32(HDR_NUM_SEQ);
	const int seqIndex = r.i32(HDR_SEQ_INDEX);
	if (numSeq < 0 || numSeq > MAX_ITEMS || seqIndex < 0 || !r.fits(static_cast<std::size_t>(seqIndex), static_cast<std::size_t>(numSeq) * SEQDESC_SIZE)) {
		return fail("sequence table out of range");
	}
	for (int i = 0; i < numSeq; ++i) {
		const auto base = static_cast<long long>(seqIndex) + static_cast<long long>(i) * SEQDESC_SIZE;
		info.sequences.push_back(r.str(base + r.i32(static_cast<std::size_t>(base) + 4)));
		info.activities.push_back(r.str(base + r.i32(static_cast<std::size_t>(base) + 8)));
	}

	const int numTextures = r.i32(HDR_NUM_TEXTURES);
	const int textureIndex = r.i32(HDR_TEXTURE_INDEX);
	if (numTextures > 0 && numTextures < MAX_ITEMS && textureIndex > 0 &&
	    r.fits(static_cast<std::size_t>(textureIndex), static_cast<std::size_t>(numTextures) * TEXTURE_SIZE)) {
		for (int i = 0; i < numTextures; ++i) {
			const auto base = static_cast<long long>(textureIndex) + static_cast<long long>(i) * TEXTURE_SIZE;
			info.materials.push_back(r.str(base + r.i32(static_cast<std::size_t>(base))));
		}
	}
	const int numCd = r.i32(HDR_NUM_CDTEXTURES);
	const int cdIndex = r.i32(HDR_CDTEXTURE_INDEX);
	if (numCd > 0 && numCd < MAX_ITEMS && cdIndex > 0 && r.fits(static_cast<std::size_t>(cdIndex), static_cast<std::size_t>(numCd) * 4)) {
		for (int i = 0; i < numCd; ++i) {
			info.materialDirs.push_back(r.str(r.i32(static_cast<std::size_t>(cdIndex) + static_cast<std::size_t>(i) * 4)));
		}
	}
	info.surfaceProp = r.str(r.i32(HDR_SURFACEPROP));
	info.staticProp = (r.i32(HDR_FLAGS) & STUDIOHDR_FLAGS_STATIC_PROP) != 0;
	info.mass = r.f32(HDR_MASS);
	if (!(info.mass >= 0 && info.mass < 1e7f)) info.mass = 0;   // also NaN
	info.skinCount = std::clamp(r.i32(HDR_NUM_SKINFAMILIES), 1, MAX_ITEMS);
	info.boneCount = std::clamp(r.i32(HDR_NUM_BONES), 0, MAX_ITEMS);
	const int keyValueIndex = r.i32(HDR_KEYVALUE_INDEX), keyValueSize = r.i32(HDR_KEYVALUE_SIZE);
	if (keyValueIndex > 0 && keyValueSize > 0 && r.fits(static_cast<std::size_t>(keyValueIndex), static_cast<std::size_t>(keyValueSize))) {
		info.keyValues = r.str(keyValueIndex, static_cast<std::size_t>(std::min(keyValueSize, 65536)));
	}
	// Body groups: each body part with its models (options).
	const int numParts = r.i32(HDR_NUM_BODYPARTS);
	const int partIndex = r.i32(HDR_BODYPART_INDEX);
	if (numParts > 0 && numParts <= 256 && partIndex > 0 &&
	    r.fits(static_cast<std::size_t>(partIndex), static_cast<std::size_t>(numParts) * BODYPART_SIZE)) {
		for (int i = 0; i < numParts; ++i) {
			const auto part = static_cast<long long>(partIndex) + static_cast<long long>(i) * BODYPART_SIZE;
			ModelInfo::BodyGroup group;
			group.name = r.str(part + r.i32(static_cast<std::size_t>(part)), 64);
			const int numModels = r.i32(static_cast<std::size_t>(part) + 4);
			const long long models = part + r.i32(static_cast<std::size_t>(part) + 12);
			if (numModels < 1 || numModels > 256 || models < 0 ||
			    !r.fits(static_cast<std::size_t>(models), static_cast<std::size_t>(numModels) * STUDIO_MODEL_SIZE)) {
				group.options.emplace_back();
			} else {
				for (int m = 0; m < numModels; ++m) {
					group.options.push_back(r.str(models + static_cast<long long>(m) * STUDIO_MODEL_SIZE, 64));
				}
			}
			info.bodyGroups.push_back(std::move(group));
		}
	}

	const int numIncludes = r.i32(HDR_NUM_INCLUDES);
	const int includeIndex = r.i32(HDR_INCLUDE_INDEX);
	if (numIncludes > 0 && numIncludes < 256 && includeIndex > 0 &&
	    r.fits(static_cast<std::size_t>(includeIndex), static_cast<std::size_t>(numIncludes) * MODELGROUP_SIZE)) {
		for (int i = 0; i < numIncludes; ++i) {
			const auto base = static_cast<long long>(includeIndex) + static_cast<long long>(i) * MODELGROUP_SIZE;
			auto name = r.str(base + r.i32(static_cast<std::size_t>(base) + 4));
			if (!name.empty()) {
				info.includeModels.push_back(std::move(name));
			}
		}
	}
	return info;
}

int ModelInfo::bodyValue(const std::vector<int>& choices) const {
	int body = 0, base = 1;
	for (std::size_t i = 0; i < bodyGroups.size(); ++i) {
		const int options = std::max<int>(1, static_cast<int>(bodyGroups[i].options.size()));
		const int choice = i < choices.size() ? std::clamp(choices[i], 0, options - 1) : 0;
		body += choice * base;
		if (base > (1 << 24) / options) break;   // absurd counts: stop before overflowing
		base *= options;
	}
	return body;
}

std::vector<int> ModelInfo::bodyChoices(int body) const {
	std::vector<int> choices;
	body = std::max(0, body);
	for (const auto& group : bodyGroups) {
		const int options = std::max<int>(1, static_cast<int>(group.options.size()));
		choices.push_back(body % options);
		body /= options;
	}
	return choices;
}

std::vector<std::string> modelSequences(const GameFileSystem& fs, std::string_view modelPath, std::string* error) {
	std::vector<std::string> out;
	std::set<std::string> seen;
	if (!fs.exists(modelPath)) {
		if (error) {
			*error = std::string(modelPath) + " not found";
		}
		return out;
	}
	collectSequences(fs, std::string(modelPath), seen, out, 0);
	return out;
}

std::vector<std::string> soundScriptNames(const GameFileSystem& fs) {
	std::vector<std::string> names;
	for (auto& entry : soundScripts(fs)) {
		names.push_back(std::move(entry.name));
	}
	return names;
}

std::string stripSoundChars(std::string_view wave) {
	// Characters the engine reads as flags in front of a wave name (see soundchars.h).
	constexpr std::string_view chars = "*#@><^)}$!?&~`+%";
	while (!wave.empty() && chars.find(wave.front()) != std::string_view::npos) {
		wave.remove_prefix(1);
	}
	return normalizePath(wave);
}

std::string soundFileOf(std::string_view sound, const std::vector<SoundScriptEntry>* scripts) {
	const auto name = stripSoundChars(trim(sound));
	if (name.empty()) {
		return {};
	}
	if (endsWithI(name, ".wav") || endsWithI(name, ".mp3") || endsWithI(name, ".ogg")) {
		return name;
	}
	if (scripts) {
		for (const auto& entry : *scripts) {
			if (iequals(entry.name, trim(sound)) && !entry.waves.empty()) {
				return entry.waves.front();
			}
		}
	}
	return {};
}

std::optional<bool> soundLoops(const GameFileSystem& fs, std::string_view sound, const std::vector<SoundScriptEntry>* scripts) {
	const auto file = soundFileOf(sound, scripts);
	if (file.empty()) {
		return std::nullopt;
	}
	const auto data = fs.read("sound/" + file);
	if (!data) {
		return std::nullopt;
	}
	return soundFileLoops(*data);
}

std::optional<PropModelFlags> readPropModelFlags(const GameFileSystem& fs, std::string_view modelPath) {
	if (!endsWithI(modelPath, ".mdl")) {
		return std::nullopt;
	}
	const auto data = fs.read(modelPath);
	if (!data) {
		return std::nullopt;
	}
	const auto info = readModelInfo(*data);
	if (!info) {
		return std::nullopt;
	}
	PropModelFlags flags;
	flags.staticProp = info->staticProp;
	flags.collision = fs.exists(std::string(modelPath.substr(0, modelPath.size() - 4)) + ".phy");
	flags.door = isDoorPropModel(modelPath, *info);
	return flags;
}

bool isDoorPropModel(std::string_view modelPath, const ModelInfo& info) {
	if (toLower(info.keyValues).find("door_options") != std::string::npos) {
		return true;
	}
	const std::string name = toLower(std::string(modelPath.substr(modelPath.find_last_of('/') + 1)));
	if (name.find("door") == std::string::npos) {
		return false;
	}
	for (const std::string_view word : {"frame", "gib", "chunk", "break", "elevator", "garage", "slide", "blast"}) {
		if (name.find(word) != std::string::npos) return false;
	}
	return std::any_of(info.bodyGroups.begin(), info.bodyGroups.end(), [](const ModelInfo::BodyGroup& group) {
		const std::string lower = toLower(group.name);
		return lower.find("handle") != std::string::npos || lower.find("hardware") != std::string::npos || lower.find("knob") != std::string::npos;
	});
}

std::string recommendedPropClass(const PropModelFlags& flags) {
	if (flags.door && !flags.staticProp) {
		return "prop_door_rotating";
	}
	if (flags.staticProp) {
		return "prop_static";
	}
	return flags.collision ? "prop_physics" : "prop_dynamic";
}

std::string betterPropClass(std::string_view classname, const PropModelFlags& flags) {
	if (iequals(classname, "prop_static") && !flags.staticProp) {
		return "prop_dynamic";   // stays where it was put, like the prop_static was meant to
	}
	if ((iequals(classname, "prop_physics") || iequals(classname, "prop_physics_multiplayer")) && flags.door && !flags.staticProp) {
		return "prop_door_rotating";   // a door that falls over when the map starts
	}
	if ((iequals(classname, "prop_physics") || iequals(classname, "prop_physics_multiplayer")) && !flags.collision) {
		return flags.staticProp ? "prop_static" : "prop_dynamic";
	}
	return {};
}

namespace {

std::vector<std::string> soundScriptFiles(const GameFileSystem& fs) {
	std::vector<std::string> scripts;
	if (const auto manifest = fs.readText("scripts/game_sounds_manifest.txt")) {
		try {
			for (const auto& block : parseKeyValues(*manifest).children) {
				for (const auto& entry : block.children) {
					if (!entry.block && (iequals(entry.key, "precache_file") || iequals(entry.key, "preload_file"))) {
						scripts.push_back(normalizePath(entry.value));
					}
				}
			}
		} catch (const std::exception&) {
		}
	}
	if (scripts.empty()) {
		scripts.emplace_back("scripts/game_sounds.txt");
	}
	return scripts;
}

void collectWaves(const KvNode& node, std::vector<std::string>& out) {
	for (const auto& child : node.children) {
		if (child.block) {
			collectWaves(child, out);
		} else if (iequals(child.key, "wave")) {
			out.push_back(stripSoundChars(child.value));
		}
	}
}

} // namespace

std::vector<SoundScriptEntry> soundScripts(const GameFileSystem& fs) {
	std::vector<SoundScriptEntry> entries;
	std::map<std::string, std::size_t> index;
	for (const auto& script : soundScriptFiles(fs)) {
		const auto text = fs.readText(script);
		if (!text) {
			continue;
		}
		try {
			for (const auto& block : parseKeyValues(*text).children) {
				if (!block.block || block.key.empty()) {
					continue;
				}
				SoundScriptEntry entry;
				entry.name = block.key;
				entry.script = script;
				entry.channel = block.get("channel");
				entry.volume = block.get("volume");
				entry.pitch = block.get("pitch");
				entry.soundlevel = block.get("soundlevel");
				collectWaves(block, entry.waves);
				const auto key = toLower(entry.name);
				if (const auto it = index.find(key); it != index.end()) {
					entries[it->second] = std::move(entry);
				} else {
					index.emplace(key, entries.size());
					entries.push_back(std::move(entry));
				}
			}
		} catch (const std::exception&) {
			// A broken script must not hide the others.
		}
	}
	std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return toLower(a.name) < toLower(b.name); });
	return entries;
}

namespace {

std::string kvQuoted(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c != '"') out += c;   // KeyValues has no escaping for quotes
	}
	return out + "\"";
}

/// Generic writer with every key and value quoted (sound scripts, manifests).
void writeQuotedNode(std::string& out, const KvNode& node, int depth) {
	const std::string indent(static_cast<std::size_t>(depth), '\t');
	if (node.block) {
		out += indent + kvQuoted(node.key) + "\n" + indent + "{\n";
		for (const auto& child : node.children) {
			writeQuotedNode(out, child, depth + 1);
		}
		out += indent + "}\n";
	} else {
		out += indent + kvQuoted(node.key) + "\t" + kvQuoted(node.value) + "\n";
	}
}

} // namespace

std::string writeSoundScriptEntry(const SoundScriptEntry& entry) {
	KvNode block = KvNode::makeBlock(entry.name);
	block.set("channel", entry.channel.empty() ? "CHAN_AUTO" : entry.channel);
	block.set("volume", entry.volume.empty() ? "1" : entry.volume);
	block.set("pitch", entry.pitch.empty() ? "PITCH_NORM" : entry.pitch);
	block.set("soundlevel", entry.soundlevel.empty() ? "SNDLVL_NORM" : entry.soundlevel);
	if (entry.waves.size() == 1) {
		block.set("wave", entry.waves.front());
	} else if (!entry.waves.empty()) {
		KvNode random = KvNode::makeBlock("rndwave");
		for (const auto& wave : entry.waves) {
			random.children.push_back(KvNode::makeValue("wave", wave));
		}
		block.children.push_back(std::move(random));
	}
	std::string out;
	writeQuotedNode(out, block, 0);
	return out;
}

std::string upsertSoundScriptEntry(std::string_view scriptText, const SoundScriptEntry& entry) {
	const KvNode root = parseKeyValues(scriptText);
	std::string out;
	for (const auto& node : root.children) {
		if (node.block && iequals(node.key, entry.name)) {
			continue;   // replaced below
		}
		writeQuotedNode(out, node, 0);
		out += "\n";
	}
	return out + writeSoundScriptEntry(entry);
}

std::string addToSoundManifest(std::string_view manifestText, std::string_view file) {
	KvNode root = manifestText.empty() ? KvNode{} : parseKeyValues(manifestText);
	KvNode* manifest = nullptr;
	for (auto& node : root.children) {
		if (node.block) {
			manifest = &node;
			break;
		}
	}
	if (!manifest) {
		root.children.push_back(KvNode::makeBlock("game_sounds_manifest"));
		manifest = &root.children.back();
	}
	const auto wanted = normalizePath(file);
	for (const auto& child : manifest->children) {
		if (!child.block && normalizePath(child.value) == wanted) {
			return std::string(manifestText);   // already there
		}
	}
	manifest->children.push_back(KvNode::makeValue("precache_file", std::string(file)));
	std::string out;
	for (const auto& node : root.children) {
		writeQuotedNode(out, node, 0);
	}
	return out;
}

std::vector<Soundscape> soundscapes(const GameFileSystem& fs) {
	std::vector<std::string> files;
	if (const auto manifest = fs.readText("scripts/soundscapes_manifest.txt")) {
		try {
			for (const auto& block : parseKeyValues(*manifest).children) {
				for (const auto& entry : block.children) {
					if (!entry.block && iequals(entry.key, "file")) {
						files.push_back(normalizePath(entry.value));
					}
				}
			}
		} catch (const std::exception&) {
		}
	}
	if (files.empty()) {
		for (const auto& file : fs.list("scripts", ".txt")) {
			if (file.rfind("scripts/soundscapes", 0) == 0 && file != "scripts/soundscapes_manifest.txt") {
				files.push_back(file);
			}
		}
	}
	std::vector<Soundscape> out;
	for (const auto& file : files) {
		const auto text = fs.readText(file);
		if (!text) {
			continue;
		}
		try {
			for (const auto& block : parseKeyValues(*text).children) {
				if (!block.block || block.key.empty()) {
					continue;
				}
				Soundscape scape;
				scape.name = block.key;
				scape.file = file;
				const auto dsp = block.get("dsp");
				if (!dsp.empty()) {
					scape.dsp = std::atoi(dsp.c_str());
				}
				const std::function<void(const KvNode&)> walk = [&](const KvNode& node) {
					for (const auto& child : node.children) {
						if (child.block) {
							if (iequals(child.key, "playsoundscape")) {
								const auto name = child.get("name");
								if (!name.empty()) {
									scape.includes.push_back(name);
								}
							} else {
								walk(child);
							}
						} else if (iequals(child.key, "wave")) {
							scape.sounds.push_back(stripSoundChars(child.value));
						} else if (iequals(child.key, "soundname")) {
							scape.sounds.push_back(child.value);
						}
					}
				};
				walk(block);
				out.push_back(std::move(scape));
			}
		} catch (const std::exception&) {
		}
	}
	return out;
}

std::vector<std::string> particleSystemNames(std::span<const std::byte> pcf) {
	std::vector<std::string> names;
	try {
		const kvpp::DMX dmx(pcf);
		if (!dmx) {
			return names;
		}
		for (const auto& element : dmx.getElements()) {
			if (element.getType() == "DmeParticleSystemDefinition" && !element.getKey().empty()) {
				names.emplace_back(element.getKey());
			}
		}
	} catch (const std::exception&) {
		names.clear();   // broken file: no names rather than half of them
	}
	return names;
}

std::vector<std::string> skyboxNames(const GameFileSystem& fs) {
	// The engine loads <skyname> + up/dn/lf/rt/ft/bk from materials/skybox/.
	const auto files = fs.list("materials/skybox", ".vmt");
	const std::set<std::string> present(files.begin(), files.end());
	std::vector<std::string> names;
	constexpr std::string_view prefix = "materials/skybox/";
	for (const auto& file : files) {
		if (!file.starts_with(prefix) || !file.ends_with("up.vmt")) continue;
		const std::string name = file.substr(prefix.size(), file.size() - prefix.size() - 6);
		if (name.empty() || name.find('/') != std::string::npos) continue;
		if (present.contains(std::string(prefix) + name + "ft.vmt")) names.push_back(name);
	}
	return names;   // list() is sorted
}

std::vector<ParticleSystem> particleSystems(const GameFileSystem& fs) {
	std::vector<std::string> files;
	if (const auto manifest = fs.readText("particles/particles_manifest.txt")) {
		try {
			for (const auto& block : parseKeyValues(*manifest).children) {
				for (const auto& entry : block.children) {
					if (!entry.block && iequals(entry.key, "file")) {
						std::string file = entry.value;
						if (!file.empty() && file.front() == '!') file.erase(0, 1);   // "!" = load at startup
						files.push_back(normalizePath(file));
					}
				}
			}
		} catch (const std::exception&) {
		}
	}
	if (files.empty()) {
		files = fs.list("particles", ".pcf");
	}
	std::vector<ParticleSystem> out;
	std::set<std::string> seen;
	for (const auto& file : files) {
		const auto data = fs.read(file);
		if (!data) continue;
		for (auto& name : particleSystemNames(*data)) {
			if (seen.insert(toLower(name)).second) {
				out.push_back({std::move(name), file});
			}
		}
	}
	std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return toLower(a.name) < toLower(b.name); });
	return out;
}

std::size_t editDistance(std::string_view a, std::string_view b) {
	std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
	for (std::size_t j = 0; j <= b.size(); ++j) {
		prev[j] = j;
	}
	for (std::size_t i = 1; i <= a.size(); ++i) {
		cur[0] = i;
		for (std::size_t j = 1; j <= b.size(); ++j) {
			const bool same = std::tolower(static_cast<unsigned char>(a[i - 1])) == std::tolower(static_cast<unsigned char>(b[j - 1]));
			cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (same ? 0 : 1)});
		}
		std::swap(prev, cur);
	}
	return prev[b.size()];
}

std::string closestMatch(std::string_view name, const std::vector<std::string>& candidates) {
	// Score = 2 * distance, minus 1 if the candidate starts with the name ("TurnOf" -> "TurnOff", not "TurnOn").
	// Paths also count as close when only the folder differs and the file name is nearly the same
	// ("brick/brickwal001" -> "demo/brickwall001").
	const auto baseName = [](std::string_view path) {
		const auto slash = path.rfind('/');
		return slash == std::string_view::npos ? path : path.substr(slash + 1);
	};
	const auto nameBase = baseName(name);
	const bool isPath = nameBase.size() != name.size();
	std::string best;
	const std::size_t limit = std::max<std::size_t>(3, name.size() / 4);
	std::size_t bestScore = 2 * limit + 2;
	for (const auto& candidate : candidates) {
		std::size_t d = limit + 1;
		const auto lengthGap = candidate.size() > name.size() ? candidate.size() - name.size() : name.size() - candidate.size();
		if (lengthGap <= limit) {
			d = editDistance(name, candidate);
		}
		if (isPath && d > 2) {
			const auto candidateBase = baseName(candidate);
			const auto baseGap = candidateBase.size() > nameBase.size() ? candidateBase.size() - nameBase.size() : nameBase.size() - candidateBase.size();
			if (baseGap <= 2 && nameBase.size() >= 4) {
				const auto dBase = editDistance(nameBase, candidateBase);
				if (dBase <= 2) {
					d = std::min(d, dBase + 2);
				}
			}
		}
		if (d > limit) {
			continue;
		}
		const std::size_t score = 2 * d + (startsWithI(candidate, name) ? 0 : 1);
		if (score < bestScore) {
			bestScore = score;
			best = candidate;
		}
	}
	return best;
}

} // namespace sme
