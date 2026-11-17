// material_decisions.cpp - The mod's decisions about its materials (see material_decisions.h).

#include <sme/material_decisions.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include <sme/text.h>

namespace sme {

namespace {

/// Short numbers as people write them (0.008, 2, 0.25).
std::string shortNumber(double value) {
	std::ostringstream out;
	out.precision(6);
	out << value;
	return out.str();
}

std::optional<double> parseNumber(std::string_view text) {
	const std::string s(trim(text));
	if (s.empty()) return std::nullopt;
	char* end = nullptr;
	const double v = std::strtod(s.c_str(), &end);
	if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) return std::nullopt;
	return v;
}

std::optional<int> parseInt(std::string_view text) {
	const auto v = parseNumber(text);
	if (!v || *v != std::floor(*v) || *v < 0 || *v > 1000) return std::nullopt;
	return static_cast<int>(*v);
}

/// How specific a pattern is: fixed characters count, an exact name beats every wildcard.
int specificity(std::string_view pattern) {
	int fixed = 0;
	bool wild = false;
	for (const char c : pattern) {
		if (c == '*' || c == '?') wild = true;
		else ++fixed;
	}
	return wild ? fixed : 100000 + fixed;
}

bool matchFrom(std::string_view pattern, std::string_view name) {
	// Iterative glob with backtracking on the last '*'.
	std::size_t p = 0, n = 0, star = std::string_view::npos, mark = 0;
	while (n < name.size()) {
		if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
			++p;
			++n;
		} else if (p < pattern.size() && pattern[p] == '*') {
			star = p++;
			mark = n;
		} else if (star != std::string_view::npos) {
			p = star + 1;
			n = ++mark;
		} else {
			return false;
		}
	}
	while (p < pattern.size() && pattern[p] == '*') ++p;
	return p == pattern.size();
}

/// Splits "a=b c="d e" f" into tokens, keeping quoted values whole. `comment` gets a trailing // ...
std::vector<std::string> tokens(std::string_view line, std::string* comment, bool* unterminated) {
	std::vector<std::string> out;
	std::string current;
	bool quoted = false, any = false;
	for (std::size_t i = 0; i < line.size(); ++i) {
		const char c = line[i];
		if (!quoted && c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
			if (comment) *comment = std::string(trim(line.substr(i + 2)));
			break;
		}
		if (c == '"') {
			quoted = !quoted;
			any = true;
			continue;
		}
		if (!quoted && (c == ' ' || c == '\t' || c == '\r')) {
			if (any) out.push_back(std::move(current));
			current.clear();
			any = false;
			continue;
		}
		current += c;
		any = true;
	}
	if (any) out.push_back(std::move(current));
	if (unterminated) *unterminated = quoted;
	return out;
}

bool needsQuotes(std::string_view value) {
	return value.empty() || value.find_first_of(" \t\"/") != std::string_view::npos;
}

std::string quoteValue(std::string_view value) {
	if (!needsQuotes(value)) return std::string(value);
	std::string out = "\"";
	for (const char c : value) {
		if (c != '"') out += c;
	}
	return out + "\"";
}

std::string rectsText(const std::vector<FacadeWindowRect>& rects) {
	std::string out;
	for (std::size_t i = 0; i < rects.size(); ++i) {
		if (i) out += ';';
		const auto& r = rects[i];
		out += shortNumber(r.u0) + "," + shortNumber(r.v0) + "," + shortNumber(r.u1) + "," + shortNumber(r.v1);
	}
	return out;
}

std::optional<std::vector<FacadeWindowRect>> parseRects(std::string_view text) {
	std::vector<FacadeWindowRect> out;
	for (const auto& part : splitTrimmed(text, ';')) {
		if (part.empty()) continue;
		const auto values = splitTrimmed(part, ',');
		if (values.size() != 4) return std::nullopt;
		double v[4];
		for (int i = 0; i < 4; ++i) {
			const auto n = parseNumber(values[static_cast<std::size_t>(i)]);
			if (!n || *n < -1 || *n > 3) return std::nullopt;
			v[i] = *n;
		}
		if (v[2] <= v[0] || v[3] <= v[1]) return std::nullopt;
		out.push_back({v[0], v[1], v[2], v[3]});
	}
	if (out.empty()) return std::nullopt;
	return out;
}

