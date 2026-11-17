// claude_service.cpp - The local pipe for Claude, the handshake, the access level, the rate limit,
// the activity and the undo of Claude's changes (see claude_service.h; the tools are in
// claude_tools.cpp).

#include "claude_service.h"

#include "claude_protocol.h"
#include "editor.h"
#include "material_decision_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <cctype>
#include <deque>
#include <exception>

namespace sme::app {

using namespace sme::claude;

namespace {
constexpr int MAX_PEERS = 4;
constexpr int MAX_ACTIVITY = 500;
constexpr int MAX_UNDO = 100;
constexpr int HANDSHAKE_MS = 10000;
} // namespace

/// One connection from a bridge.
struct ClaudeService::Peer {
	enum class Stage { Hello, Auth, Ready };
	LineBuffer buffer{PIPE_LINE_MAX};
	Stage stage = Stage::Hello;
	QByteArray appNonce;
	std::deque<QJsonObject> calls;   ///< Calls waiting while another tool runs
};

ClaudeService::ClaudeService(Editor* editor, QObject* parent) : QObject(parent), m_editor(editor) {
	QSettings settings;
	m_access = settings.value("claude/access").toString() == QStringLiteral("change") ? ClaudeAccess::Change : ClaudeAccess::Read;
	m_ask = settings.value("claude/askBeforeChange", false).toBool();
	// Switched on again at start when the mapper left it on (Mapsmith writes a new secret each start).
	if (settings.value("claude/enabled", false).toBool()) QTimer::singleShot(0, this, [this] { setEnabled(true); });
}

ClaudeService::~ClaudeService() {
	blockSignals(true);   // the log and the panel may be gone already
	stopListening(tr("Mapsmith was closed."));
}

void ClaudeService::setEnabled(bool on) {
	QSettings().setValue("claude/enabled", on);
	if (on == m_enabled && (!on || m_server)) return;
	m_enabled = on;
	if (on) startListening();
	else stopListening(tr("Claude was switched off in Mapsmith."));
	emit stateChanged();
}

void ClaudeService::setAccess(ClaudeAccess access) {
	if (access == m_access) return;
	m_access = access;
	QSettings().setValue("claude/access", access == ClaudeAccess::Change ? "change" : "read");
	emit logLine(access == ClaudeAccess::Change ? tr("Claude may now read and change materials") : tr("Claude may now only read"));
	emit stateChanged();
}

void ClaudeService::setAskBeforeChange(bool on) {
	if (on == m_ask) return;
	m_ask = on;
	QSettings().setValue("claude/askBeforeChange", on);
	emit stateChanged();
}

QString ClaudeService::exchangeFolder() const {
	if (!m_exchangeOverride.isEmpty()) return m_exchangeOverride;
	return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Mapsmith/Claude");
}

QString ClaudeService::bridgePath() {
#ifdef Q_OS_WIN
	return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("mapsmith-mcp.exe"));
#else
	return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("mapsmith-mcp"));
#endif
}

bool ClaudeService::listening() const {
	return m_server && m_server->isListening();
}

int ClaudeService::connections() const {
	int n = 0;
	for (const auto& [socket, peer] : m_peers) n += peer->stage == Peer::Stage::Ready ? 1 : 0;
	return n;
}

QString ClaudeService::statusText() const {
	if (!m_enabled) return tr("Off - Claude cannot reach Mapsmith.");
	if (!listening()) return tr("Could not start: %1").arg(m_startError);
	const int n = connections();
	if (n == 0) return tr("On - waiting for Claude. Start a chat in Claude Desktop (or Claude Code) and ask about your materials.");
	return n == 1 ? tr("On - Claude is connected.") : tr("On - %1 Claude chats are connected.").arg(n);
}

// --- Pipe --------------------------------------------------------------------------------------------

