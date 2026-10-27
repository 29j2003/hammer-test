// entity_search.cpp - Finding entities (see entity_search.h).

#include <sme/entity_search.h>

#include <sme/text.h>

#include <cctype>

namespace sme {

namespace {

char lower(char c) {
	return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool hasWildcard(std::string_view pattern) {
	return pattern.find_first_of("*?") != std::string_view::npos;
}

std::vector<std::string_view> splitTerms(std::string_view query) {
	std::vector<std::string_view> terms;
	std::size_t i = 0;
	while (i < query.size()) {
		while (i < query.size() && query[i] == ' ') ++i;
		const std::size_t start = i;
		while (i < query.size() && query[i] != ' ') ++i;
		if (i > start) terms.push_back(query.substr(start, i - start));
	}
	return terms;
}

bool anyKey(const Entity& entity, auto&& predicate) {
	for (const auto& [key, value] : entity.keyValues) {
		if (predicate(key, value)) return true;
	}
	return false;
}

bool matchesField(const Entity& entity, EntityField field, std::string_view pattern) {
	switch (field) {
		case EntityField::Class: return looseMatch(pattern, entity.classname());
		case EntityField::Name: return looseMatch(pattern, entity.get("targetname"));
		case EntityField::Model: return looseMatch(pattern, entity.get("model"));
		case EntityField::Sound:
			return anyKey(entity, [&](const std::string& key, const std::string& value) { return isSoundKey(key, value) && looseMatch(pattern, value); });
		case EntityField::OutputTarget:
			for (const auto& c : entity.connections) {
				if (looseMatch(pattern, c.target)) return true;
			}
			return false;
		case EntityField::KeyValue: {
			const auto eq = pattern.find('=');
			if (eq == std::string_view::npos) {
				// Only a key: the entity has it.
				return anyKey(entity, [&](const std::string& key, const std::string&) { return wildcardMatch(pattern, key); });
			}
			const auto key = pattern.substr(0, eq), value = pattern.substr(eq + 1);
			return anyKey(entity, [&](const std::string& k, const std::string& v) { return wildcardMatch(key, k) && wildcardMatch(value, v); });
		}
		case EntityField::Anything: {
			// One term of the query syntax.
			if (const auto colon = pattern.find(':'); colon != std::string_view::npos) {
				const auto prefix = pattern.substr(0, colon), rest = pattern.substr(colon + 1);
				if (iequals(prefix, "class")) return matchesField(entity, EntityField::Class, rest);
				if (iequals(prefix, "name")) return matchesField(entity, EntityField::Name, rest);
				if (iequals(prefix, "target")) return matchesField(entity, EntityField::OutputTarget, rest);
				if (iequals(prefix, "model")) return matchesField(entity, EntityField::Model, rest);
				if (iequals(prefix, "sound")) return matchesField(entity, EntityField::Sound, rest);
			}
			if (pattern.find('=') != std::string_view::npos) return matchesField(entity, EntityField::KeyValue, pattern);
			return looseMatch(pattern, entity.classname()) ||
			       anyKey(entity, [&](const std::string&, const std::string& value) { return looseMatch(pattern, value); });
		}
	}
	return false;
}

} // namespace

bool wildcardMatch(std::string_view pattern, std::string_view text) {
	// Iterative matcher with backtracking to the last '*'.
	std::size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
	while (t < text.size()) {
		if (p < pattern.size() && (pattern[p] == '?' || lower(pattern[p]) == lower(text[t]))) {
			++p;
			++t;
		} else if (p < pattern.size() && pattern[p] == '*') {
			star = p++;
			mark = t;
		} else if (star != std::string_view::npos) {
			p = star + 1;
			t = ++mark;
		} else {
			return false;
		}
	}
	while (p < pattern.size() && pattern[p] == '*') ++p;
	return p == pattern.size();
}

bool looseMatch(std::string_view pattern, std::string_view text) {
	if (pattern.empty()) return true;
	if (hasWildcard(pattern)) return wildcardMatch(pattern, text);
	return toLower(text).find(toLower(pattern)) != std::string::npos;
}

bool isSoundKey(std::string_view key, std::string_view value) {
	const std::string k = toLower(key);
	if (k == "message" || k.find("sound") != std::string::npos || k.find("noise") != std::string::npos) return true;
	const std::string v = toLower(value);
	return v.ends_with(".wav") || v.ends_with(".mp3") || v.ends_with(".ogg");
}

std::vector<int> findEntities(const MapDocument& doc, EntityField field, std::string_view query) {
	std::vector<int> out;
	const auto terms = field == EntityField::Anything ? splitTerms(query) : std::vector<std::string_view>{query};
	for (const auto& entity : doc.entities) {
		bool all = true;
		for (const auto& term : terms) {
			if (!matchesField(entity, field, term)) {
				all = false;
				break;
			}
		}
		if (all) out.push_back(entity.id);
	}
	return out;
}

} // namespace sme