/// One key=value into the decision; an error message when the value is bad.
std::string setKey(MaterialDecision& d, const std::string& keyIn, const std::string& value) {
	const std::string key = toLower(keyIn);
	if (key == "role") {
		const std::string v = toLower(value);
		const auto& roles = decisionRoles();
		if (std::find(roles.begin(), roles.end(), v) == roles.end()) return "unknown role \"" + value + "\" (facade, wall, trim, roof, shop, ground, interior, unsuitable)";
		d.role = v;
	} else if (key == "windows") {
		const std::string v = toLower(value);
		const auto x = v.find('x');
		const auto c = x == std::string::npos ? std::nullopt : parseInt(std::string_view(v).substr(0, x));
		const auto r = x == std::string::npos ? std::nullopt : parseInt(std::string_view(v).substr(x + 1));
		if (!c || !r || *c > 32 || *r > 32) return "windows needs columns x rows, like windows=2x1";
		d.windowColumns = *c;
		d.windowRows = *r;
	} else if (key == "floors") {
		const auto n = parseInt(value);
		if (!n || *n < 1 || *n > 16) return "floors needs a whole number from 1 to 16";
		d.floors = *n;
	} else if (key == "windowrects") {
		auto rects = parseRects(value);
		if (!rects) return "windowrects needs u0,v0,u1,v1 per window (0..1 across one tile), windows separated by ;";
		d.windowRects = std::move(*rects);
	} else if (key == "scale") {
		const auto n = parseNumber(value);
		if (!n || *n <= 0 || *n > 16) return "scale needs a number above 0 (0.25 is the usual)";
		d.scale = *n;
	} else if (key == "parallax") {
		const auto choice = parallaxChoiceFromKey(toLower(value));
		if (!choice) return "parallax needs auto, on or off";
		d.parallax = *choice;
	} else if (key == "depth") {
		const auto n = parseNumber(value);
		if (!n || *n < 0 || *n > 0.1) return "depth needs a number from 0 to 0.1 (0.006 is shallow, 0.01 the usual)";
		d.depth = *n;
	} else if (key == "pattern") {
		const std::string v = toLower(value);
		const auto& patterns = decisionPatterns();
		if (std::find(patterns.begin(), patterns.end(), v) == patterns.end()) return "unknown pattern \"" + value + "\" (brick, tile, plank, stone, none)";
		d.pattern = v;
	} else if (key == "bump") {
		const auto choice = bumpChoiceFromKey(toLower(value));
		if (!choice) return "bump needs auto, generate or off";
		d.bump = *choice;
	} else if (key == "bumpstrength") {
		const auto n = parseNumber(value);
		if (!n || *n < BUMP_STRENGTH_MIN || *n > BUMP_STRENGTH_MAX) return "bumpstrength needs a number from 0.1 to 4 (1 is as steep as the relief)";
		d.bumpStrength = *n;
	} else if (key == "note") {
		d.note = value;
	} else {
		const auto it = std::find_if(d.extra.begin(), d.extra.end(), [&key](const auto& kv) { return kv.first == key; });
		if (it != d.extra.end()) it->second = value;
		else d.extra.emplace_back(key, value);
	}
	return {};
}

} // namespace

const char* parallaxChoiceKey(ParallaxChoice choice) {
	switch (choice) {
	case ParallaxChoice::On: return "on";
	case ParallaxChoice::Off: return "off";
	case ParallaxChoice::Auto: break;
	}
	return "auto";
}

std::optional<ParallaxChoice> parallaxChoiceFromKey(std::string_view key) {
	if (iequals(key, "auto")) return ParallaxChoice::Auto;
	if (iequals(key, "on") || iequals(key, "yes") || iequals(key, "1")) return ParallaxChoice::On;
	if (iequals(key, "off") || iequals(key, "no") || iequals(key, "0")) return ParallaxChoice::Off;
	return std::nullopt;
}

const char* bumpChoiceKey(BumpChoice choice) {
	switch (choice) {
	case BumpChoice::Generate: return "generate";
	case BumpChoice::Off: return "off";
	case BumpChoice::Auto: break;
	}
	return "auto";
}

std::optional<BumpChoice> bumpChoiceFromKey(std::string_view key) {
	if (iequals(key, "auto")) return BumpChoice::Auto;
	if (iequals(key, "generate") || iequals(key, "on") || iequals(key, "yes") || iequals(key, "1")) return BumpChoice::Generate;
	if (iequals(key, "off") || iequals(key, "no") || iequals(key, "0")) return BumpChoice::Off;
	return std::nullopt;
}

