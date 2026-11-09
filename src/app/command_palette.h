// command_palette.h - Command search (Ctrl+K): type a few letters, Enter runs the entry. Lists every
// menu entry, tool and page of the editor (with where it lives and its key), and - from three
// letters on - the game's materials, models and entity classes (Enter picks them like a click in
// their browser). Ranking: sme/command_search.h.

#pragma once

#include <QDialog>
#include <QString>

#include <functional>
#include <vector>

class QLineEdit;
class QListWidget;
class QMenuBar;
class QWidget;

namespace sme::app {

struct Command {
	QString name;       ///< "Fill gap ..."
	QString where;      ///< "Brushes", "Tools", "Material"
	QString shortcut;   ///< "Ctrl+Shift+R" (may be empty)
	std::function<void()> run;
	bool enabled = true;
};

class CommandPalette : public QDialog {
	Q_OBJECT

public:
	/// `fixed`: entries always listed; `more`: entries for a query of three letters or more (assets).
	CommandPalette(std::vector<Command> fixed, std::function<std::vector<Command>(const QString&)> more, QWidget* parent = nullptr);

	/// Every action of the menu bar's menus (and their submenus), with the menu path as `where`.
	[[nodiscard]] static std::vector<Command> menuCommands(QMenuBar* bar);

	/// The names listed for the current query, best first (tests).
	[[nodiscard]] QStringList listed() const;
	void setQuery(const QString& query);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void update();
	void runCurrent();

	std::vector<Command> m_fixed;
	std::function<std::vector<Command>(const QString&)> m_more;
	std::vector<Command> m_shown;
	QLineEdit* m_query;
	QListWidget* m_list;
};

} // namespace sme::app
