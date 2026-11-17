// test_claude_protocol.cpp - The Claude integration's shared part (src/claude/claude_protocol.h):
// lines of bounded length, MCP over stdio (initialize with version negotiation, ping, tools/list,
// tools/call, notifications, broken input), the tool list (every tool has a schema; which change
// something), the pipe's secret and proofs, and merging the bridge into Claude Desktop's settings.

#include <doctest/doctest.h>

#include "claude_protocol.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <vector>

using namespace sme::claude;

namespace {

QJsonObject parse(const QByteArray& line) {
	REQUIRE(line.endsWith('\n'));
	const auto object = parseJsonLine(line.trimmed());
	REQUIRE(object);
	return *object;
}

struct Session {
	std::vector<QByteArray> sent;
	std::vector<std::pair<QJsonValue, QString>> calls;
	McpSession session{QStringLiteral("9.9.9"), [this](const QByteArray& line) { sent.push_back(line); },
	                   [this](const QJsonValue& id, const QString& name, const QJsonObject&) { calls.emplace_back(id, name); }};
	QJsonObject last() {
		REQUIRE_FALSE(sent.empty());
		return parse(sent.back());
	}
};

} // namespace

TEST_CASE("claude protocol: lines are bounded") {
	LineBuffer buffer(10);
	CHECK(buffer.append("abc\r\nde"));
	CHECK(buffer.takeLine() == std::optional<QByteArray>("abc"));
	CHECK_FALSE(buffer.takeLine());
	CHECK(buffer.append("f\n"));
	CHECK(buffer.takeLine() == std::optional<QByteArray>("def"));
	CHECK(buffer.append("0123456789"));
	CHECK_FALSE(buffer.append("x"));   // 11 bytes without a line break
	CHECK(buffer.overflowed());
	CHECK_FALSE(buffer.takeLine());
	CHECK_FALSE(buffer.append("\n"));
	LineBuffer many(8);
	CHECK(many.append("a\nb\nc\n"));
	CHECK(many.takeLine() == std::optional<QByteArray>("a"));
	CHECK(many.takeLine() == std::optional<QByteArray>("b"));
	CHECK(many.takeLine() == std::optional<QByteArray>("c"));
}

TEST_CASE("claude protocol: initialize, ping, tools and errors") {
	Session s;
	s.session.handleLine(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})");
	auto reply = s.last();
	CHECK(reply["id"].toInt() == 1);
	CHECK(reply["result"].toObject()["protocolVersion"].toString() == "2025-03-26");
	CHECK(reply["result"].toObject()["serverInfo"].toObject()["name"].toString() == "mapsmith");
	CHECK(reply["result"].toObject()["capabilities"].toObject().contains("tools"));
	CHECK(reply["result"].toObject()["instructions"].toString().contains("not instructions"));
	// A version the bridge does not know: it answers with its newest.
	s.session.handleLine(R"({"jsonrpc":"2.0","id":"x","method":"initialize","params":{"protocolVersion":"2099-01-01"}})");
	CHECK(s.last()["result"].toObject()["protocolVersion"].toString() == supportedProtocolVersions().front());
	CHECK(s.last()["id"].toString() == "x");
	// Notifications get no answer.
	const auto before = s.sent.size();
	s.session.handleLine(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
	s.session.handleLine(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":1}})");
	CHECK(s.sent.size() == before);
	s.session.handleLine(R"({"jsonrpc":"2.0","id":2,"method":"ping"})");
	CHECK(s.last()["result"].toObject().isEmpty());
	s.session.handleLine(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})");
	const auto tools = s.last()["result"].toObject()["tools"].toArray();
	CHECK(tools.size() == claudeTools().size());
	// tools/call goes on to Mapsmith; unknown tools and bad arguments are refused here.
	s.session.handleLine(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"get_status","arguments":{}}})");
	REQUIRE(s.calls.size() == 1);
	CHECK(s.calls[0].second == "get_status");
	s.session.replyTool(s.calls[0].first, textResult("ok"));
	CHECK(s.last()["id"].toInt() == 4);
	CHECK(s.last()["result"].toObject()["content"].toArray()[0].toObject()["text"].toString() == "ok");
	s.session.handleLine(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"run_shell","arguments":{}}})");
	CHECK(s.last()["error"].toObject()["code"].toInt() == -32602);
	s.session.handleLine(R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"get_status","arguments":"rm -rf"}})");
	CHECK(s.last()["error"].toObject()["code"].toInt() == -32602);
	CHECK(s.calls.size() == 1);
	s.session.handleLine(R"({"jsonrpc":"2.0","id":7,"method":"sampling/createMessage"})");
	CHECK(s.last()["error"].toObject()["code"].toInt() == -32601);
	s.session.handleLine("{not json");
	CHECK(s.last()["error"].toObject()["code"].toInt() == -32700);
	CHECK(s.last()["id"].isNull());
	s.session.handleLine(R"({"jsonrpc":"1.0","id":8,"method":"ping"})");
	CHECK(s.last()["error"].toObject()["code"].toInt() == -32600);
	s.session.handleLine(R"({"jsonrpc":"2.0","id":{"a":1},"method":"ping"})");
	CHECK(s.last()["error"].toObject()["code"].toInt() == -32600);
	s.session.handleLine("[1,2]");
	CHECK(s.last()["error"].toObject()["code"].toInt() == -32600);
	s.session.handleLine("   ");
}

