// start_page.h - What Mapsmith shows when it opens (and File > Start page): continue with the
// last project in one click, pick one of the recent ones, or start a new one - a guided new
// Mapbase mod with the features installed and a first map ready to edit. Big, plain and
// friendly; "Show at startup" turns it off.

#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

#include <vector>

#include <sme/recent_projects.h>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace sme::app {

/// Settings key of the recent projects (text from writeRecentProjects).
inline constexpr const char* RECENT_PROJECTS_KEY = "start/recentProjects";

/// The recent projects from the settings, without maps that are gone.
[[nodiscard]] std::vector<RecentProject> loadRecentProjects();
/// Puts a map on top of the recent projects (the settings), with a picture of the 3D view when
/// one is given (shown on the start page).
void rememberRecentProject(const QString& gameKey, const QString& mapPath, const QImage& picture = {});
/// Where the picture of a project's map is kept.
[[nodiscard]] QString projectPicturePath(const QString& mapPath);

/// The start page (see the file comment).
class StartPage : public QWidget {
	Q_OBJECT

public:
	explicit StartPage(QWidget* parent = nullptr);

	/// Reads the recent projects again and fills the page.
	void refresh();
	/// Whether the page shows when Mapsmith opens (setting "start/show", on by default).
	[[nodiscard]] static bool showAtStartup();
	[[nodiscard]] const std::vector<RecentProject>& projects() const { return m_projects; }

signals:
	/// Continue / a recent project: open this map in its game.
	void openProject(const QString& gameKey, const QString& mapPath);
	/// "Start something new": the guided new project.
	void newProjectRequested();
	/// "Open a map ...".
	void openMapRequested();
	/// "Go to the editor" without opening anything.
	void closeRequested();

private:
	std::vector<RecentProject> m_projects;
	QPushButton* m_continue;
	QPushButton* m_startNew;     ///< The main button only while there is no project to continue
	QLabel* m_subtitle;          ///< Under the title: depends on whether there is a project to continue
	QLabel* m_continuePicture;
	QLabel* m_continueTitle;
	QLabel* m_continueDetail;
	QWidget* m_continueCard;
	QListWidget* m_recent;
	QLabel* m_recentTitle;
	QCheckBox* m_showAtStartup;
};

} // namespace sme::app
