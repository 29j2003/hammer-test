// compile_log.cpp - Compile output classification (see compile_log.h).

#include <sme/compile_log.h>

#include <sme/text.h>

#include <array>
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
	KnownMessage{"couldn't open", CompileMessage::Severity::Error,
	             "A file could not be opened. For VVIS/VRAD this usually means VBSP failed or the map leaked (no .prt file)."},
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

} // namespace sme
