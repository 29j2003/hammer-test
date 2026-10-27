// panels.h - Smaller docks: Outliner (all objects), Problems (checks with autofix) and the log.

#pragma once

#include <QPlainTextEdit>
#include <QWidget>

#include <functional>
#include <vector>

#include <sme/checks.h>

class QLineEdit;
class QTreeWidget;
class QListWidget;
class QLabel;

namespace sme::app {

class Editor;
class AssetBrowser;

class Outliner : public QWidget {
	Q_OBJECT

public:
	explicit Outliner(Editor* editor, QWidget* parent = nullptr);
	/// Icons for the entity classes (the entity picker's sprites and model previews).
	void setIconProvider(std::function<QIcon(const QString&)> provider);
	/// Asks the provider again (its pictures arrive later).
	void refreshIcons();

signals:
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

class ProblemsPanel : public QWidget {
	Q_OBJECT

public:
	ProblemsPanel(Editor* editor, AssetBrowser* assets, QWidget* parent = nullptr);

	/// Runs all checks now (also called before compiling).
	void runChecks();
	/// Adds a leak from a compile (VBSP pointfile).
	void addLeak(const std::vector<Vec3>& line);
	[[nodiscard]] int errorCount() const;

signals:
	void showLeak(const std::vector<Vec3>& line);
	void frameRequested();

private:
	void showProblems();

	Editor* m_editor;
	AssetBrowser* m_assets;
	QListWidget* m_list;
	QLabel* m_summary;
	std::vector<Problem> m_problems;
	std::vector<Problem> m_leaks;
};

class LogPanel : public QPlainTextEdit {
	Q_OBJECT

public:
	explicit LogPanel(QWidget* parent = nullptr);
	void append(const QString& text, const QColor& color = {});
};

} // namespace sme::app
