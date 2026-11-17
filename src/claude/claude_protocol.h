// claude_protocol.h - What the bridge `mapsmith-mcp` and Mapsmith share for the Claude integration
// (library `smeclaude`, Qt Core only):
//
//  * MCP (Model Context Protocol) over stdio between Claude (Claude Desktop, Claude Code) and the
//    bridge: JSON-RPC 2.0, one message per line (McpSession: initialize with version
//    negotiation, ping, tools/list, tools/call; notifications get no answer).
//  * The tools Claude sees (claudeTools()): names, descriptions, JSON schemas, which change
//    something. The bridge lists them even while Mapsmith is closed.
//  * The local pipe between the bridge and the running Mapsmith (QLocalSocket / QLocalServer,
//    one JSON object per line): a per-user name, a random secret per Mapsmith start in a file
//    only this user can read, and a handshake in which each side proves it knows the secret
//    without sending it (HMAC-SHA256 over the other side's random nonce). A program that
//    squats the pipe name learns nothing and is refused by the bridge; a program without the
//    secret is refused by Mapsmith. No network: no TCP port, nothing a browser could reach.
//  * Lines of bounded length on both connections (LineBuffer), so a peer cannot make either side
//    hold unbounded memory.
//  * Connecting Claude Desktop: merging the bridge into claude_desktop_config.json.

#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace sme::claude {

/// Longest line Claude may send to the bridge (a tool call with its arguments).
inline constexpr qsizetype MCP_LINE_MAX = 4 * 1024 * 1024;
/// Longest line on the pipe (a result with up to three pictures).
inline constexpr qsizetype PIPE_LINE_MAX = 24 * 1024 * 1024;

/// Collects bytes and hands out complete lines ('\n', a '\r' before it is dropped). More than
/// `max` bytes without a line break: overflowed() - the connection should be closed.
class LineBuffer {
public:
	explicit LineBuffer(qsizetype max) : m_max(max) {}
	/// Adds bytes; false once the buffer overflowed (it stays that way and drops everything).
	bool append(const QByteArray& bytes);
	/// The next complete line, if there is one.
	[[nodiscard]] std::optional<QByteArray> takeLine();
	[[nodiscard]] bool overflowed() const { return m_overflow; }

private:
	qsizetype m_max;
	QByteArray m_data;
	qsizetype m_scanned = 0;
	bool m_overflow = false;
};

/// The MCP versions the bridge speaks, newest first.
[[nodiscard]] QStringList supportedProtocolVersions();
/// The version to answer an initialize with: the requested one when the bridge speaks it, else its newest.
[[nodiscard]] QString negotiateProtocolVersion(const QString& requested);

/// Every tool Claude can call (MCP tool objects: name, title, description, inputSchema, annotations).
[[nodiscard]] QJsonArray claudeTools();
/// Whether a tool changes something (needs "Read and change materials").
[[nodiscard]] bool toolChangesSomething(const QString& name);
/// Whether a tool of this name exists.
[[nodiscard]] bool toolExists(const QString& name);
/// What the server tells Claude at initialize (how to use the tools, what to trust).
[[nodiscard]] QString serverInstructions();

/// A tool result with one text block.
[[nodiscard]] QJsonObject textResult(const QString& text, bool isError = false);
/// An image block (JPEG bytes).
[[nodiscard]] QJsonObject imageContent(const QByteArray& jpeg);
/// A text block.
[[nodiscard]] QJsonObject textContent(const QString& text);
/// Text from a file wrapped so Claude reads it as data ("<file ...>\n...\n</file>" with a note).
[[nodiscard]] QString untrustedBlock(const QString& label, const QString& text);

/// One MCP conversation with Claude on the bridge's side (see the file comment). Lines in, lines out.
class McpSession {
public:
	using Send = std::function<void(const QByteArray& line)>;
	/// A tools/call: answer it later with replyTool(id, result) (from any order).
	using CallTool = std::function<void(const QJsonValue& id, const QString& name, const QJsonObject& arguments)>;
	McpSession(QString serverVersion, Send send, CallTool callTool);
	/// One line from Claude.
	void handleLine(const QByteArray& line);
	/// The result of a tool call.
	void replyTool(const QJsonValue& id, const QJsonObject& result);
	/// Whether initialize has been answered.
	[[nodiscard]] bool initialized() const { return m_initialized; }
	[[nodiscard]] QString protocolVersion() const { return m_version; }

private:
	void handleMessage(const QJsonObject& message);
	void sendResult(const QJsonValue& id, const QJsonObject& result);
	void sendError(const QJsonValue& id, int code, const QString& message);
	QString m_serverVersion;
	Send m_send;
	CallTool m_callTool;
	bool m_initialized = false;
	QString m_version;
};

// --- The pipe between the bridge and Mapsmith ---------------------------------------------------

/// The pipe's name for this user ("mapsmith-claude-" + a hash of the user name; SME_CLAUDE_PIPE overrides it for tests).
[[nodiscard]] QString pipeName();
/// Folder of the secret file (per user; SME_CLAUDE_DATA_DIR overrides it for tests).
[[nodiscard]] QString dataFolder();
/// The secret file Mapsmith writes at start and removes when Claude is switched off.
[[nodiscard]] QString secretFilePath();
/// A new random secret (32 bytes, hex).
[[nodiscard]] QByteArray newSecret();
/// Writes the secret for this user only (owner read/write). false on failure.
bool writeSecretFile(const QString& path, const QByteArray& secret);
/// Reads the secret ("" when missing or not a secret).
[[nodiscard]] QByteArray readSecretFile(const QString& path);
/// A random nonce (16 bytes, hex).
[[nodiscard]] QByteArray newNonce();
/// HMAC-SHA256(secret, role + ":" + nonce) as hex: each side proves it knows the secret.
[[nodiscard]] QByteArray proof(const QByteArray& secret, const QByteArray& role, const QByteArray& nonce);
/// Compares two proofs in constant time.
[[nodiscard]] bool proofMatches(const QByteArray& a, const QByteArray& b);

/// One JSON object as a line ("...\n").
[[nodiscard]] QByteArray jsonLine(const QJsonObject& object);
/// A line as a JSON object (nullopt when it is not one).
[[nodiscard]] std::optional<QJsonObject> parseJsonLine(const QByteArray& line);

// --- Claude Desktop ------------------------------------------------------------------------------

/// claude_desktop_config.json with the bridge as server "mapsmith" (other servers and settings kept).
/// nullopt and why when the existing file is not a JSON object (it is then left alone).
[[nodiscard]] std::optional<QByteArray> mergeClaudeDesktopConfig(const QByteArray& existing, const QString& bridgeCommand, QString* error = nullptr);
/// Where Claude Desktop keeps its settings on this system: the existing files first (also the
/// Microsoft Store version's folder on Windows), else the usual place for a new one.
[[nodiscard]] QStringList claudeDesktopConfigFiles();
/// The command line that adds the bridge to Claude Code.
[[nodiscard]] QString claudeCodeCommand(const QString& bridgeCommand);

} // namespace sme::claude
