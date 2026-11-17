// claude_service.h - Mapsmith's side of the Claude integration (Project › Claude). When the mapper
// switches it on, Mapsmith listens on a local pipe (QLocalServer, only this user may connect, see
// claude_protocol.h) for the bridge `mapsmith-mcp` that Claude Desktop or Claude Code starts. A
// connection must prove it knows the secret Mapsmith wrote for this start; then Claude can call
// the tools of claude_protocol.h, and only those: reading the open game's materials, the map's
// problems, the log and a picture of the 3D view, and - with "Read and change materials" - the
// mod's decision file, the mod's own .vmt files and files in one fixed exchange folder
// (Documents/Mapsmith/Claude). Every name and path Claude sends is checked in the core
// (sme/claude_scope.h); every change keeps a backup and can be undone here; changes are rate
// limited; with "Ask before each change" the mapper confirms each one. Every call is listed in the
// activity (Project › Claude) and written to the log. Switching off closes the pipe at once.
// The tools themselves are in claude_tools.cpp.

#pragma once

#include <QDateTime>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <sme/claude_scope.h>

class QLocalServer;
class QLocalSocket;

namespace sme::app {

class Editor;

/// What Claude may do.
enum class ClaudeAccess {
	Read,     ///< Read only: materials, problems, log, pictures, the exchange folder
	Change,   ///< Also change material decisions, the mod's own materials and files in the exchange folder
};

/// What the service needs from the main window (set by MainWindow; empty functions are skipped).
struct ClaudeHost {
	std::function<QImage()> screenshot;                               ///< The active 3D view
	std::function<QString()> log;                                     ///< Mapsmith's log
	std::function<QString()> compileLog;                              ///< The last compile's log
	std::function<QString()> problems;                                ///< The Problems panel as text
	std::function<bool(const QString& path, QString* error)> writeReport;   ///< The editor report
	std::function<void(const QString& material)> materialChanged;     ///< A .vmt changed: show it
	/// "Ask before each change": true when the mapper allows it.
	std::function<bool(const QString& tool, const QString& what)> confirm;
};

/// One call in the activity list.
struct ClaudeActivity {
	QDateTime time;
	QString tool;
	QString what;     ///< Short: the material, the file
	QString result;   ///< "Done", "Refused: ...", "Error: ..."
	bool change = false;
	bool ok = true;
};

class ClaudeService : public QObject {
	Q_OBJECT

public:
	explicit ClaudeService(Editor* editor, QObject* parent = nullptr);
	~ClaudeService() override;

	void setHost(ClaudeHost host) { m_host = std::move(host); }

	/// Switched on (kept in the settings; off by default). On: listens; off: closes every connection.
	[[nodiscard]] bool enabled() const { return m_enabled; }
	void setEnabled(bool on);
	[[nodiscard]] ClaudeAccess access() const { return m_access; }
	void setAccess(ClaudeAccess access);
	[[nodiscard]] bool askBeforeChange() const { return m_ask; }
	void setAskBeforeChange(bool on);

	/// The only folder Claude reads and writes files in (Documents/Mapsmith/Claude; made when needed).
	[[nodiscard]] QString exchangeFolder() const;
	/// For tests only (never reachable from Claude).
	void setExchangeFolderForTests(const QString& folder) { m_exchangeOverride = folder; }
	/// The bridge program next to Mapsmith's executable.
	[[nodiscard]] static QString bridgePath();

	/// Listening, connected Claudes, or why it could not start.
	[[nodiscard]] QString statusText() const;
	[[nodiscard]] bool listening() const;
	[[nodiscard]] int connections() const;
	[[nodiscard]] const QList<ClaudeActivity>& activity() const { return m_activity; }

	/// Undo of Claude's changes (newest first): the backup goes back in place.
	[[nodiscard]] bool canUndo() const { return !m_undo.empty(); }
	[[nodiscard]] QString undoText() const;
	bool undoLastChange(QString* error = nullptr);

	/// Runs one tool as if Claude called it (the access level, rate limit and confirmation apply).
	/// Public for the tests; the pipe calls it for every "call".
	QJsonObject callTool(const QString& tool, const QJsonObject& arguments);

	/// Connects Claude Desktop: adds the bridge to its settings file (backup next to it). The
	/// file it changed, or "" and why.
	QString connectClaudeDesktop(QString* error);

signals:
	void stateChanged();
	void activityAdded(const ClaudeActivity& activity);
	/// A line for Mapsmith's log.
	void logLine(const QString& text);

private:
	struct Peer;
	struct UndoStep {
		QString target;   ///< The file Claude changed
		QString backup;   ///< Its copy before ("" = it did not exist)
		QString what;
		bool decisions = false;
		QString material;   ///< A .vmt of this material (reload after undo)
	};

	void startListening();
	void stopListening(const QString& reason);
	void acceptConnections();
	void readPeer(QLocalSocket* socket);
	void handlePeerMessage(QLocalSocket* socket, Peer& peer, const QJsonObject& message);
	void record(const QString& tool, const QString& what, const QString& result, bool change, bool ok);

	// --- Tools (claude_tools.cpp) ---
	QJsonObject runTool(const QString& tool, const QJsonObject& arguments, QString& what);
	QJsonObject toolStatus();
	QJsonObject toolListMaterials(const QJsonObject& arguments);
	QJsonObject toolGetMaterial(const QJsonObject& arguments, QString& what);
	QJsonObject toolMaterialPictures(const QJsonObject& arguments, QString& what);
	QJsonObject toolProblems();
	QJsonObject toolLog(const QJsonObject& arguments);
	QJsonObject toolScreenshot();
	QJsonObject toolListExchange();
	QJsonObject toolReadExchange(const QJsonObject& arguments, QString& what);
	QJsonObject toolSetDecision(const QJsonObject& arguments, QString& what);
	QJsonObject toolEditMaterial(const QJsonObject& arguments, QString& what);
	QJsonObject toolWriteExchange(const QJsonObject& arguments, QString& what);
	QJsonObject toolEditorReport(QString& what);
	/// A copy of `file` in exchange/backups before Claude changes it ("" when it does not exist yet).
	QString backup(const QString& file, QString* error);
	void pushUndo(UndoStep step);
	/// Personal folders (the home folder) masked in text going to Claude.
	[[nodiscard]] QString masked(const QString& text) const;
	/// The world materials of the open game (cached until the game changes).
	const std::vector<std::string>& worldMaterials();
	[[nodiscard]] std::set<std::string> openMapMaterials() const;

	Editor* m_editor;
	ClaudeHost m_host;
	bool m_enabled = false;
	ClaudeAccess m_access = ClaudeAccess::Read;
	bool m_ask = false;
	QString m_exchangeOverride;
	QLocalServer* m_server = nullptr;
	QString m_startError;
	QByteArray m_secret;
	std::map<QLocalSocket*, Peer*> m_peers;
	QList<ClaudeActivity> m_activity;
	std::vector<UndoStep> m_undo;
	ClaudeRateLimit m_changeLimit{30, std::chrono::seconds(60)};
	ClaudeRateLimit m_callLimit{240, std::chrono::seconds(60)};
	std::vector<std::string> m_materials;
	QString m_materialsGame;
	bool m_busy = false;   ///< A tool runs (a confirmation dialog can start the event loop)
};

} // namespace sme::app
