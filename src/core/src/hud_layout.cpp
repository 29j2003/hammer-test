// hud_layout.cpp - HudLayout.res and ClientScheme.res for the HUD editor (see hud_layout.h).

#include <sme/hud_layout.h>

#include <sme/filesystem.h>
#include <sme/text.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace sme {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t MAX_TEXT = 4u << 20;   // HudLayout/ClientScheme are a few tens of KB
constexpr int MAX_DEPTH = 64;
constexpr std::size_t MAX_NODES = 200000;
constexpr std::string_view BACKUP_SUFFIX = ".mapsmith-backup";
constexpr std::string_view RECORD_KIND = "hud";

/// A token of a .res file with its place in the text.
struct Token {
	enum class Kind { End, String, Open, Close } kind = Kind::End;
	std::string_view text;     ///< Without quotes
	bool quoted = false;
	std::size_t begin = 0;     ///< With quotes
	std::size_t end = 0;
};

class ResTokenizer {
public:
	explicit ResTokenizer(std::string_view text) : m_text(text) {
		if (m_text.starts_with("\xEF\xBB\xBF")) m_pos = 3;
	}

	Token next() {
		skip();
		Token t;
		t.begin = m_pos;
		if (m_pos >= m_text.size()) {
			t.end = m_pos;
			return t;
		}
		const char c = m_text[m_pos];
		if (c == '{' || c == '}') {
			t.kind = c == '{' ? Token::Kind::Open : Token::Kind::Close;
			t.end = ++m_pos;
			return t;
		}
		t.kind = Token::Kind::String;
		if (c == '"') {
			const std::size_t start = ++m_pos;
			while (m_pos < m_text.size() && m_text[m_pos] != '"') {
				if (m_text[m_pos] == '\n') ++m_line;
				++m_pos;
			}
			if (m_pos >= m_text.size()) throw std::runtime_error(where() + "unterminated string");
			t.text = m_text.substr(start, m_pos - start);
			t.quoted = true;
			t.end = ++m_pos;
			return t;
		}
		const std::size_t start = m_pos;
		while (m_pos < m_text.size()) {
			const char d = m_text[m_pos];
			if (d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == '{' || d == '}' || d == '"') break;
			++m_pos;
		}
		t.text = m_text.substr(start, m_pos - start);
		t.end = m_pos;
		return t;
	}

	Token peek() {
		const auto pos = m_pos;
		const auto line = m_line;
		const Token t = next();
		m_pos = pos;
		m_line = line;
		return t;
	}

	[[nodiscard]] std::string where() const { return "line " + std::to_string(m_line) + ": "; }

private:
	void skip() {
		while (m_pos < m_text.size()) {
			const char c = m_text[m_pos];
			if (c == '\n') {
				++m_line;
				++m_pos;
			} else if (c == ' ' || c == '\t' || c == '\r') {
				++m_pos;
			} else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
				while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
			} else {
				break;
			}
		}
	}

	std::string_view m_text;
	std::size_t m_pos = 0;
	int m_line = 1;
};

bool isCondition(const Token& t) {
	return t.kind == Token::Kind::String && !t.quoted && t.text.starts_with('[');
}