const std::vector<std::string>& decisionRoles() {
	static const std::vector<std::string> roles = {"facade", "wall", "trim", "roof", "shop", "ground", "interior", "unsuitable"};
	return roles;
}

const std::vector<std::string>& decisionPatterns() {
	static const std::vector<std::string> patterns = {"brick", "tile", "plank", "stone", "none"};
	return patterns;
}

bool MaterialDecision::empty() const {
	return !role && !windowColumns && !windowRows && !floors && !windowRects && !scale && !parallax && !depth && !pattern && !bump && !bumpStrength && !note && extra.empty();
}

void MaterialDecision::mergeFrom(const MaterialDecision& o) {
	if (o.role) role = o.role;
	if (o.windowColumns) windowColumns = o.windowColumns;
	if (o.windowRows) windowRows = o.windowRows;
	if (o.floors) floors = o.floors;
	if (o.windowRects) windowRects = o.windowRects;
	if (o.scale) scale = o.scale;
	if (o.parallax) parallax = o.parallax;
	if (o.depth) depth = o.depth;
	if (o.pattern) pattern = o.pattern;
	if (o.bump) bump = o.bump;
	if (o.bumpStrength) bumpStrength = o.bumpStrength;
	if (o.note) note = o.note;
	for (const auto& [key, value] : o.extra) {
		const auto it = std::find_if(extra.begin(), extra.end(), [&key](const auto& kv) { return kv.first == key; });
		if (it != extra.end()) it->second = value;
		else extra.emplace_back(key, value);
	}
}

std::string MaterialDecision::keysText() const {
	std::vector<std::string> parts;
	if (role) parts.push_back("role=" + *role);
	if (windowColumns && windowRows) parts.push_back("windows=" + std::to_string(*windowColumns) + "x" + std::to_string(*windowRows));
	if (floors) parts.push_back("floors=" + std::to_string(*floors));
	if (windowRects) parts.push_back("windowrects=" + rectsText(*windowRects));
	if (scale) parts.push_back("scale=" + shortNumber(*scale));
	if (parallax) parts.push_back(std::string("parallax=") + parallaxChoiceKey(*parallax));
	if (depth) parts.push_back("depth=" + shortNumber(*depth));
	if (pattern) parts.push_back("pattern=" + *pattern);
	if (bump) parts.push_back(std::string("bump=") + bumpChoiceKey(*bump));
	if (bumpStrength) parts.push_back("bumpstrength=" + shortNumber(*bumpStrength));
	if (note) parts.push_back("note=" + quoteValue(*note));
	for (const auto& [key, value] : extra) parts.push_back(key + "=" + quoteValue(value));
	std::string out;
	for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? " " : "") + parts[i];
	return out;
}

std::string decisionKey(std::string_view name) {
	std::string s = toLower(trim(name));
	std::replace(s.begin(), s.end(), '\\', '/');
	if (s.starts_with("materials/")) s.erase(0, 10);
	if (s.ends_with(".vmt")) s.resize(s.size() - 4);
	while (s.starts_with("/")) s.erase(0, 1);
	return s;
}

bool decisionPatternMatches(std::string_view pattern, std::string_view name) {
	return matchFrom(pattern, name);
}

MaterialDecisions MaterialDecisions::parse(std::string_view text) {
	MaterialDecisions out;
	int number = 0;
	std::size_t start = 0;
	while (start <= text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos) end = text.size();
		std::string_view raw = text.substr(start, end - start);
		if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
		++number;
		const bool last = end >= text.size();
		if (last && raw.empty()) break;
		Line line;
		line.raw = std::string(raw);
		bool unterminated = false;
		const auto parts = tokens(raw, &line.comment, &unterminated);
		if (!parts.empty()) {
			line.pattern = decisionKey(parts.front());
			if (line.pattern.find('=') != std::string::npos) {
				out.m_problems.push_back({number, "the line must start with a material name (or a wildcard like brick/*), not \"" + parts.front() + "\""});
				line.pattern.clear();
			}
			if (unterminated) out.m_problems.push_back({number, "a quote is not closed"});
			for (std::size_t i = 1; i < parts.size() && !line.pattern.empty(); ++i) {
				const auto eq = parts[i].find('=');
				if (eq == std::string::npos || eq == 0) {
					out.m_problems.push_back({number, "\"" + parts[i] + "\" is not key=value"});
					continue;
				}
				const std::string error = setKey(line.decision, parts[i].substr(0, eq), parts[i].substr(eq + 1));
				if (!error.empty()) out.m_problems.push_back({number, error});
			}
		}
		out.m_lines.push_back(std::move(line));
		if (last) break;
		start = end + 1;
	}
	return out;
}

