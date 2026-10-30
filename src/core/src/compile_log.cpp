// compile_log.cpp - Compile output classification (see compile_log.h).

#include <sme/compile_log.h>

#include <sme/text.h>

#include <array>
#include <cstring>
#include <cstdint>
#include <cctype>

namespace sme {

namespace {

struct KnownMessage {
	std::string_view pattern;   ///< Lowercase text the line contains
	CompileMessage::Severity severity;
	std::string_view explanation;
	bool leak = false;
};

// Most specific first. Wording of the tools from the Source SDK 2013 sources (vbsp, vvis, vrad).
constexpr std::array KNOWN = {
	KnownMessage{"leaked", CompileMessage::Severity::Error,
	             "The map is not sealed: there is a hole to the void. Follow the red line in the 3D view from the entity to the hole "
	             "and close it with a brush (Problems shows the leak). Without a sealed map there is no VVIS and only fullbright lighting.",
	             true},
	KnownMessage{"**** leak", CompileMessage::Severity::Error, "The map is not sealed (see the red leak line in the 3D view).", true},
	// Harmless notes that contain alarming words.
	KnownMessage{"texlight file", CompileMessage::Severity::Info,
	             "lights.rad (materials that glow by themselves) was not found. Harmless: only those materials would give light."},
	KnownMessage{"could not find lights.rad", CompileMessage::Severity::Info, "Harmless: lights.rad is optional."},
	KnownMessage{"unknown light specifier", CompileMessage::Severity::Info, "Harmless note of VRAD."},
	KnownMessage{"could not locate 'gamedata'", CompileMessage::Severity::Info,
	             "gameinfo.txt names no FGD for Hammer. Harmless for compiling (the editor finds FGDs itself)."},
	KnownMessage{"run buildcubemaps", CompileMessage::Severity::Info, "Type buildcubemaps in the game console once to bake reflections."},
	// Common errors, explained (wording of VBSP/VVIS/VRAD; the Valve wiki's "Compile Errors" page).
	KnownMessage{"duplication of '", CompileMessage::Severity::Warning,
	             "A VRAD lights file has a line it cannot read (older fidelity_lights.rad had comment lines). Harmless; Install features "
	             "again (Project > Features) to write the file anew."},
	KnownMessage{"texture axis perpendicular to face", CompileMessage::Severity::Error,
	             "A face's texture lies edge-on to it (its texture axes run into the face), so it cannot be drawn. Select the face (Face mode), "
	             "then Justify > Fit or reset its alignment."},
	KnownMessage{"bad surface extents", CompileMessage::Severity::Error,
	             "A face is too large for its lightmap scale (or its texture scale is tiny). Raise the face's lightmap scale, or cut the face "
	             "into smaller pieces; Problems lists extreme scales."},
	KnownMessage{"max_points_on_winding", CompileMessage::Severity::Error,
	             "A brush is broken or very far from the grid (a side with too many corners). Snap it to the grid (Ctrl+B) or rebuild it."},
	KnownMessage{"doesn't touch two areas", CompileMessage::Severity::Error,
	             "A func_areaportal does not close an opening between two sealed areas. It must fill the doorway completely and touch "
	             "world brushes (not func_detail) all around; the areas on both sides must be sealed from each other."},
	KnownMessage{"areaportal", CompileMessage::Severity::Warning,
	             "An areaportal problem: an areaportal must fill its opening completely, and the spaces on its two sides must be sealed "
	             "from each other by world brushes."},
	KnownMessage{"too many light styles", CompileMessage::Severity::Error,
	             "More than 4 switchable lights (lights with a name, or with a style) shine on one face. Give fewer lights a name, or cut "
	             "the face into smaller pieces."},
	KnownMessage{"sided brush face", CompileMessage::Severity::Error,
	             "A displacement sits on a face that does not have exactly 4 corners. Displacements need 4-sided faces: make it flat "
	             "again (Displacement tool) and cut the brush so the face has 4 corners."},
	KnownMessage{"loadportals", CompileMessage::Severity::Error,
	             "VVIS needs the .prt file VBSP writes only for a sealed map. Fix the leak first (Problems shows it), then compile again."},
	KnownMessage{"couldn't open", CompileMessage::Severity::Error,
	             "A file could not be opened. For VVIS/VRAD this usually means VBSP failed or the map leaked (no .prt file)."},
	// The engine limits one by one (VBSP: "ERROR: MAX_MAP_..."; View > Map Budget counts them while building).
	KnownMessage{"max_map_brushsides", CompileMessage::Severity::Error,
	             "Too many brush sides (65536; VBSP adds up to 6 bevel sides per brush). func_detail does not help here (detail brushes "
	             "count too): merge boxes, delete hidden brushes, use fewer segments on round shapes, and turn detailed brushwork into models."},
	KnownMessage{"max_map_brushes", CompileMessage::Severity::Error,
	             "Too many brushes (8192, func_detail and tool brushes included). Merge boxes that lie side by side, delete hidden or "
	             "unused brushes, and turn detailed brushwork into models."},
	KnownMessage{"max_map_planes", CompileMessage::Severity::Error,
	             "Too many different planes (65536). Odd angles and off-grid vertices each make new planes: snap brushes to the grid "
	             "(Ctrl+B), use fewer round shapes or fewer segments, and turn detailed brushwork into models."},
	KnownMessage{"max_map_entities", CompileMessage::Severity::Error,
	             "Too many entities. Remove unused ones; props that never move can be prop_static (compiled into the map); logic that "
	             "runs once can often be one logic_relay."},
	KnownMessage{"max_map_overlays", CompileMessage::Severity::Error, "Too many info_overlay entities (512). Use fewer overlays."},
	KnownMessage{"max_map_dispinfo", CompileMessage::Severity::Error,
	             "Too many displacements (2048). Use fewer, larger displacements (power 3 or 4 instead of many small ones)."},
	KnownMessage{"max_map_texinfo", CompileMessage::Severity::Error,
	             "Too many different texture alignments (texinfo). Faces with odd shifts, scales or rotations each count: align faces "
	             "of a wall alike (Face mode, Justify) and use fewer different materials."},
	KnownMessage{"max_map_portals", CompileMessage::Severity::Error,
	             "Too many vis portals. Make round shapes, small pieces and props-like brushwork func_detail so vis ignores them."},
	KnownMessage{"microbrush", CompileMessage::Severity::Warning,
	             "A brush is so tiny (under about 1 unit) that VBSP drops it. Select it (Problems lists invalid brushes) and delete it or "
	             "make it bigger."},
	KnownMessage{"t-junction", CompileMessage::Severity::Warning,
	             "Too many places where faces meet off their corners (t-junctions). Usually harmless; on very large maps make details "
	             "func_detail and keep brushes on the grid."},
	KnownMessage{"max_map_", CompileMessage::Severity::Error,
	             "The map is over an engine limit (brushes, planes, entities, ...). Turn details into func_detail, merge or remove brushes."},
	KnownMessage{"too many", CompileMessage::Severity::Error, "An engine limit was reached (see the message)."},
	KnownMessage{"error loading studio model", CompileMessage::Severity::Warning,
	             "A prop uses a model that is missing or broken. Check the model path in Object Properties (the model browser lists valid models)."},
	KnownMessage{"not a static prop", CompileMessage::Severity::Warning,
	             "This model cannot be a prop_static (it was not compiled with $staticprop). Use prop_dynamic or prop_physics."},
	KnownMessage{"material not found", CompileMessage::Severity::Warning,
	             "A brush uses a material the game does not have; it will show as a pink/black checkerboard. Problems lists it with a fix."},
	KnownMessage{"portal clipped away", CompileMessage::Severity::Warning,
	             "A brush is so thin or so far off the grid that a side vanished. Snap the brush to the grid or make it thicker."},
	KnownMessage{"brush with no sides", CompileMessage::Severity::Warning, "A broken brush was skipped. Problems can find and delete it."},
	KnownMessage{"plane with no normal", CompileMessage::Severity::Error, "A broken brush (three points on a line). Problems can find and delete it."},
	KnownMessage{"mixed face contents", CompileMessage::Severity::Warning,
	             "A brush has sides with different kinds of material (e.g. water and solid). Use one kind per brush."},
	KnownMessage{"no lights", CompileMessage::Severity::Warning, "The map has no light entities: everything will be dark (or fullbright without VRAD)."},
	KnownMessage{"no env_cubemap", CompileMessage::Severity::Warning, "Shiny materials need env_cubemap entities; place some and run buildcubemaps in the game."},
	KnownMessage{"entity has no origin", CompileMessage::Severity::Warning, "A point entity has no position; select it and move it once."},
	KnownMessage{"has no brushes", CompileMessage::Severity::Warning, "A brush entity without brushes was removed (Problems shows it)."},
	KnownMessage{"unknown entity", CompileMessage::Severity::Warning, "An entity class the game does not know (misspelled, or from another game's FGD)."},
	KnownMessage{"fatal", CompileMessage::Severity::Error, "The tool stopped. Look at the lines just above for the reason."},
};

bool containsWord(const std::string& lower, std::string_view word) {
	for (std::size_t at = lower.find(word); at != std::string::npos; at = lower.find(word, at + 1)) {
		const bool startOk = at == 0 || !std::isalpha(static_cast<unsigned char>(lower[at - 1]));
		const bool endOk = at + word.size() >= lower.size() || !std::isalpha(static_cast<unsigned char>(lower[at + word.size()]));
		if (startOk && endOk) {
			return true;
		}
	}
	return false;
}

} // namespace

std::optional<CompileMessage> classifyCompileLine(std::string_view line) {
	const auto lower = toLower(line);
	for (const auto& known : KNOWN) {
		if (lower.find(known.pattern) != std::string::npos) {
			return CompileMessage{known.severity, std::string(known.explanation), known.leak};
		}
	}
	// "0 errors" style summaries are not errors.
	if (lower.find("0 error") != std::string::npos || lower.find("no error") != std::string::npos) {
		return std::nullopt;
	}
	if (containsWord(lower, "error") || containsWord(lower, "errors")) {
		return CompileMessage{CompileMessage::Severity::Error, {}};
	}
	if (containsWord(lower, "warning") || containsWord(lower, "warnings")) {
		return CompileMessage{CompileMessage::Severity::Warning, {}};
	}
	return std::nullopt;
}

std::optional<int> compileProgress(std::string_view text) {
	// "0...1...2...3" - the last number followed by dots (or at the end after dots) counts; 10 is done.
	std::optional<int> last;
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
			continue;
		}
		std::size_t j = i;
		int value = 0;
		while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j])) && value < 1000) {
			value = value * 10 + (text[j] - '0');
			++j;
		}
		const bool dotsAfter = text.substr(j).starts_with("...");
		const bool dotsBefore = i >= 3 && text.substr(i - 3, 3) == "...";
		if ((dotsAfter || dotsBefore) && value <= 10) {
			last = value * 10;
		}
		i = j;
	}
	return last;
}

