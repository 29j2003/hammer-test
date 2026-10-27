// maps_panel.h - All maps of the chosen game or mod in one list: which ones are compiled and up to
// date, what the last compile said, and a batch compile that builds the checked (or all outdated)
// maps one after another with the current compile settings. Also new / duplicate / rename /
// delete for map files. The main window shows it on the Project page.

#pragma once

#include <QDateTime>
#include <QPointer>
#include <QStringList>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QCheckBox;
class QTreeWidget;
class QTreeWidgetItem;

namespace sme::app {

class CompileRunner;
class Editor;

/// Whether a map's BSP exists and is newer than the map file.
enum class BspState { NotCompiled, UpToDate, Outdated };

/// The BSP of a map: <game>/maps/<name>.bsp, else the one VBSP left next to the VMF; empty if none.
[[nodiscard]] QString compiledBsp(const QString& vmfPath, const QString& gameDir);
/// BspState of a map (compares modification times).
[[nodiscard]] BspState bspState(const QString& vmfPath, const QString& gameDir);

/// The Maps tab of the Project page (see the file comment).
class MapsPanel : public QWidget {
	Q_OBJECT

public:
	/// `compiler` is the editor's one compile runner (its output also goes to the Compile panel).
	MapsPanel(Editor* editor, CompileRunner* compiler, QWidget* parent = nullptr);

	/// Reads the map folder again.
	void refresh();
	/// The map folder that is listed (the game's mapsrc or the Hammer config's map folder).
	[[nodiscard]] QString mapFolder() const { return m_folder; }
	/// Paths of all listed maps, and of the checked ones.
	[[nodiscard]] QStringList maps() const;
	[[nodiscard]] QStringList checkedMaps() const;
	void setChecked(const QString& vmfPath, bool checked);

	// File operations without questions (the buttons ask first). They return the new path (or
	// true) and set `error` when they fail.
	QString createMap(const QString& name, QString* error = nullptr);
	QString duplicateMap(const QString& vmfPath, QString* error = nullptr);
	QString renameMap(const QString& vmfPath, const QString& newName, QString* error = nullptr);
	bool deleteMap(const QString& vmfPath, QString* error = nullptr);

	/// Compiles the maps one after another (no game in between). With `runAfter` the game starts
	/// with the last map that compiled. False if nothing to do or a compile is already running.
	bool compileMaps(const QStringList& vmfPaths, bool runAfter);
	/// Stops the batch after cancelling the running compile.
	void cancelBatch();
	[[nodiscard]] bool batchRunning() const { return !m_queue.isEmpty() || !m_current.isEmpty(); }

	/// Last compile result of a map as stored in the settings: "ok", "failed" or empty.
	[[nodiscard]] QString lastResult(const QString& vmfPath) const;

signals:
	/// The mapper wants to open this map.
	void openMapRequested(const QString& vmfPath);
	/// A batch began (the main window shows the Compile panel).
	void compileStarted();
	/// The batch ended: how many maps compiled and how many failed.
	void batchFinished(int compiled, int failed);

private:
	void buildRows();
	void updateRow(QTreeWidgetItem* item);
	[[nodiscard]] QTreeWidgetItem* rowFor(const QString& vmfPath) const;
	[[nodiscard]] QString currentPath() const;
	[[nodiscard]] QString settingsKey(const QString& vmfPath) const;
	void startNext();
	void finishCurrent(bool ok);
	void updateButtons();

	Editor* m_editor;
	QPointer<CompileRunner> m_compiler;
	QString m_folder;
	QLabel* m_folderLabel;
	QTreeWidget* m_table;
	QProgressBar* m_progress;
	QLabel* m_batchLabel;
	QCheckBox* m_runAfter;
	QPushButton *m_open, *m_duplicate, *m_rename, *m_delete, *m_compileChecked, *m_compileOutdated, *m_cancel;

	// Batch state
	QStringList m_queue;        ///< Maps still to compile
	QString m_current;          ///< Map compiling now (empty between maps)
	QString m_lastOk;           ///< Last map that compiled (for "run the game afterwards")
	int m_total = 0, m_compiled = 0, m_failed = 0;
	int m_stepCount = 0, m_step = 0;
	bool m_runGame = false;
};

} // namespace sme::app
