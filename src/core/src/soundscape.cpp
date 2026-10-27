// soundscape.cpp - Soundscape scripts: read, edit, write (see soundscape.h).

#include <sme/soundscape.h>

#include <sme/text.h>

namespace sme {

namespace {

std::string kvQuote(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c != '"') out += c;   // KeyValues has no escape for quotes
	}
	return out + "\"";
}

void writeNode(std::string& out, const KvNode& node, int depth) {
	const std::string indent(static_cast<std::size_t>(depth), '\t');
	if (node.block) {
		out += indent + kvQuote(node.key) + "\n" + indent + "{\n";
		for (const auto& child : node.children) writeNode(out, child, depth + 1);
		out += indent + "}\n";
	} else {
		out += indent + kvQuote(node.key) + "\t" + kvQuote(node.value) + "\n";
	}
}

SoundscapeRule readRule(const KvNode& block) {
	using Kind = SoundscapeRule::Kind;
	SoundscapeRule rule;
	rule.key = block.key;
	rule.kind = iequals(block.key, "playlooping")      ? Kind::Looping
	            : iequals(block.key, "playrandom")     ? Kind::Random
	            : iequals(block.key, "playsoundscape") ? Kind::Soundscape
	                                                   : Kind::Other;
	if (rule.kind == Kind::Other) {
		rule.extra = block.children;   // unknown rule: kept whole
		return rule;
	}
	for (const auto& child : block.children) {
		const auto& k = child.key;
		if (child.block) {
			if (iequals(k, "rndwave") && rule.kind == Kind::Random) {
				for (const auto& wave : child.children) {
					if (!wave.block && iequals(wave.key, "wave")) {
						rule.waves.push_back(wave.value);
					} else {
						rule.extra.push_back(wave);   // rare: kept, written after the waves' block
					}
				}
			} else {
				rule.extra.push_back(child);
			}
		} else if (iequals(k, "volume")) {
			rule.volume = child.value;
		} else if (iequals(k, "pitch")) {
			rule.pitch = child.value;
		} else if (iequals(k, "soundlevel")) {
			rule.soundlevel = child.value;
		} else if (iequals(k, "position")) {
			rule.position = child.value;
		} else if (iequals(k, "time") && rule.kind == Kind::Random) {
			rule.time = child.value;
		} else if (iequals(k, "wave") && rule.kind == Kind::Looping) {
			rule.wave = child.value;
		} else if (iequals(k, "name") && rule.kind == Kind::Soundscape) {
			rule.name = child.value;
		} else if (iequals(k, "positionoverride")) {
			rule.positionOverride = child.value;
		} else if (iequals(k, "ambientpositionoverride")) {
			rule.ambientPositionOverride = child.value;
		} else {
			rule.extra.push_back(child);
		}
	}
	return rule;
}

KvNode ruleNode(const SoundscapeRule& rule) {
	using Kind = SoundscapeRule::Kind;
	const char* key = rule.kind == Kind::Looping ? "playlooping" : rule.kind == Kind::Random ? "playrandom" : "playsoundscape";
	KvNode block = KvNode::makeBlock(rule.kind == Kind::Other ? rule.key : key);
	const auto put = [&](const char* name, const std::string& value) {
		if (!value.empty()) block.children.push_back(KvNode::makeValue(name, value));
	};
	if (rule.kind != Kind::Other) {
		if (rule.kind == Kind::Soundscape) put("name", rule.name);
		if (rule.kind == Kind::Random) put("time", rule.time);
		put("volume", rule.volume);
		put("pitch", rule.pitch);
		put("soundlevel", rule.soundlevel);
		put("position", rule.position);
		put("positionoverride", rule.positionOverride);
		put("ambientpositionoverride", rule.ambientPositionOverride);
		if (rule.kind == Kind::Looping) put("wave", rule.wave);
		if (rule.kind == Kind::Random && !rule.waves.empty()) {
			KvNode random = KvNode::makeBlock("rndwave");
			for (const auto& wave : rule.waves) random.children.push_back(KvNode::makeValue("wave", wave));
			block.children.push_back(std::move(random));
		}
	}
	for (const auto& node : rule.extra) block.children.push_back(node);
	return block;
}