void ClaudeService::startListening() {
	m_startError.clear();
	const QString name = pipeName();
	// Another Mapsmith of this user already talks to Claude: leave it alone (its secret stays valid).
	{
		QLocalSocket probe;
		probe.connectToServer(name);
		if (probe.waitForConnected(300)) {
			probe.abort();
			m_startError = tr("another Mapsmith window is already connected to Claude. Switch it off there first.");
			emit logLine(tr("Claude: %1").arg(m_startError));
			return;
		}
	}
	m_secret = newSecret();
	if (!writeSecretFile(secretFilePath(), m_secret)) {
		m_startError = tr("the secret file could not be written in %1").arg(QDir::toNativeSeparators(dataFolder()));
		m_secret.clear();
		emit logLine(tr("Claude: %1").arg(m_startError));
		return;
	}
	QLocalServer::removeServer(name);   // a socket file left by a crash (Linux, macOS)
	m_server = new QLocalServer(this);
	m_server->setSocketOptions(QLocalServer::UserAccessOption);   // only this user may connect
	m_server->setMaxPendingConnections(MAX_PEERS);
	if (!m_server->listen(name)) {
		m_startError = m_server->errorString();
		delete m_server;
		m_server = nullptr;
		emit logLine(tr("Claude: could not start - %1").arg(m_startError));
		return;
	}
	connect(m_server, &QLocalServer::newConnection, this, &ClaudeService::acceptConnections);
	emit logLine(tr("Claude: on - waiting for Claude Desktop or Claude Code (%1)").arg(m_access == ClaudeAccess::Change ? tr("read and change materials") : tr("read only")));
}

void ClaudeService::stopListening(const QString& reason) {
	for (const auto& [socket, peer] : m_peers) {
		socket->disconnect(this);
		if (peer->stage == Peer::Stage::Ready) socket->write(jsonLine(QJsonObject{{"type", "closing"}, {"reason", reason}}));
		socket->flush();
		socket->abort();
		socket->deleteLater();
		delete peer;
	}
	m_peers.clear();
	if (m_server) {
		m_server->close();
		delete m_server;
		m_server = nullptr;
		emit logLine(tr("Claude: off"));
	}
	// The secret goes with the pipe (only if it is still ours: another Mapsmith may have written its own).
	if (!m_secret.isEmpty() && readSecretFile(secretFilePath()) == m_secret) QFile::remove(secretFilePath());
	m_secret.clear();
}

void ClaudeService::acceptConnections() {
	while (m_server && m_server->hasPendingConnections()) {
		QLocalSocket* socket = m_server->nextPendingConnection();
		if (!socket) break;
		if (int(m_peers.size()) >= MAX_PEERS) {
			socket->abort();
			socket->deleteLater();
			continue;
		}
		m_peers[socket] = new Peer;
		connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readPeer(socket); });
		connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
			const auto it = m_peers.find(socket);
			if (it == m_peers.end()) return;
			const bool wasReady = it->second->stage == Peer::Stage::Ready;
			delete it->second;
			m_peers.erase(it);
			socket->deleteLater();
			if (wasReady) emit logLine(tr("Claude: a chat disconnected"));
			emit stateChanged();
		});
		// A connection that does not prove itself in time is closed.
		QTimer::singleShot(HANDSHAKE_MS, socket, [this, socket] {
			const auto it = m_peers.find(socket);
			if (it != m_peers.end() && it->second->stage != Peer::Stage::Ready) socket->abort();
		});
	}
}

void ClaudeService::readPeer(QLocalSocket* socket) {
	const auto it = m_peers.find(socket);
	if (it == m_peers.end()) return;
	Peer& peer = *it->second;
	if (!peer.buffer.append(socket->readAll())) {
		emit logLine(tr("Claude: a message was too long - connection closed"));
		socket->abort();
		return;
	}
	while (auto line = peer.buffer.takeLine()) {
		const auto message = parseJsonLine(*line);
		if (!message) {
			socket->abort();
			return;
		}
		handlePeerMessage(socket, peer, *message);
		if (!m_peers.count(socket)) return;   // closed while handling
	}
	// Calls run one after the other (a confirmation dialog runs the event loop meanwhile).
	if (m_busy) return;
	while (m_peers.count(socket) && !m_peers[socket]->calls.empty() && !m_busy) {
		const QJsonObject call = m_peers[socket]->calls.front();
		m_peers[socket]->calls.pop_front();
		const QPointer<QLocalSocket> guard(socket);
		const QJsonObject result = callTool(call["tool"].toString(), call["arguments"].toObject());
		if (!guard || !m_peers.count(socket)) return;   // switched off meanwhile
		socket->write(jsonLine(QJsonObject{{"type", "result"}, {"id", call["id"]}, {"result", result}}));
	}
	// Other connections that queued calls while this one ran.
	for (const auto& [other, otherPeer] : m_peers) {
		if (other != socket && !otherPeer->calls.empty()) {
			QTimer::singleShot(0, this, [this, other = QPointer<QLocalSocket>(other)] {
				if (other) readPeer(other);
			});
		}
	}
}