void parseBlock(ResTokenizer& tokens, ResNode& parent, bool top, int depth, std::size_t& count, std::vector<std::string>& includes) {
	if (depth > MAX_DEPTH) throw std::runtime_error(tokens.where() + "blocks nested too deep");
	while (true) {
		Token key = tokens.next();
		if (key.kind == Token::Kind::End) {
			if (!top) throw std::runtime_error(tokens.where() + "missing '}'");
			return;
		}
		if (key.kind == Token::Kind::Close) {
			if (top) throw std::runtime_error(tokens.where() + "unexpected '}'");
			parent.closeBrace = key.begin;
			return;
		}
		if (key.kind == Token::Kind::Open) throw std::runtime_error(tokens.where() + "unexpected '{'");
		if (++count > MAX_NODES) throw std::runtime_error("too many entries");
		ResNode node;
		node.key = std::string(key.text);
		node.depth = depth;
		Token after = tokens.peek();
		// A condition between a block's name and its '{' ("HudHealth [$WIN32] {").
		if (isCondition(after)) {
			const Token cond = tokens.next();
			if (tokens.peek().kind == Token::Kind::Open) {
				node.condition = std::string(cond.text);
				after = tokens.peek();
			} else {
				// Not a block: the "condition" was the value after all (odd but harmless).
				node.value = std::string(cond.text);
				node.valueBegin = cond.begin;
				node.valueEnd = cond.end;
				parent.children.push_back(std::move(node));
				continue;
			}
		}
		if (after.kind == Token::Kind::Open) {
			tokens.next();
			node.block = true;
			parseBlock(tokens, node, false, depth + 1, count, includes);
		} else if (after.kind == Token::Kind::String) {
			const Token value = tokens.next();
			node.value = std::string(value.text);
			node.valueBegin = value.begin;
			node.valueEnd = value.end;
			if (isCondition(tokens.peek())) node.condition = std::string(tokens.next().text);
			if (top && node.key.starts_with('#')) includes.push_back(node.value);
		} else {
			throw std::runtime_error(tokens.where() + "key '" + node.key + "' has no value");
		}
		parent.children.push_back(std::move(node));
	}
}

// --- Conditions -------------------------------------------------------------------------------

class ConditionParser {
public:
	explicit ConditionParser(std::string_view text) : m_text(text) {}

	/// nullopt on a broken expression.
	std::optional<bool> parse() {
		auto value = orExpr(0);
		skip();
		if (!value || m_pos != m_text.size()) return std::nullopt;
		return value;
	}

private:
	void skip() {
		while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
	}
	bool take(std::string_view s) {
		skip();
		if (m_text.substr(m_pos).starts_with(s)) {
			m_pos += s.size();
			return true;
		}
		return false;
	}
	std::optional<bool> orExpr(int depth) {
		auto left = andExpr(depth);
		while (left && take("||")) {
			auto right = andExpr(depth);
			if (!right) return std::nullopt;
			left = *left || *right;
		}
		return left;
	}
	std::optional<bool> andExpr(int depth) {
		auto left = unary(depth);
		while (left && take("&&")) {
			auto right = unary(depth);
			if (!right) return std::nullopt;
			left = *left && *right;
		}
		return left;
	}
	std::optional<bool> unary(int depth) {
		if (depth > 32) return std::nullopt;
		if (take("!")) {
			auto v = unary(depth + 1);
			if (!v) return std::nullopt;
			return !*v;
		}
		if (take("(")) {
			auto v = orExpr(depth + 1);
			if (!v || !take(")")) return std::nullopt;
			return v;
		}
		skip();
		if (m_pos >= m_text.size() || m_text[m_pos] != '$') return std::nullopt;
		const std::size_t start = ++m_pos;
		while (m_pos < m_text.size() && (std::isalnum(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '_')) ++m_pos;
		const auto name = toLower(m_text.substr(start, m_pos - start));
		if (name.empty()) return std::nullopt;
		// The Windows PC game: WIN32 and WINDOWS hold, every other platform symbol does not.
		return name == "win32" || name == "windows";
	}

	std::string_view m_text;
	std::size_t m_pos = 0;
};

// --- Numbers ----------------------------------------------------------------------------------

/// atoi/atof on the text from `at` (as the C runtime: leading number, else 0).
int leadingInt(std::string_view text) {
	return std::atoi(std::string(text).c_str());
}
double leadingDouble(std::string_view text) {
	return std::atof(std::string(text).c_str());
}

std::string numberText(double value) {
	const long long rounded = std::llround(value);
	return std::to_string(rounded);
}

bool startsWithChar(std::string_view text, char lower) {
	return !text.empty() && std::tolower(static_cast<unsigned char>(text.front())) == lower;
}

/// vgui Panel::ComputeWide/ComputeTall for one axis (proportional panel, parent = screen).
int computeSize(std::string_view text, int parentSize, int screenTall, int otherPixels) {
	if (text.empty()) return 0;
	enum { Plain, Full, Other, Proportional } mode = Plain;
	if (startsWithChar(text, 'f')) {
		mode = Full;
		text.remove_prefix(1);
	} else if (startsWithChar(text, 'o')) {
		mode = Other;
		text.remove_prefix(1);
	} else if (startsWithChar(text, 'p')) {
		mode = Proportional;
		text.remove_prefix(1);
	}
	const double fraction = leadingDouble(text);
	switch (mode) {
	case Other: return static_cast<int>(otherPixels * fraction);
	case Proportional: return static_cast<int>((parentSize - hudScaled(leadingInt(text), screenTall)) * fraction);
	case Full: return parentSize - hudScaled(leadingInt(text), screenTall);
	case Plain: break;
	}
	return hudScaled(leadingInt(text), screenTall);
}

/// vgui Panel::ComputePos for one axis.
int computePos(std::string_view text, int size, int parentSize, int screenTall) {
	if (text.empty()) return 0;
	HudAnchor anchor = HudAnchor::Start;
	if (startsWithChar(text, 'r')) {
		anchor = HudAnchor::End;
		text.remove_prefix(1);
	} else if (startsWithChar(text, 'c')) {
		anchor = HudAnchor::Center;
		text.remove_prefix(1);
	}
	enum { Absolute, Self, Parent } of = Absolute;
	if (startsWithChar(text, 's')) {
		of = Self;
		text.remove_prefix(1);
	} else if (startsWithChar(text, 'p')) {
		of = Parent;
		text.remove_prefix(1);
	}
	const double fraction = leadingDouble(text);
	int delta = 0;
	if (of == Self) {
		delta = static_cast<int>(size * fraction);
	} else if (of == Parent) {
		delta = static_cast<int>(parentSize * fraction);
	} else {
		delta = hudScaled(leadingInt(text), screenTall);
	}
	if (anchor == HudAnchor::End) return parentSize - delta;
	if (anchor == HudAnchor::Center) return parentSize / 2 + delta;
	return delta;
}

const ResNode* firstActiveBlock(const ResNode& parent) {
	for (const auto& child : parent.children) {
		if (child.block && child.active()) return &child;
	}
	return nullptr;
}

std::string quotedValue(std::string_view value) {
	std::string out = "\"";
	for (const char c : value) out += c == '"' ? '\'' : (c == '\n' || c == '\r') ? ' ' : c;
	out += '"';
	return out;
}

// --- Files ------------------------------------------------------------------------------------

bool writeAtomically(const fs::path& file, std::string_view text, std::string* error) {
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	const fs::path temp = file.string() + ".mapsmith-tmp";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size()))) {
			if (error) *error = "cannot write " + displayPath(file);
			return false;
		}
	}
	fs::rename(temp, file, ec);
	if (ec) {
		fs::remove(temp, ec);
		if (error) *error = "cannot write " + displayPath(file);
		return false;
	}
	return true;
}