std::string compilePhase(std::string_view line) {
	const auto colon = line.find(':');
	if (colon == std::string_view::npos || !compileProgress(line.substr(colon + 1))) {
		return {};
	}
	return std::string(trim(line.substr(0, colon)));
}

namespace {

std::uint32_t readU32(std::span<const std::byte> data, std::size_t at) {
	return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8) | (static_cast<std::uint32_t>(data[at + 2]) << 16) |
	       (static_cast<std::uint32_t>(data[at + 3]) << 24);
}
std::uint16_t readU16(std::span<const std::byte> data, std::size_t at) {
	return static_cast<std::uint16_t>(static_cast<unsigned>(data[at]) | (static_cast<unsigned>(data[at + 1]) << 8));
}

/// "materials/maps/<map>/c-512_256_64.vtf" or ".hdr.vtf" (the names the game gives built cubemaps).
bool isBuiltCubemapName(std::string_view name, std::string_view prefix) {
	const std::string lower = toLower(name);
	if (!lower.starts_with(prefix)) return false;
	std::string_view rest = std::string_view(lower).substr(prefix.size());
	if (rest.ends_with(".hdr.vtf")) rest.remove_suffix(8);
	else if (rest.ends_with(".vtf")) rest.remove_suffix(4);
	else return false;
	int numbers = 0;
	bool digit = false;
	for (std::size_t i = 0; i < rest.size(); ++i) {
		const char c = rest[i];
		if (c >= '0' && c <= '9') {
			digit = true;
		} else if (c == '-' && !digit) {
		} else if (c == '_' && digit) {
			++numbers;
			digit = false;
		} else {
			return false;
		}
	}
	return digit && numbers == 2;
}

} // namespace

