// keyvalues.cpp - KeyValues parser and VMF-style writer (see keyvalues.h).

#include <sme/keyvalues.h>

#include <sme/text.h>

#include <stdexcept>

namespace sme {

namespace {

// Splits KeyValues text into tokens: quoted or bare strings, '{' and '}'. Skips whitespace,
// // comments and a UTF-8 byte order mark. Quoted strings may span lines and have no escapes
// (a backslash is kept as it is, as in VMF paths). Errors throw with the line number.
class Tokenizer {
public:
	explicit Tokenizer(std::string_view text) : m_text(text) {
		if (m_text.starts_with("\xEF\xBB\xBF")) {
			m_pos = 3;
		}
	}

	enum class Kind { End, String, Open, Close };

	struct Token {
		Kind kind = Kind::End;
		std::string_view text;
		bool quoted = false;
	};

	Token next() {
		skipWhitespaceAndComments();
		if (m_pos >= m_text.size()) {
			return {};
		}
		const char c = m_text[m_pos];
		if (c == '{') {
			++m_pos;
			return {Kind::Open, {}, false};
		}
		if (c == '}') {
			++m_pos;
			return {Kind::Close, {}, false};
		}
		if (c == '"') {
			const auto start = ++m_pos;
			while (m_pos < m_text.size() && m_text[m_pos] != '"') {
				if (m_text[m_pos] == '\n') {
					++m_line;
				}
				++m_pos;
			}
			if (m_pos >= m_text.size()) {
				fail("unterminated string");
			}
			const auto out = m_text.substr(start, m_pos - start);
			++m_pos;
			return {Kind::String, out, true};
		}
		const auto start = m_pos;
		while (m_pos < m_text.size()) {
			const char d = m_text[m_pos];
			if (d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == '{' || d == '}' || d == '"') {
				break;
			}
			++m_pos;
		}
		return {Kind::String, m_text.substr(start, m_pos - start), false};
	}

	Token peek() {
		const auto pos = m_pos;
		const auto line = m_line;
		const auto token = next();
		m_pos = pos;
		m_line = line;
		return token;
	}

	[[noreturn]] void fail(const std::string& message) const {
		throw std::runtime_error("line " + std::to_string(m_line) + ": " + message);
	}

private:
	void skipWhitespaceAndComments() {
		while (m_pos < m_text.size()) {
			const char c = m_text[m_pos];
			if (c == '\n') {
				++m_line;
				++m_pos;
			} else if (c == ' ' || c == '\t' || c == '\r') {
				++m_pos;
			} else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
				while (m_pos < m_text.size() && m_text[m_pos] != '\n') {
					++m_pos;
				}
			} else {
				break;
			}
		}
	}

	std::string_view m_text;
	std::size_t m_pos = 0;
	int m_line = 1;
};

// Reads `key value` pairs and `key { ... }` blocks until the matching '}' (or the end of the
// text at the top level). The depth limit keeps broken or hostile files from overflowing the stack.
void parseChildren(Tokenizer& tokens, KvNode& parent, bool topLevel, int depth) {
	if (depth > 256) {
		tokens.fail("blocks nested too deep");
	}
	while (true) {
		auto token = tokens.next();
		if (token.kind == Tokenizer::Kind::End) {
			if (!topLevel) {
				tokens.fail("missing '}'");
			}
			return;
		}
		if (token.kind == Tokenizer::Kind::Close) {
			if (topLevel) {
				tokens.fail("unexpected '}'");
			}
			return;
		}
		if (token.kind == Tokenizer::Kind::Open) {
			tokens.fail("unexpected '{'");
		}
		KvNode node;
		node.key = std::string(token.text);
		auto after = tokens.peek();
		if (after.kind == Tokenizer::Kind::Open) {
			tokens.next();
			node.block = true;
			parseChildren(tokens, node, false, depth + 1);
		} else if (after.kind == Tokenizer::Kind::String) {
			node.value = std::string(tokens.next().text);
			// Optional conditional like [$WIN32] after a value (VMT files; VMF never uses it).
			if (auto cond = tokens.peek(); cond.kind == Tokenizer::Kind::String && !cond.quoted && cond.text.starts_with('[')) {
				node.condition = std::string(tokens.next().text);
			}
		} else {
			tokens.fail("key '" + node.key + "' has no value");
		}
		parent.children.push_back(std::move(node));
	}
}

void appendQuoted(std::string& out, std::string_view text) {
	out += '"';
	out += text;
	out += '"';
}

} // namespace

const KvNode* KvNode::find(std::string_view childKey) const {
	for (const auto& child : children) {
		if (iequals(child.key, childKey)) {
			return &child;
		}
	}
	return nullptr;
}

KvNode* KvNode::find(std::string_view childKey) {
	for (auto& child : children) {
		if (iequals(child.key, childKey)) {
			return &child;
		}
	}
	return nullptr;
}

std::string KvNode::get(std::string_view childKey, std::string_view fallback) const {
	const auto* child = find(childKey);
	return child && !child->block ? child->value : std::string(fallback);
}

void KvNode::set(std::string_view childKey, std::string newValue) {
	if (auto* child = find(childKey); child && !child->block) {
		child->value = std::move(newValue);
		return;
	}
	children.push_back(makeValue(std::string(childKey), std::move(newValue)));
}

KvNode parseKeyValues(std::string_view text) {
	Tokenizer tokens(text);
	KvNode root = KvNode::makeBlock({});
	parseChildren(tokens, root, true, 0);
	return root;
}

void writeKeyValueNode(std::string& out, const KvNode& node, int depth) {
	const std::string indent(static_cast<std::size_t>(depth), '\t');
	if (node.block) {
		out += indent;
		out += node.key;
		out += '\n';
		out += indent;
		out += "{\n";
		for (const auto& child : node.children) {
			writeKeyValueNode(out, child, depth + 1);
		}
		out += indent;
		out += "}\n";
	} else {
		out += indent;
		appendQuoted(out, node.key);
		out += ' ';
		appendQuoted(out, node.value);
		if (!node.condition.empty()) {
			out += ' ';
			out += node.condition;
		}
		out += '\n';
	}
}

std::string writeKeyValues(const KvNode& root) {
	std::string out;
	for (const auto& child : root.children) {
		writeKeyValueNode(out, child, 0);
	}
	return out;
}

} // namespace sme