std::string slashes(std::string path) {
	std::replace(path.begin(), path.end(), '\\', '/');
	return path;
}

std::optional<ImportRecord> hudRecord(const fs::path& modDir) {
	for (const auto& record : loadImports(modDir)) {
		if (record.kind == RECORD_KIND) return record;
	}
	return std::nullopt;
}

} // namespace

// --- ResNode / ResFile ------------------------------------------------------------------------

bool ResNode::active() const {
	return condition.empty() || resConditionTrue(condition);
}

const ResNode* ResNode::find(std::string_view childKey) const {
	for (const auto& child : children) {
		if (iequals(child.key, childKey) && child.active()) return &child;
	}
	return nullptr;
}

std::string ResNode::get(std::string_view childKey, std::string_view fallback) const {
	for (const auto& child : children) {
		if (!child.block && iequals(child.key, childKey) && child.active()) return child.value;
	}
	return std::string(fallback);
}

std::optional<ResFile> parseResFile(std::string_view text, std::string* error) {
	if (text.size() > MAX_TEXT) {
		if (error) *error = "the file is too large";
		return std::nullopt;
	}
	try {
		ResFile file;
		file.text = std::string(text);
		ResTokenizer tokens(file.text);
		std::size_t count = 0;
		file.root.block = true;
		file.root.depth = -1;
		parseBlock(tokens, file.root, true, 0, count, file.includes);
		file.root.closeBrace = file.text.size();
		return file;
	} catch (const std::exception& e) {
		if (error) *error = e.what();
		return std::nullopt;
	}
}