std::optional<BspCubemaps> readBspCubemaps(std::span<const std::byte> bsp, std::string_view mapName) {
	constexpr std::size_t HEADER = 8 + 64 * 16 + 4;
	if (bsp.size() < HEADER || std::memcmp(bsp.data(), "VBSP", 4) != 0) return std::nullopt;
	const auto lump = [&](int index) -> std::optional<std::span<const std::byte>> {
		const std::size_t at = 8 + static_cast<std::size_t>(index) * 16;
		const std::uint32_t offset = readU32(bsp, at), length = readU32(bsp, at + 4);
		if (offset > bsp.size() || length > bsp.size() - offset) return std::nullopt;
		return bsp.subspan(offset, length);
	};
	BspCubemaps out;
	const auto samples = lump(42);   // LUMP_CUBEMAPS: dcubemapsample_t, 16 bytes each
	if (!samples) return std::nullopt;
	out.samples = static_cast<int>(samples->size() / 16);
	const auto pak = lump(40);       // LUMP_PAKFILE: an uncompressed zip
	if (!pak) return std::nullopt;
	if (pak->size() < 22) return out;
	// End of central directory: the last "PK\5\6" (a comment may follow it).
	std::size_t eocd = std::string::npos;
	const std::size_t lowest = pak->size() > 22 + 65535 ? pak->size() - 22 - 65535 : 0;
	for (std::size_t at = pak->size() - 22 + 1; at-- > lowest;) {
		if (readU32(*pak, at) == 0x06054b50u) {
			eocd = at;
			break;
		}
	}
	if (eocd == std::string::npos) return out;
	const std::size_t entries = readU16(*pak, eocd + 10);
	std::size_t at = readU32(*pak, eocd + 16);
	const std::string prefix = "materials/maps/" + toLower(mapName) + "/c";
	for (std::size_t i = 0; i < entries; ++i) {
		if (at > pak->size() || pak->size() - at < 46 || readU32(*pak, at) != 0x02014b50u) break;
		const std::size_t nameLength = readU16(*pak, at + 28), extra = readU16(*pak, at + 30), comment = readU16(*pak, at + 32);
		if (pak->size() - at - 46 < nameLength) break;
		const std::string_view name(reinterpret_cast<const char*>(pak->data() + at + 46), nameLength);
		if (isBuiltCubemapName(name, prefix)) ++out.built;
		at += 46 + nameLength + extra + comment;
	}
	return out;
}