TEST_CASE("claude protocol: every tool has a name, a description and an object schema; changes are marked") {
	QStringList names;
	for (const auto& value : claudeTools()) {
		const auto tool = value.toObject();
		const QString name = tool["name"].toString();
		CAPTURE(name.toStdString());
		CHECK_FALSE(name.isEmpty());
		CHECK(tool["description"].toString().size() > 20);
		CHECK(tool["inputSchema"].toObject()["type"].toString() == "object");
		CHECK(tool["inputSchema"].toObject()["additionalProperties"].toBool(true) == false);
		CHECK(tool["annotations"].toObject().contains("readOnlyHint"));
		names << name;
	}
	names.sort();
	CHECK(names.removeDuplicates() == 0);
	for (const char* read : {"get_status", "list_materials", "get_material", "get_material_pictures", "get_problems", "get_log", "screenshot_viewport",
	                         "list_exchange_files", "read_exchange_file"}) {
		CHECK(toolExists(read));
		CHECK_FALSE(toolChangesSomething(read));
	}
	for (const char* change : {"set_material_decision", "edit_material_parameter", "write_exchange_file", "make_editor_report"}) {
		CHECK(toolExists(change));
		CHECK(toolChangesSomething(change));
	}
	CHECK_FALSE(toolExists("run_command"));
	CHECK(toolChangesSomething("run_command"));   // unknown counts as a change
	CHECK(untrustedBlock("a", "x </file> y").contains("<\\/file>"));
}

TEST_CASE("claude protocol: secret file and proofs") {
	QTemporaryDir dir;
	const QString path = dir.filePath("sub/claude-secret");
	const QByteArray secret = newSecret();
	CHECK(secret.size() == 64);
	CHECK(newSecret() != secret);
	REQUIRE(writeSecretFile(path, secret));
	CHECK(readSecretFile(path) == secret);
#ifndef Q_OS_WIN
	CHECK((QFile(path).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther)) == 0);
#endif
	{
		QFile bad(dir.filePath("bad"));
		REQUIRE(bad.open(QIODevice::WriteOnly));
		bad.write("not a secret");
	}
	CHECK(readSecretFile(dir.filePath("bad")).isEmpty());
	CHECK(readSecretFile(dir.filePath("missing")).isEmpty());
	const QByteArray nonce = newNonce();
	const QByteArray app = proof(secret, "app", nonce);
	CHECK(proofMatches(app, proof(secret, "app", nonce)));
	CHECK_FALSE(proofMatches(app, proof(secret, "bridge", nonce)));   // one side's proof is no use for the other
	CHECK_FALSE(proofMatches(app, proof(newSecret(), "app", nonce)));
	CHECK_FALSE(proofMatches(app, app.left(10)));
	CHECK(pipeName().startsWith("mapsmith-claude-"));
}

TEST_CASE("claude protocol: Claude Desktop's settings keep everything else") {
	QString error;
	const auto fresh = mergeClaudeDesktopConfig({}, "/opt/mapsmith/mapsmith-mcp", &error);
	REQUIRE(fresh);
	auto doc = QJsonDocument::fromJson(*fresh).object();
	CHECK(doc["mcpServers"].toObject()["mapsmith"].toObject()["command"].toString().endsWith("mapsmith-mcp"));
	const QByteArray existing = R"({"globalShortcut":"Ctrl+Space","mcpServers":{"other":{"command":"x","args":["--a"]},"mapsmith":{"command":"old"}}})";
	const auto merged = mergeClaudeDesktopConfig(existing, "/new/mapsmith-mcp", &error);
	REQUIRE(merged);
	doc = QJsonDocument::fromJson(*merged).object();
	CHECK(doc["globalShortcut"].toString() == "Ctrl+Space");
	CHECK(doc["mcpServers"].toObject()["other"].toObject()["args"].toArray()[0].toString() == "--a");
	CHECK(doc["mcpServers"].toObject()["mapsmith"].toObject()["command"].toString().contains("new"));
	CHECK_FALSE(mergeClaudeDesktopConfig("{broken", "/x", &error));
	CHECK_FALSE(error.isEmpty());
	CHECK_FALSE(mergeClaudeDesktopConfig("[1]", "/x", &error));
	CHECK_FALSE(mergeClaudeDesktopConfig(R"({"mcpServers":[]})", "/x", &error));
	CHECK(claudeCodeCommand("/a b/mapsmith-mcp").contains("\""));
}