bool resConditionTrue(std::string_view condition) {
	condition = trim(condition);
	if (condition.size() < 2 || condition.front() != '[' || condition.back() != ']') return false;
	condition = condition.substr(1, condition.size() - 2);
	return ConditionParser(condition).parse().value_or(false);
}

std::string withResValue(const ResFile& file, const ResNode& block, std::string_view key, std::string_view value) {
	const std::string& text = file.text;
	for (const auto& child : block.children) {
		if (!child.block && iequals(child.key, key) && child.active() && child.valueEnd <= text.size() && child.valueBegin < child.valueEnd) {
			return text.substr(0, child.valueBegin) + quotedValue(value) + text.substr(child.valueEnd);
		}
	}
	// A new line just before the block's '}' (at the end of the file for the root).
	const std::size_t brace = std::min(block.closeBrace, text.size());
	const std::string line = "\"" + std::string(key) + "\"\t" + quotedValue(value);
	std::size_t lineStart = text.rfind('\n', brace == 0 ? 0 : brace - 1);
	lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
	if (brace == 0 || lineStart > brace) lineStart = brace;
	const std::string_view before(text.data() + lineStart, brace - lineStart);
	const bool onlySpace = std::all_of(before.begin(), before.end(), [](char c) { return c == ' ' || c == '\t'; });
	const std::string indent(static_cast<std::size_t>(std::max(0, block.depth + 1)), '\t');
	if (onlySpace) {
		return text.substr(0, lineStart) + indent + line + "\n" + text.substr(lineStart);
	}
	return text.substr(0, brace) + "\n" + indent + line + "\n" + text.substr(brace);
}

const ResNode* findResBlock(const ResFile& file, const std::vector<std::string_view>& path) {
	const ResNode* node = &file.root;
	for (const auto name : path) {
		const ResNode* next = nullptr;
		for (const auto& child : node->children) {
			if (child.block && iequals(child.key, name) && child.active()) {
				next = &child;
				break;
			}
		}
		if (!next) return nullptr;
		node = next;
	}
	return node;
}

// --- Positions --------------------------------------------------------------------------------

int hudScaled(double value, int screenTall) {
	if (screenTall <= 0) return 0;
	return static_cast<int>(value * static_cast<double>(screenTall) / 480.0);
}

HudRect resolveHudRect(std::string_view xpos, std::string_view ypos, std::string_view wide, std::string_view tall, int screenWide, int screenTall) {
	HudRect r;
	xpos = trim(xpos);
	ypos = trim(ypos);
	wide = trim(wide);
	tall = trim(tall);
	// "o" sizes take the other axis: compute that one first.
	const bool wideFromTall = startsWithChar(wide, 'o');
	if (wideFromTall) {
		r.tall = computeSize(tall, screenTall, screenTall, 0);
		r.wide = computeSize(wide, screenWide, screenTall, r.tall);
	} else {
		r.wide = computeSize(wide, screenWide, screenTall, 0);
		r.tall = computeSize(tall, screenTall, screenTall, r.wide);
	}
	r.x = computePos(xpos, r.wide, screenWide, screenTall);
	r.y = computePos(ypos, r.tall, screenTall, screenTall);
	return r;
}

HudAnchor hudAnchor(std::string_view text) {
	text = trim(text);
	if (startsWithChar(text, 'r')) return HudAnchor::End;
	if (startsWithChar(text, 'c')) return HudAnchor::Center;
	return HudAnchor::Start;
}

double hudPositionNumber(std::string_view text) {
	text = trim(text);
	if (startsWithChar(text, 'r') || startsWithChar(text, 'c')) text.remove_prefix(1);
	if (startsWithChar(text, 's') || startsWithChar(text, 'p')) text.remove_prefix(1);
	return leadingDouble(text);
}

std::string hudPositionFrom(HudAnchor anchor, double number) {
	const std::string n = numberText(number);
	if (anchor == HudAnchor::End) return "r" + n;
	if (anchor == HudAnchor::Center) return "c" + n;
	return n;
}

std::string hudPositionText(std::string_view oldText, int pixel, int, int screenSize, int screenTall) {
	if (screenTall <= 0) return std::string(oldText);
	const HudAnchor anchor = hudAnchor(oldText);
	double delta = pixel;
	if (anchor == HudAnchor::End) delta = screenSize - pixel;
	if (anchor == HudAnchor::Center) delta = pixel - screenSize / 2;
	return hudPositionFrom(anchor, delta * 480.0 / screenTall);
}

