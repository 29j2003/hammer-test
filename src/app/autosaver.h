// autosaver.h - Automatic backups of the open map and recovery after a crash.
//
// Every few minutes (setting "autosave/minutes", default 5, 0 = off) a changed map is written to
// "<map folder>/autosave/<map>.<time>.vmf" (a map never saved goes to the app's data folder).
// Only the newest "autosave/keep" (default 10) copies stay. The map is turned into text on the UI
// thread (fast) and written in the background.
//
// A small marker file says "the editor is running with this map". A clean exit removes it; if it
// is still there at the next start, the editor did not close normally and the newest autosave
// (when it is newer than the map file) is offered back.

#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

#include <optional>

class QWidget;

namespace sme::app {

class Editor;

/// Autosaves that could be restored for one map.
struct RecoveryOffer {
	QString mapPath;           ///< The map they belong to; empty for a map that was never saved
	QStringList autosaves;     ///< Newest first
	QList<QDateTime> times;    ///< When each was written
	bool afterCrash = false;   ///< Found because the last session did not end normally
};

/// Writes the autosaves and keeps the session marker. One per editor window.
class Autosaver : public QObject {
	Q_OBJECT

public:
	explicit Autosaver(Editor* editor, QObject* parent = nullptr);
	~Autosaver() override;

	/// Minutes between autosaves (0 = off); stored in the settings.
	[[nodiscard]] static int intervalMinutes();
	void setIntervalMinutes(int minutes);
	/// How many autosaves of a map are kept.
	[[nodiscard]] static int keepCount();

	/// Writes an autosave now if the map changed since the last one (or `force`). Returns the file
	/// that is being written, or an empty string. The writing itself runs in the background.
	QString autosaveNow(bool force = false);
	/// Waits until all background writes are done (tests, quitting).
	void waitForWrites();

	/// Folder and map name the autosaves of `mapPath` use (empty path: the app's folder, "untitled").
	[[nodiscard]] static QString folderFor(const QString& mapPath);
	[[nodiscard]] static QString mapNameFor(const QString& mapPath);
	/// The marker file of a running session.
	[[nodiscard]] static QString markerPath();

	/// Marks the session as running (with the current map). Called by the constructor.
	void startSession();
	/// Removes the marker: the session ended normally. Called when the application quits.
	void endSession();

private:
	void updateMarker();

	Editor* m_editor;
	QTimer m_timer;
	QThreadPool m_pool;               ///< One thread: writes happen in order
	unsigned m_savedRevision = 0;     ///< History revision of the last autosave
	QString m_markerMap;              ///< Map path written into the marker
	bool m_sessionOpen = false;
};

/// Autosaves of the last session if it did not end normally and they are newer than its map.
/// Call before an Autosaver starts the new session (it overwrites the marker).
[[nodiscard]] std::optional<RecoveryOffer> recoveryAfterCrash();
/// Autosaves of `mapPath` that are newer than the file itself.
[[nodiscard]] std::optional<RecoveryOffer> recoveryForMap(const QString& mapPath);
/// Asks whether to restore one of the offered autosaves and opens it if so. Returns true when an
/// autosave was opened.
bool offerRecovery(Editor* editor, QWidget* parent, const RecoveryOffer& offer);
/// Opens a map, but first offers a newer autosave of it if there is one.
bool openMapWithRecovery(Editor* editor, QWidget* parent, const QString& path, QString* error = nullptr);

} // namespace sme::app
