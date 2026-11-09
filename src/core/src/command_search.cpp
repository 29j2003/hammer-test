// command_search.cpp - Matching for the command search (see command_search.h).

#include <sme/command_search.h>

#include <algorithm>
#include <cctype>
#include <numeric>

namespace sme {

namespace {

std::string lower(std::string_view text) {
	std::string out(text);
	for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

std::vector<std::string> words(const std::string& text) {
	std::vector<std::string> out;
	std::string word;
	for (const char c : text) {
		if (std::isspace(static_cast<unsigned char>(c))) {
			if (!word.empty()) out.push_back(std::move(word));
			word.clear();
		} else {
			word += c;
		}
	}
	if (!word.empty()) out.push_back(std::move(word));
	return out;
}

bool wordStart(const std::string& text, std::size_t at) {
	if (at == 0) return true;
	const unsigned char before = static_cast<unsigned char>(text[at - 1]);
	return !std::isalnum(before);
}

/// Score of one word in `text`: -1 missing, 2 at a word start, 1 inside a word.
int wordScore(const std::string& text, const std::string& word) {
	int best = -1;
	for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
		best = std::max(best, wordStart(text, at) ? 2 : 1);
		if (best == 2) break;
	}
	return best;
}

} // namespace

int commandMatchScore(std::string_view query, std::string_view name, std::string_view where) {
	const std::string q = lower(query);
	const auto queryWords = words(q);
	if (queryWords.empty()) return 0;
	const std::string n = lower(name), w = lower(where);
	int score = 0;
	for (const auto& word : queryWords) {
		const int inName = wordScore(n, word);
		if (inName >= 0) {
			score += inName == 2 ? 40 : 15;
			continue;
		}
		const int inWhere = wordScore(w, word);
		if (inWhere < 0) return -1;
		score += inWhere == 2 ? 10 : 4;
	}
	std::string joined;
	for (const auto& word : queryWords) joined += (joined.empty() ? "" : " ") + word;
	if (n == joined) {
		score += 400;
	} else if (n.rfind(joined, 0) == 0) {
		score += 200;
	} else if (n.find(joined) != std::string::npos) {
		score += 60;
	}
	score += std::max(0, 40 - int(n.size()) / 2);   // shorter names first
	return score;
}

std::vector<std::size_t> rankCommands(std::string_view query, const std::vector<std::string>& names, const std::vector<std::string>& wheres,
                                      std::size_t limit) {
	std::vector<std::pair<int, std::size_t>> scored;
	for (std::size_t i = 0; i < names.size(); ++i) {
		const int score = commandMatchScore(query, names[i], i < wheres.size() ? std::string_view(wheres[i]) : std::string_view());
		if (score >= 0) scored.emplace_back(score, i);
	}
	std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	std::vector<std::size_t> out;
	for (std::size_t i = 0; i < scored.size() && out.size() < limit; ++i) out.push_back(scored[i].second);
	return out;
}

} // namespace sme