void ClaudeService::handlePeerMessage(QLocalSocket* socket, Peer& peer, const QJsonObject& message) {
	const QString type = message["type"].toString();
	const auto isHex = [](const QByteArray& text) {
		if (text.size() < 16 || text.size() > 64) return false;
		for (const char c : text) {
			if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
		}
		return true;
	};
	if (peer.stage == Peer::Stage::Hello && type == QStringLiteral("hello")) {
		const QByteArray nonce = message["nonce"].toString().toLatin1();
		if (!isHex(nonce)) {
			socket->abort();
			return;
		}
		peer.appNonce = newNonce();
		peer.stage = Peer::Stage::Auth;
		socket->write(jsonLine(QJsonObject{{"type", "challenge"}, {"nonce", QString::fromLatin1(peer.appNonce)},
		                                   {"proof", QString::fromLatin1(proof(m_secret, "app", nonce))}}));
	} else if (peer.stage == Peer::Stage::Auth && type == QStringLiteral("auth")) {
		if (!proofMatches(message["proof"].toString().toLatin1(), proof(m_secret, "bridge", peer.appNonce))) {
			emit logLine(tr("Claude: a program without Mapsmith's secret tried to connect - refused"));
			record(QStringLiteral("connect"), {}, tr("Refused: the program could not prove it is Mapsmith's bridge"), false, false);
			socket->write(jsonLine(QJsonObject{{"type", "refused"}, {"reason", "Not allowed."}}));
			socket->flush();
			socket->abort();
			return;
		}
		peer.stage = Peer::Stage::Ready;
		socket->write(jsonLine(QJsonObject{{"type", "welcome"}, {"access", m_access == ClaudeAccess::Change ? "change" : "read"}}));
		record(QStringLiteral("connect"), {}, tr("Claude connected"), false, true);
		emit stateChanged();
	} else if (peer.stage == Peer::Stage::Ready && type == QStringLiteral("call")) {
		if (peer.calls.size() >= 32) {   // a flood: answer at once
			socket->write(jsonLine(QJsonObject{{"type", "result"}, {"id", message["id"]}, {"result", textResult(tr("Too many calls at once - wait for the answers."), true)}}));
			return;
		}
		peer.calls.push_back(message);
	} else {
		socket->abort();
	}
}

// --- Calls -------------------------------------------------------------------------------------------

QJsonObject ClaudeService::callTool(const QString& tool, const QJsonObject& arguments) {
	if (!toolExists(tool)) {
		record(tool.left(60), {}, tr("Refused: unknown tool"), false, false);
		return textResult(tr("Unknown tool."), true);
	}
	const bool change = toolChangesSomething(tool);
	if (!m_callLimit.allow()) {
		record(tool, {}, tr("Refused: too many calls"), change, false);
		return textResult(tr("Too many calls - wait a minute."), true);
	}
	if (change && m_access != ClaudeAccess::Change) {
		record(tool, {}, tr("Refused: Claude may only read"), true, false);
		return textResult(tr("Mapsmith lets Claude only read. The mapper can choose \"Read and change materials\" in Project > Claude."), true);
	}
	if (change && !m_changeLimit.allow()) {
		const int wait = m_changeLimit.waitSeconds();
		record(tool, {}, tr("Refused: too many changes"), true, false);
		return textResult(tr("Too many changes in a short time - wait %1 seconds.").arg(wait), true);
	}
	m_busy = true;
	QString what;
	QJsonObject result;
	try {
		result = runTool(tool, arguments, what);
	} catch (const std::exception& e) {
		result = textResult(tr("Mapsmith could not do this: %1").arg(QString::fromUtf8(e.what())), true);
	}
	m_busy = false;
	const bool error = result["isError"].toBool();
	QString summary = tr("Done");
	if (error) {
		const auto content = result["content"].toArray();
		summary = content.isEmpty() ? tr("Error") : tr("Not done: %1").arg(content.first().toObject()["text"].toString().left(160));
	}
	record(tool, what, summary, change, !error);
	return result;
}

