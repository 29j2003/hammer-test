// panels.h - Smaller docks: Outliner (all objects), Problems (checks with autofix), Map Budget and the log.

#pragma once

#include <QPlainTextEdit>
#include <QFutureWatcher>
#include <QWidget>

#include <functional>
#include <vector>

#include <sme/checks.h>
#include <sme/map_budget.h>

class QLineEdit;
class QTreeWidget;
class QListWidget;
class QLabel;
class QGridLayout;
class QProgressBar;

namespace sme::app {

class Editor;
class AssetBrowser;

/// The Outliner dock: world brushes and entities as a tree with a filter; clicking selects.
class Outliner : public QWidget {
	Q_OBJECT

public:
	explicit Outliner(Editor* editor, QWidget* parent = nullptr);
	/// Icons for the entity classes (the entity picker's sprites and model previews).
	void setIconProvider(std::function<QIcon(const QString&)> provider);
	/// Asks the provider again (its pictures arrive later).
	void refreshIcons();

signals:
	/// Double-click: frame the selection in the views.
	void frameRequested();

private:
	void rebuild();

	std::function<QIcon(const QString&)> m_icons;

	Editor* m_editor;
	QLineEdit* m_filter;
	QTreeWidget* m_tree;
	bool m_pending = false;
	bool m_updatingSelection = false;
};

/// The Problems dock: results of the map check with autofix buttons, plus leaks from compiles.
class ProblemsPanel : public QWidget {
	Q_OBJECT

public:
	ProblemsPanel(Editor* editor, AssetBrowser* assets, QWidget* parent = nullptr);
	~ProblemsPanel() override;

	/// Runs all checks now (also called before compiling).
	void runChecks();
	/// Runs all checks on a copy of the map on another thread and shows them when done (after a
	/// map was opened: a big map's checks took the editor a second).
	void runChecksInBackground();
	/// Adds a leak from a compile (VBSP pointfile).
	void addLeak(const std::vector<Vec3>& line);
	[[nodiscard]] int errorCount() const;

	/// Every problem with its fix as text ("Copy all").
	[[nodiscard]] QString problemsText() const;

signals:
	/// "Show leak line" was pressed / a problem's objects were selected and should be framed.
	void showLeak(const std::vector<Vec3>& line);
	void frameRequested();
	/// "Make report" was pressed (something looked wrong in the game).
	void reportRequested();
	/// A problem's button asks for an app action instead of a map edit (Problem::action,
	/// "buildCubemaps").
	void actionRequested(const QString& action);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void showProblems();
	/// What the checks get besides the map (`names` and `limits` must outlive the result).
	CheckContext checkContext(const std::vector<std::string>& names, BudgetLimits& limits);
	/// Waits for a check on the other thread; its result is dropped (a newer one comes).
	void dropBackgroundCheck();
	/// Gives every row the height its wrapped text and buttons need at the list's width.
	void fitRows();

	Editor* m_editor;
	AssetBrowser* m_assets;
	QListWidget* m_list;
	QLabel* m_summary;
	std::vector<Problem> m_problems;
	std::vector<Problem> m_leaks;
	std::optional<FeatureBundle> m_bundle;   ///< The features Mapsmith brings (read once, again with "Check map")
	bool m_bundleRead = false;
	CheckCache m_cache;            ///< Models and sounds read by the checks (cleared with the game or "Check map")
	/// A check on another thread (runChecksInBackground): it uses m_cache while it runs.
	struct BackgroundCheck;
	std::shared_ptr<BackgroundCheck> m_background;
	QFutureWatcher<std::vector<Problem>> m_backgroundWatcher;
	bool m_backgroundAgain = false;   ///< Asked again while one ran
	bool m_backgroundDropped = false; ///< Its result is not shown
};

/// The Map Budget dock: live counts of brushes, sides, planes, entities, edicts, ... against the
/// engine limits of the current game (map_budget.h). Counts again shortly after a change, and
/// only while the dock is visible.
class BudgetPanel : public QWidget {
	Q_OBJECT

public:
	explicit BudgetPanel(Editor* editor, QWidget* parent = nullptr);

	/// Counts now (also for tests).
	void refresh();
	[[nodiscard]] const std::vector<BudgetItem>& items() const { return m_items; }

protected:
	void showEvent(QShowEvent* event) override;

private:
	void scheduleRefresh();

	Editor* m_editor;
	QGridLayout* m_grid;
	QLabel* m_summary;
	std::vector<BudgetItem> m_items;
	bool m_dirty = true;
	bool m_scheduled = false;
};

/// The Log dock: messages and compile output in colour.
class LogPanel : public QPlainTextEdit {
	Q_OBJECT

public:
	explicit LogPanel(QWidget* parent = nullptr);
	/// Adds a line (default colour when `color` is invalid).
	void append(const QString& text, const QColor& color = {});
};

} // namespace sme::app
