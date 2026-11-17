// main.cpp - mapsmith-mcp: the bridge between Claude (Claude Desktop, Claude Code) and a running
// Mapsmith. Claude starts it and speaks MCP over stdin/stdout (one JSON-RPC message per line);
// the bridge answers initialize, ping and tools/list itself and passes every tools/call to Mapsmith
// over a local pipe (claude_protocol.h: per-user name, mutual proof of the secret Mapsmith wrote).
// The bridge itself reads and writes no files except the secret, opens no network connection and
// starts no programs: everything Claude can do is what Mapsmith's tools allow. Diagnostics go to
// stderr (Claude Desktop shows them in its MCP log); stdout carries only the protocol.

#include "claude_protocol.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocalSocket>
#include <QTimer>

#include <cstdio>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace sme::claude;

namespace {

const QString NOT_RUNNING = QStringLiteral(
    "Mapsmith is not running, or Claude is switched off in Mapsmith. Ask the mapper to open Mapsmith and switch on "
    "Project > Claude, then try again.");

void writeOut(const QByteArray& line) {
	std::fwrite(line.constData(), 1, std::size_t(line.size()), stdout);
	std::fflush(stdout);
}

void logError(const QString& text) {
	const QByteArray bytes = ("mapsmith-mcp: " + text + '\n').toUtf8();
	std::fwrite(bytes.constData(), 1, std::size_t(bytes.size()), stderr);
	std::fflush(stderr);
}

/// The connection to Mapsmith: connects when a tool is called, proves itself, passes calls on.
class Bridge : public QObject {
public:
	explicit Bridge(McpSession* session) : m_session(session), m_pipe(PIPE_LINE_MAX) {
		connect(&m_socket, &QLocalSocket::connected, this, [this] {
			m_nonce = newNonce();
			m_state = State::Challenge;
			m_socket.write(jsonLine(QJsonObject{{"type", "hello"}, {"nonce", QString::fromLatin1(m_nonce)}, {"version", QStringLiteral(SME_VERSION)}}));
		});
		connect(&m_socket, &QLocalSocket::readyRead, this, &Bridge::readPipe);
		connect(&m_socket, &QLocalSocket::disconnected, this, [this] { fail(NOT_RUNNING); });
		connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
			if (m_state == State::Connecting) fail(NOT_RUNNING);
		});
		m_timeout.setSingleShot(true);
		connect(&m_timeout, &QTimer::timeout, this, [this] {
			fail(m_state == State::Ready ? QStringLiteral("Mapsmith did not answer in time.") : NOT_RUNNING);
		});
	}

	/// A tools/call from Claude.
	void call(const QJsonValue& id, const QString& tool, const QJsonObject& arguments) {
		const int key = ++m_lastKey;
		m_calls.insert(key, Call{id, tool, arguments});
		if (m_state == State::Ready) {
			send(key);
		} else if (m_state == State::Idle) {
			m_state = State::Connecting;
			m_pipe = LineBuffer(PIPE_LINE_MAX);
			m_timeout.start(5000);
			m_socket.connectToServer(pipeName());
		}
	}