std::string hudSizeText(std::string_view oldText, int pixels, int screenSize, int screenTall) {
	if (screenTall <= 0) return std::string(oldText);
	if (startsWithChar(trim(oldText), 'f')) return "f" + numberText(std::max(0, screenSize - pixels) * 480.0 / screenTall);
	return numberText(std::max(0, pixels) * 480.0 / screenTall);
}

// --- HudLayout.res ----------------------------------------------------------------------------

const ResNode* hudElementBlock(const ResFile& layout, std::string_view name) {
	const ResNode* top = firstActiveBlock(layout.root);
	if (!top) return nullptr;
	for (const auto& child : top->children) {
		if (child.block && child.active() && iequals(child.key, name)) return &child;
	}
	return nullptr;
}

std::vector<HudElement> hudElements(const ResFile& layout) {
	std::vector<HudElement> out;
	const ResNode* top = firstActiveBlock(layout.root);
	if (!top) return out;
	for (const auto& child : top->children) {
		if (!child.block || !child.active()) continue;
		if (std::any_of(out.begin(), out.end(), [&](const HudElement& e) { return iequals(e.name, child.key); })) continue;
		HudElement e;
		e.name = child.key;
		e.xpos = child.get("xpos");
		e.ypos = child.get("ypos");
		e.wide = child.get("wide");
		e.tall = child.get("tall");
		e.visible = trim(child.get("visible", "1")) != "0";
		e.hasPosition = !trim(e.xpos).empty() && !trim(e.ypos).empty();
		const auto zero = [](std::string_view v) { return trim(v).empty() || trim(v) == "0"; };
		e.fullScreen = ((trim(e.wide) == "640" && trim(e.tall) == "480") || (trim(e.wide) == "f0" && trim(e.tall) == "f0")) && zero(e.xpos) && zero(e.ypos);
		out.push_back(std::move(e));
	}
	return out;
}

HudElementInfo hudElementInfo(std::string_view name) {
	struct Known {
		std::string_view name;
		HudElementInfo info;
	};
	static const Known KNOWN[] = {
	    {"HudHealth", {"Health", "HEALTH", "100", "", "Health of the player"}},
	    {"HudSuit", {"Suit", "SUIT", "75", "", "Suit armour"}},
	    {"HudAmmo", {"Ammo", "AMMO", "45", "180", "Ammo in the weapon and in reserve"}},
	    {"HudAmmoSecondary", {"Alt ammo", "ALT", "3", "", "Second ammo (AR2 balls, SMG grenades)"}},
	    {"HudSuitPower", {"Aux power", "AUX POWER", "", "", "Sprint, flashlight and oxygen"}},
	    {"HudFlashlight", {"Flashlight", "", "", "", "Flashlight battery (when Aux power does not show it)"}},
	    {"HudPosture", {"Posture", "", "", "", "Crouch indicator (consoles)"}},
	    {"HudDamageIndicator", {"Damage indicator", "", "", "", "Red edges when hit"}},
	    {"HudZoom", {"Zoom", "", "", "", "Suit zoom overlay"}},
	    {"HudWeaponSelection", {"Weapon selection", "", "", "", "Weapon boxes at the top"}},
	    {"HudCrosshair", {"Crosshair", "", "", "", "Crosshair in the middle"}},
	    {"HUDQuickInfo", {"Quick info", "", "", "", "Health/ammo arcs around the crosshair"}},
	    {"HudCloseCaption", {"Captions", "", "", "", "Subtitles and captions"}},
	    {"HudHistoryResource", {"Pickups", "", "", "", "Ammo and items just picked up"}},
	    {"HudHintDisplay", {"Hint", "", "", "", "Hints such as \"Press E to use\""}},
	    {"HudHintKeyDisplay", {"Key hint", "", "", "", "Key hints"}},
	    {"HudGeiger", {"Geiger counter", "", "", "", "Radiation sound (no picture)"}},
	    {"HudTrain", {"Train", "", "", "", "func_tracktrain speed"}},
	    {"HudVehicle", {"Vehicle", "", "", "", "Vehicle display"}},
	    {"HudMessage", {"Messages", "", "", "", "game_text messages"}},
	    {"HudMenu", {"Menu", "", "", "", "Number menus"}},
	    {"HudChat", {"Chat", "", "", "", "Chat (multiplayer)"}},
	    {"HudDeathNotice", {"Death notices", "", "", "", "Kill feed (multiplayer)"}},
	    {"HudLocator", {"Locator", "", "", "", "Jalopy locator (Episode Two)"}},
	    {"HudCredits", {"Credits", "", "", "", "End credits"}},
	};
	for (const auto& known : KNOWN) {
		if (iequals(known.name, name)) return known.info;
	}
	return {"", "", "", "", "HUD element"};
}

