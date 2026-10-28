// panels.h - Smaller docks: Outliner (all objects), Problems (checks with autofix), Map Budget and the log.

#pragma once

#include <QPlainTextEdit>
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

	/// Runs all checks now (also called before compiling).
	void runChecks();
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

private:
	void showProblems();

	Editor* m_editor;
	AssetBrowser* m_assets;
	QListWidget* m_list;
	QLabel* m_summary;
	std::vector<Problem> m_problems;
	std::vector<Problem> m_leaks;
	CheckCache m_cache;            ///< Models and sounds read by the checks (cleared with the game or "Check map")
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