CubemapVerdict judgeCubemapBuild(int exitCode, bool crashed, const std::optional<BspCubemaps>& before, const std::optional<BspCubemaps>& after,
                                 bool mapRewritten) {
	CubemapVerdict v;
	if (!after) {
		v.message = crashed ? "The game crashed while building the cubemaps, and the map in maps/ cannot be read."
		                    : "The map in maps/ cannot be read after building the cubemaps - compile again.";
		return v;
	}
	if (after->samples == 0) {
		v.ok = true;
		v.message = "The map has no env_cubemap, so there was nothing to build (shiny materials reflect the sky).";
		return v;
	}
	const int built = after->built;
	const bool newer = mapRewritten || !before || built > before->built;
	if (built > 0 && newer) {
		v.ok = true;
		v.message = std::to_string(built) + " cubemap" + (built == 1 ? "" : "s") + " built into the map" +
		            (exitCode != 0 ? " (the game always ends with exit code " + std::to_string(exitCode) + " after building - that is fine)." : ".");
		return v;
	}
	v.message = crashed ? "The game crashed while building the cubemaps; the map has none built."
	                    : "The game ended (exit code " + std::to_string(exitCode) + ") without building cubemaps into maps/ - start it once by hand and look at its console.";
	return v;
}

} // namespace sme