// --- ClientScheme.res -------------------------------------------------------------------------

std::optional<HudColor> parseHudColor(std::string_view text) {
	HudColor c{0, 0, 0, 255};
	int count = 0;
	std::size_t i = 0;
	text = trim(text);
	while (i < text.size()) {
		while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
		if (i >= text.size()) break;
		const std::size_t start = i;
		while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
		const auto part = text.substr(start, i - start);
		if (part.empty() || part.size() > 4 || !std::all_of(part.begin(), part.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); })) {
			return std::nullopt;
		}
		if (count >= 4) return std::nullopt;
		c[static_cast<std::size_t>(count++)] = std::clamp(std::atoi(std::string(part).c_str()), 0, 255);
	}
	if (count < 3) return std::nullopt;
	return c;
}

std::string hudColorText(const HudColor& color) {
	return std::to_string(color[0]) + " " + std::to_string(color[1]) + " " + std::to_string(color[2]) + " " + std::to_string(color[3]);
}

namespace {

const ResNode* schemeSection(const ResFile& scheme, std::string_view section) {
	const ResNode* top = firstActiveBlock(scheme.root);
	return top ? top->find(section) : nullptr;
}

} // namespace

std::optional<HudColor> resolveSchemeColor(const ResFile& scheme, std::string_view nameOrColor) {
	std::string current(trim(nameOrColor));
	const ResNode* base = schemeSection(scheme, "BaseSettings");
	const ResNode* colors = schemeSection(scheme, "Colors");
	for (int i = 0; i < 8 && !current.empty(); ++i) {
		if (auto c = parseHudColor(current)) return c;
		std::string next;
		if (base && base->find(current) && !base->find(current)->block) next = base->get(current);
		if (next.empty() && colors && colors->find(current) && !colors->find(current)->block) next = colors->get(current);
		if (next.empty() || iequals(next, current)) return std::nullopt;
		current = std::string(trim(next));
	}
	return std::nullopt;
}

std::vector<SchemeColor> schemeColors(const ResFile& scheme) {
	std::vector<SchemeColor> out;
	for (const std::string_view section : {"BaseSettings", "Colors"}) {
		const ResNode* block = schemeSection(scheme, section);
		if (!block) continue;
		for (const auto& child : block->children) {
			if (child.block || !child.active()) continue;
			if (std::any_of(out.begin(), out.end(), [&](const SchemeColor& c) { return c.section == section && iequals(c.name, child.key); })) continue;
			auto color = resolveSchemeColor(scheme, child.value);
			if (!color) continue;   // numbers and texts of BaseSettings (Main.Title1.X ...)
			out.push_back({child.key, std::string(section), child.value, color});
		}
	}
	return out;
}