std::string MaterialDecisions::serialize() const {
	std::string out;
	for (const auto& line : m_lines) {
		if (!line.changed || line.pattern.empty()) {
			out += line.raw;
		} else {
			out += line.pattern;
			const std::string keys = line.decision.keysText();
			if (!keys.empty()) out += "  " + keys;
			if (!line.comment.empty()) out += "  // " + line.comment;
		}
		out += "\n";
	}
	return out;
}

MaterialDecision MaterialDecisions::lookup(std::string_view material) const {
	const std::string name = decisionKey(material);
	std::vector<std::pair<int, std::size_t>> hits;   // specificity, line index
	for (std::size_t i = 0; i < m_lines.size(); ++i) {
		const auto& line = m_lines[i];
		if (!line.pattern.empty() && decisionPatternMatches(line.pattern, name)) hits.emplace_back(specificity(line.pattern), i);
	}
	std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });   // later lines stay later
	MaterialDecision out;
	for (const auto& [weight, index] : hits) out.mergeFrom(m_lines[index].decision);
	return out;
}

const MaterialDecision* MaterialDecisions::exact(std::string_view pattern) const {
	const std::string key = decisionKey(pattern);
	for (auto it = m_lines.rbegin(); it != m_lines.rend(); ++it) {
		if (it->pattern == key) return &it->decision;
	}
	return nullptr;
}

MaterialDecision& MaterialDecisions::lineFor(std::string_view pattern) {
	const std::string key = decisionKey(pattern);
	for (auto it = m_lines.rbegin(); it != m_lines.rend(); ++it) {
		if (it->pattern == key) {
			it->changed = true;
			return it->decision;
		}
	}
	Line line;
	line.pattern = key;
	line.changed = true;
	m_lines.push_back(std::move(line));
	return m_lines.back().decision;
}

void MaterialDecisions::cleanUp() {
	std::erase_if(m_lines, [](const Line& line) { return !line.pattern.empty() && line.changed && line.decision.empty() && line.comment.empty(); });
}

void MaterialDecisions::merge(const MaterialDecisions& incoming) {
	for (const auto& line : incoming.m_lines) {
		if (line.pattern.empty()) continue;
		MaterialDecision& d = lineFor(line.pattern);
		d.mergeFrom(line.decision);
		if (!line.comment.empty()) {
			for (auto it = m_lines.rbegin(); it != m_lines.rend(); ++it) {
				if (it->pattern == line.pattern) {
					it->comment = line.comment;
					break;
				}
			}
		}
	}
	cleanUp();
}

int MaterialDecisions::entryCount() const {
	return static_cast<int>(std::count_if(m_lines.begin(), m_lines.end(), [](const Line& line) { return !line.pattern.empty(); }));
}

std::vector<DecisionChange> compareDecisions(const MaterialDecisions& before, const MaterialDecisions& after) {
	std::map<std::string, std::string> a, b;
	std::vector<std::string> order;
	for (const auto& line : before.lines()) {
		if (line.pattern.empty()) continue;
		if (!a.count(line.pattern) && !b.count(line.pattern)) order.push_back(line.pattern);
		a[line.pattern] = before.exact(line.pattern)->keysText();
	}
	for (const auto& line : after.lines()) {
		if (line.pattern.empty()) continue;
		if (!a.count(line.pattern) && !b.count(line.pattern)) order.push_back(line.pattern);
		b[line.pattern] = after.exact(line.pattern)->keysText();
	}
	std::vector<DecisionChange> out;
	for (const auto& pattern : order) {
		const std::string x = a.count(pattern) ? a[pattern] : std::string();
		const std::string y = b.count(pattern) ? b[pattern] : std::string();
		if (x != y) out.push_back({pattern, x, y});
	}
	return out;
}

std::filesystem::path materialDecisionsFile(const std::filesystem::path& modDir) {
	return modDir / "cfg" / "mapsmith_materials.txt";
}

MaterialDecisions loadMaterialDecisions(const std::filesystem::path& modDir) {
	if (modDir.empty()) return {};
	const auto text = readFileText(materialDecisionsFile(modDir));
	return text ? MaterialDecisions::parse(*text) : MaterialDecisions{};
}