void ClaudeService::record(const QString& tool, const QString& what, const QString& result, bool change, bool ok) {
	ClaudeActivity a{QDateTime::currentDateTime(), tool, what.left(200), result, change, ok};
	m_activity.push_back(a);
	while (m_activity.size() > MAX_ACTIVITY) m_activity.removeFirst();
	emit activityAdded(a);
	if (change || !ok) emit logLine(tr("Claude: %1%2 - %3").arg(tool, what.isEmpty() ? QString() : QStringLiteral(" ") + what, result));
}

// --- Backups and undo -------------------------------------------------------------------------------

QString ClaudeService::backup(const QString& file, QString* error) {
	if (!QFileInfo::exists(file)) return {};
	const QString folder = exchangeFolder() + QStringLiteral("/backups");
	if (!QDir().mkpath(folder)) {
		if (error) *error = tr("the backup folder could not be made");
		return QString();
	}
	const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"));
	QString name = QFileInfo(file).fileName();
	name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
	QString target = QDir(folder).filePath(stamp + QStringLiteral("_") + name);
	for (int i = 2; QFileInfo::exists(target); ++i) target = QDir(folder).filePath(stamp + QStringLiteral("_%1_").arg(i) + name);
	if (!QFile::copy(file, target)) {
		if (error) *error = tr("the backup could not be written");
		return QString();
	}
	return target;
}

void ClaudeService::pushUndo(UndoStep step) {
	m_undo.push_back(std::move(step));
	while (m_undo.size() > MAX_UNDO) m_undo.erase(m_undo.begin());
	emit stateChanged();
}

QString ClaudeService::undoText() const {
	return m_undo.empty() ? QString() : m_undo.back().what;
}

bool ClaudeService::undoLastChange(QString* error) {
	if (m_undo.empty()) return false;
	const UndoStep step = m_undo.back();
	m_undo.pop_back();
	bool ok = true;
	if (step.backup.isEmpty()) {
		ok = !QFileInfo::exists(step.target) || QFile::remove(step.target);
	} else {
		QFile in(step.backup);
		QSaveFile out(step.target);
		ok = in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly) && out.write(in.readAll()) >= 0 && out.commit();
	}
	if (!ok) {
		if (error) *error = tr("%1 could not be restored").arg(QDir::toNativeSeparators(step.target));
		emit stateChanged();
		return false;
	}
	if (step.decisions) m_editor->materialDecisions()->reload();
	if (!step.material.isEmpty() && m_host.materialChanged) m_host.materialChanged(step.material);
	record(tr("undo"), step.what, tr("Undone by the mapper"), true, true);
	emit stateChanged();
	return true;
}

// --- Claude Desktop ----------------------------------------------------------------------------------

QString ClaudeService::connectClaudeDesktop(QString* error) {
	const QString bridge = bridgePath();
	if (!QFileInfo(bridge).isFile()) {
		if (error) *error = tr("The bridge program %1 is missing next to Mapsmith.").arg(QDir::toNativeSeparators(bridge));
		return {};
	}
	QStringList changed;
	for (const QString& file : claudeDesktopConfigFiles()) {
		QByteArray existing;
		if (QFileInfo(file).isFile()) {
			QFile in(file);
			if (!in.open(QIODevice::ReadOnly) || in.size() > 4 * 1024 * 1024) {
				if (error) *error = tr("%1 could not be read.").arg(QDir::toNativeSeparators(file));
				return {};
			}
			existing = in.readAll();
		}
		QString mergeError;
		const auto merged = mergeClaudeDesktopConfig(existing, bridge, &mergeError);
		if (!merged) {
			if (error) *error = mergeError;
			return {};
		}
		if (!existing.isEmpty()) {
			const QString copy = file + QStringLiteral(".mapsmith-backup");
			QFile::remove(copy);
			if (!QFile::copy(file, copy)) {
				if (error) *error = tr("The backup of %1 could not be written.").arg(QDir::toNativeSeparators(file));
				return {};
			}
		}
		QDir().mkpath(QFileInfo(file).absolutePath());
		QSaveFile out(file);
		if (!out.open(QIODevice::WriteOnly) || out.write(*merged) != merged->size() || !out.commit()) {
			if (error) *error = tr("%1 could not be written.").arg(QDir::toNativeSeparators(file));
			return {};
		}
		changed << QDir::toNativeSeparators(file);
	}
	emit logLine(tr("Claude Desktop connected: %1 (restart Claude Desktop)").arg(changed.join(QStringLiteral(", "))));
	return changed.join(QStringLiteral("\n"));
}

} // namespace sme::app