std::vector<SchemeFont> schemeFonts(const ResFile& scheme) {
	std::vector<SchemeFont> out;
	const ResNode* fonts = schemeSection(scheme, "Fonts");
	if (!fonts) return out;
	for (const auto& fontNode : fonts->children) {
		if (!fontNode.block || !fontNode.active()) continue;
		if (std::any_of(out.begin(), out.end(), [&](const SchemeFont& f) { return iequals(f.name, fontNode.key); })) continue;
		SchemeFont font;
		font.name = fontNode.key;
		for (const auto& entryNode : fontNode.children) {
			if (!entryNode.block || !entryNode.active()) continue;
			SchemeFontEntry e;
			e.id = entryNode.key;
			e.family = entryNode.get("name");
			e.tall = std::clamp(leadingInt(entryNode.get("tall", "12")), 0, 1000);
			e.weight = std::clamp(leadingInt(entryNode.get("weight", "400")), 0, 1000);
			e.additive = leadingInt(entryNode.get("additive")) != 0;
			e.antialias = leadingInt(entryNode.get("antialias")) != 0;
			e.custom = leadingInt(entryNode.get("custom")) != 0;
			e.blur = std::clamp(leadingInt(entryNode.get("blur")), 0, 64);
			e.scanlines = std::clamp(leadingInt(entryNode.get("scanlines")), 0, 64);
			const std::string yres(trim(entryNode.get("yres")));
			if (!yres.empty()) {
				const auto space = yres.find_first_of(" \t");
				e.yresMin = std::max(0, leadingInt(yres));
				e.yresMax = space == std::string::npos ? e.yresMin : std::max(0, leadingInt(trim(std::string_view(yres).substr(space))));
			}
			font.entries.push_back(std::move(e));
		}
		out.push_back(std::move(font));
	}
	return out;
}

const SchemeFontEntry* pickFontEntry(const SchemeFont& font, int screenTall) {
	for (const auto& e : font.entries) {
		if ((e.yresMin || e.yresMax) && screenTall >= e.yresMin && screenTall <= e.yresMax) return &e;
	}
	for (const auto& e : font.entries) {
		if (!e.yresMin && !e.yresMax) return &e;
	}
	return nullptr;
}

int fontPixelTall(const SchemeFontEntry& entry, int screenTall) {
	if (entry.yresMin || entry.yresMax) return entry.tall;
	return std::max(1, hudScaled(entry.tall, screenTall));
}

std::vector<std::string> schemeFontFiles(const ResFile& scheme) {
	std::vector<std::string> out;
	const ResNode* files = schemeSection(scheme, "CustomFontFiles");
	if (!files) return out;
	for (const auto& child : files->children) {
		if (!child.active()) continue;
		// "1" "resource/HALFLIFE2.ttf" or "1" { "font" "resource/x.ttf" ... } (newer engines).
		std::string path = child.block ? child.get("font") : child.value;
		path = slashes(std::string(trim(path)));
		if (!path.empty() && isSafeGamePath(path)) out.push_back(path);
	}
	return out;
}

// --- Files in the mod -------------------------------------------------------------------------

bool saveHudFile(const fs::path& modDir, std::string_view relativePath, std::string_view text, std::string* error) {
	const std::string relative = slashes(std::string(relativePath));
	if (!isSafeGamePath(relative)) {
		if (error) *error = "not a file of the mod: " + relative;
		return false;
	}
	const fs::path file = modDir / fs::path(relative);
	auto old = hudRecord(modDir);
	ImportRecord record = old.value_or(ImportRecord{std::string(RECORD_KIND), "hud", {}, {}, {}});
	const auto listed = [&file](const std::vector<fs::path>& files) {
		return std::any_of(files.begin(), files.end(), [&file](const fs::path& f) { return f.lexically_normal() == file.lexically_normal(); });
	};
	std::error_code ec;
	if (!listed(record.created) && !listed(record.replaced)) {
		if (fs::exists(file, ec)) {
			const fs::path backup = file.string() + std::string(BACKUP_SUFFIX);
			if (!fs::exists(backup, ec)) {
				fs::copy_file(file, backup, ec);
				if (ec) {
					if (error) *error = "cannot keep a backup of " + displayPath(file);
					return false;
				}
			}
			record.replaced.push_back(file);
		} else {
			record.created.push_back(file);
		}
	}
	if (!writeAtomically(file, text, error)) return false;
	if (!old || !(*old == record)) {
		if (old) (void)forgetImport(modDir, *old);
		if (!rememberImport(modDir, record)) {
			if (error) *error = "cannot write " + displayPath(importLogFile(modDir));
			return false;
		}
	}
	return true;
}

bool restoreHudFiles(const fs::path& modDir, std::string* error) {
	const auto record = hudRecord(modDir);
	if (!record) return true;
	if (!undoImport(*record, error)) return false;
	return forgetImport(modDir, *record);
}

bool hudFilesChanged(const fs::path& modDir) {
	return hudRecord(modDir).has_value();
}

} // namespace sme
