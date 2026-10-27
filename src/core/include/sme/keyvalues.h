// keyvalues.h - Ordered, writable KeyValues tree used for VMF files.
//
// Keeps the order and spelling of every key so maps are written back exactly as Hammer
// would, and blocks the editor does not understand survive a load/save unchanged.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sme {

struct KvNode {
	std::string key;
	std::string value;                 ///< Empty for blocks
	std::vector<KvNode> children;
	bool block = false;                ///< `key { ... }` instead of `"key" "value"`

	[[nodiscard]] static KvNode makeBlock(std::string key) { return {std::move(key), {}, {}, true}; }
	[[nodiscard]] static KvNode makeValue(std::string key, std::string value) { return {std::move(key), std::move(value), {}, false}; }

	/// First child with the key (case-insensitive) or nullptr.
	[[nodiscard]] const KvNode* find(std::string_view childKey) const;
	[[nodiscard]] KvNode* find(std::string_view childKey);
	/// Value of the first child with the key, or `fallback`.
	[[nodiscard]] std::string get(std::string_view childKey, std::string_view fallback = {}) const;
	/// Sets (or appends) a key/value child.
	void set(std::string_view childKey, std::string value);
};

/// Parses text into a root node whose children are the top-level entries.
/// Throws std::runtime_error with a line number on broken syntax.
[[nodiscard]] KvNode parseKeyValues(std::string_view text);

/// Writes children of `root` in Hammer's VMF style (tabs, quoted keys and values).
[[nodiscard]] std::string writeKeyValues(const KvNode& root);

/// Appends one node (and its children) at the given indentation.
void writeKeyValueNode(std::string& out, const KvNode& node, int depth);

} // namespace sme