KvNode scapeNode(const SoundscapeScript& scape) {
	KvNode block = KvNode::makeBlock(scape.name);
	if (!scape.dsp.empty()) block.children.push_back(KvNode::makeValue("dsp", scape.dsp));
	if (!scape.fadetime.empty()) block.children.push_back(KvNode::makeValue("fadetime", scape.fadetime));
	for (const auto& node : scape.extra) block.children.push_back(node);
	for (const auto& rule : scape.rules) block.children.push_back(ruleNode(rule));
	return block;
}

SoundscapeScript readScape(const KvNode& block) {
	SoundscapeScript scape;
	scape.name = block.key;
	for (const auto& child : block.children) {
		if (child.block) {
			scape.rules.push_back(readRule(child));
		} else if (iequals(child.key, "dsp")) {
			scape.dsp = child.value;
		} else if (iequals(child.key, "fadetime")) {
			scape.fadetime = child.value;
		} else {
			scape.extra.push_back(child);
		}
	}
	return scape;
}

// Names of Source's room presets 0-28 (dsp_presets.txt / the dsp_player list on the Valve wiki).
constexpr DspPreset DSP_PRESETS[] = {
	{0, "Normal (no effect)"}, {1, "Generic"},         {2, "Metal, small"},      {3, "Metal, medium"},     {4, "Metal, large"},
	{5, "Tunnel, small"},      {6, "Tunnel, medium"},  {7, "Tunnel, large"},     {8, "Chamber, small"},    {9, "Chamber, medium"},
	{10, "Chamber, large"},    {11, "Bright, small"},  {12, "Bright, medium"},   {13, "Bright, large"},    {14, "Water 1"},
	{15, "Water 2"},           {16, "Water 3"},        {17, "Concrete, small"},  {18, "Concrete, medium"}, {19, "Concrete, large"},
	{20, "Big 1"},             {21, "Big 2"},          {22, "Big 3"},            {23, "Cavern, small"},    {24, "Cavern, medium"},
	{25, "Cavern, large"},     {26, "Weirdo 1"},       {27, "Weirdo 2"},         {28, "Weirdo 3"},
};

} // namespace

std::vector<SoundscapeScript> parseSoundscapes(std::string_view text) {
	std::vector<SoundscapeScript> out;
	for (const auto& node : parseKeyValues(text).children) {
		if (node.block && !node.key.empty()) out.push_back(readScape(node));
	}
	return out;
}

std::string writeSoundscape(const SoundscapeScript& scape) {
	std::string out;
	writeNode(out, scapeNode(scape), 0);
	return out;
}

std::string upsertSoundscape(std::string_view text, const SoundscapeScript& scape) {
	const KvNode root = parseKeyValues(text);
	std::string out;
	bool replaced = false;
	for (const auto& node : root.children) {
		if (!out.empty()) out += "\n";
		if (node.block && !replaced && iequals(node.key, scape.name)) {
			out += writeSoundscape(scape);
			replaced = true;
		} else {
			writeNode(out, node, 0);
		}
	}
	if (!replaced) {
		if (!out.empty()) out += "\n";
		out += writeSoundscape(scape);
	}
	return out;
}

std::string removeSoundscape(std::string_view text, std::string_view name) {
	const KvNode root = parseKeyValues(text);
	std::string out;
	for (const auto& node : root.children) {
		if (node.block && iequals(node.key, name)) continue;
		if (!out.empty()) out += "\n";
		writeNode(out, node, 0);
	}
	return out;
}

std::string addToSoundscapeManifest(std::string_view manifestText, std::string_view file, bool first) {
	KvNode root = manifestText.empty() ? KvNode{} : parseKeyValues(manifestText);
	KvNode* manifest = nullptr;
	for (auto& node : root.children) {
		if (node.block) {
			manifest = &node;
			break;
		}
	}
	if (!manifest) {
		root.children.push_back(KvNode::makeBlock("soundscapes_manifest"));
		manifest = &root.children.back();
	}
	const auto wanted = normalizePath(file);
	for (const auto& child : manifest->children) {
		if (!child.block && normalizePath(child.value) == wanted) {
			return std::string(manifestText);   // already there
		}
	}
	auto entry = KvNode::makeValue("file", std::string(file));
	if (first) {
		manifest->children.insert(manifest->children.begin(), std::move(entry));
	} else {
		manifest->children.push_back(std::move(entry));
	}
	std::string out;
	for (const auto& node : root.children) writeNode(out, node, 0);
	return out;
}

std::span<const DspPreset> dspPresets() {
	return DSP_PRESETS;
}

} // namespace sme