bool saveMaterialDecisions(const std::filesystem::path& modDir, const MaterialDecisions& decisions) {
	if (modDir.empty()) return false;
	const auto file = materialDecisionsFile(modDir);
	std::error_code ec;
	if (decisions.entryCount() == 0 && std::all_of(decisions.lines().begin(), decisions.lines().end(), [](const auto& l) { return trim(l.raw).empty(); })) {
		std::filesystem::remove(file, ec);
		return !std::filesystem::exists(file, ec);
	}
	std::filesystem::create_directories(file.parent_path(), ec);
	// Through a temporary file: a half-written file would lose the mapper's decisions.
	const auto temp = std::filesystem::path(file).concat(".tmp");
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		const std::string text = decisions.serialize();
		out.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!out) return false;
	}
	std::filesystem::rename(temp, file, ec);
	if (ec) {
		std::filesystem::remove(temp, ec);
		return false;
	}
	return true;
}

TextureRole decisionTextureRole(std::string_view role) {
	if (role == "facade") return TextureRole::FacadeWindows;
	if (role == "wall" || role == "interior") return TextureRole::Wall;
	if (role == "trim") return TextureRole::Trim;
	if (role == "roof") return TextureRole::Roof;
	if (role == "shop") return TextureRole::ShopFront;
	if (role == "ground" || role == "unsuitable") return TextureRole::Unsuitable;
	return TextureRole::Unknown;
}

const char* decisionRoleKey(TextureRole role) {
	switch (role) {
	case TextureRole::FacadeWindows: return "facade";
	case TextureRole::Wall: return "wall";
	case TextureRole::Trim: return "trim";
	case TextureRole::Roof: return "roof";
	case TextureRole::ShopFront: return "shop";
	case TextureRole::Unsuitable: return "unsuitable";
	case TextureRole::Unknown: break;
	}
	return "";
}

void applyMaterialDecision(TextureAnalysis& a, const MaterialDecision& d) {
	bool any = false;
	if (d.role) {
		const TextureRole role = decisionTextureRole(*d.role);
		if (role != TextureRole::Unknown) {
			a.role = role;
			a.reason = "set by the decision file";
			if (role != TextureRole::FacadeWindows && role != TextureRole::ShopFront) {
				a.windows.clear();
				a.hasWindows = false;
				a.columns = a.rows = 0;
			}
			any = true;
		}
	}
	const bool facade = a.role == TextureRole::FacadeWindows || a.role == TextureRole::ShopFront;
	if (facade && d.windowRects) {
		a.windows = *d.windowRects;
		a.hasWindows = !a.windows.empty();
		any = true;
	}
	if (facade && d.windowColumns && d.windowRows && *d.windowColumns > 0 && *d.windowRows > 0) {
		a.columns = *d.windowColumns;
		a.rows = *d.windowRows;
		if (!d.windowRects && static_cast<int>(a.windows.size()) != a.columns * a.rows) {
			// A regular grid: each window in the middle of its cell, half the cell wide, 60 % high.
			a.windows.clear();
			for (int r = 0; r < a.rows; ++r) {
				for (int c = 0; c < a.columns; ++c) {
					const double cw = 1.0 / a.columns, ch = 1.0 / a.rows;
					a.windows.push_back({(c + 0.25) * cw, (r + 0.2) * ch, (c + 0.75) * cw, (r + 0.8) * ch});
				}
			}
		}
		a.hasWindows = true;
		any = true;
	}
	if (d.floors && *d.floors > 0) {
		if (facade) a.rows = *d.floors;
		any = true;
	}
	if (d.scale && *d.scale > 0) {
		a.scaleOverride = *d.scale;
		any = true;
	}
	if (any) {
		a.decided = true;
		a.confidence = 1;
		if (a.role == TextureRole::FacadeWindows && a.windows.empty() && !d.windowColumns && !d.windowRects) {
			// "role=facade" alone on a texture where nothing was found: one window per tile.
			a.windows.push_back({0.25, 0.2, 0.75, 0.8});
			a.columns = 1;
			if (a.rows <= 0) a.rows = 1;
			a.hasWindows = true;
		}
		if (a.hasWindows && !a.windows.empty()) {
			double share = 0;
			for (const auto& w : a.windows) share += (w.u1 - w.u0) * (w.v1 - w.v0);
			a.windowShare = std::min(1.0, share);
		}
	}
}

} // namespace sme