private:
	enum class State { Idle, Connecting, Challenge, Proven, Ready };
	struct Call {
		QJsonValue id;
		QString tool;
		QJsonObject arguments;
		bool sent = false;
	};

	void send(int key) {
		auto& call = m_calls[key];
		call.sent = true;
		m_socket.write(jsonLine(QJsonObject{{"type", "call"}, {"id", key}, {"tool", call.tool}, {"arguments", call.arguments}}));
		// "Ask before each change" waits for the mapper: give them time.
		m_timeout.start(10 * 60 * 1000);
	}

	void readPipe() {
		if (!m_pipe.append(m_socket.readAll())) {
			fail(QStringLiteral("Mapsmith sent a message that is too long."));
			return;
		}
		while (auto line = m_pipe.takeLine()) {
			const auto message = parseJsonLine(*line);
			if (!message) {
				fail(QStringLiteral("Mapsmith sent something the bridge does not understand."));
				return;
			}
			handle(*message);
			if (m_state == State::Idle) return;
		}
	}

	void handle(const QJsonObject& message) {
		const QString type = message["type"].toString();
		if (m_state == State::Challenge && type == QStringLiteral("challenge")) {
			// Mapsmith proves first that it knows the secret (a program squatting the pipe's name cannot).
			const QByteArray secret = readSecretFile(secretFilePath());
			if (secret.isEmpty() || !proofMatches(message["proof"].toString().toLatin1(), proof(secret, "app", m_nonce))) {
				logError(QStringLiteral("the program on Mapsmith's pipe could not prove it is Mapsmith - disconnected"));
				fail(QStringLiteral("The program answering on Mapsmith's connection could not prove it is Mapsmith, so the bridge did not talk to "
				                    "it. Restart Mapsmith."));
				return;
			}
			const QByteArray appNonce = message["nonce"].toString().toLatin1().left(64);
			m_state = State::Proven;
			m_socket.write(jsonLine(QJsonObject{{"type", "auth"}, {"proof", QString::fromLatin1(proof(secret, "bridge", appNonce))}}));
		} else if (m_state == State::Proven && type == QStringLiteral("welcome")) {
			m_state = State::Ready;
			m_timeout.stop();
			for (auto it = m_calls.begin(); it != m_calls.end(); ++it) {
				if (!it->sent) send(it.key());
			}
		} else if (type == QStringLiteral("refused") || type == QStringLiteral("closing")) {
			fail(message["reason"].toString().left(500).isEmpty() ? NOT_RUNNING : message["reason"].toString().left(500));
		} else if (m_state == State::Ready && type == QStringLiteral("result")) {
			const int key = message["id"].toInt();
			const auto it = m_calls.find(key);
			if (it == m_calls.end()) return;
			const QJsonValue id = it->id;
			m_calls.erase(it);
			m_session->replyTool(id, message["result"].toObject());
			if (m_calls.isEmpty()) m_timeout.stop();
		} else {
			fail(QStringLiteral("Mapsmith sent something unexpected."));
		}
	}

	/// Ends the connection; every open call gets `text` as its (error) result.
	void fail(const QString& text) {
		if (m_state == State::Idle && m_calls.isEmpty()) return;
		m_state = State::Idle;
		m_timeout.stop();
		const auto calls = m_calls;
		m_calls.clear();
		for (const auto& call : calls) m_session->replyTool(call.id, textResult(text, true));
		m_socket.abort();
	}

	McpSession* m_session;
	QLocalSocket m_socket;
	LineBuffer m_pipe;
	QTimer m_timeout;
	State m_state = State::Idle;
	QByteArray m_nonce;
	QHash<int, Call> m_calls;
	int m_lastKey = 0;
};

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("mapsmith-mcp"));
	if (app.arguments().contains(QStringLiteral("--version"))) {
		writeOut(QByteArrayLiteral("mapsmith-mcp " SME_VERSION "\n"));
		return 0;
	}
	Bridge* bridge = nullptr;
	McpSession session(QStringLiteral(SME_VERSION), writeOut, [&bridge](const QJsonValue& id, const QString& name, const QJsonObject& arguments) {
		bridge->call(id, name, arguments);
	});
	Bridge realBridge(&session);
	bridge = &realBridge;
	LineBuffer input(MCP_LINE_MAX);
	// stdin is read on its own thread (no portable way to watch it in Qt's event loop on Windows);
	// the bytes are handed to the main thread.
	std::thread reader([&app, &session, &input] {
		char buffer[65536];
		for (;;) {
			// read() returns what has arrived (fread would wait for a full buffer).
#ifdef _WIN32
			const int n = _read(_fileno(stdin), buffer, int(sizeof buffer));
#else
			const auto n = ::read(0, buffer, sizeof buffer);
#endif
			if (n <= 0) break;
			QByteArray bytes(buffer, qsizetype(n));
			QMetaObject::invokeMethod(&app, [&app, &session, &input, bytes] {
				if (!input.append(bytes)) {
					logError(QStringLiteral("a message from Claude is longer than 4 MB - stopping"));
					app.exit(1);
					return;
				}
				while (auto line = input.takeLine()) session.handleLine(*line);
			}, Qt::QueuedConnection);
		}
		QMetaObject::invokeMethod(&app, [&app] { app.quit(); }, Qt::QueuedConnection);
	});
	reader.detach();
	return app.exec();
}
