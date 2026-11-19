// chapters_panel.h - Project > Chapters: the chapters of New Game as cards - picture, title (per
// language), first map, other maps of the chapter, background map behind the menu and its loading
// picture; drag a card to reorder, Play starts the game at the chapter's map. A list of checks
// says what is missing. Saving and restoring is the core's (sme/chapters.h).

#pragma once

#include <QStringList>
#include <QWidget>

#include <sme/chapters.h>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;

namespace sme::app {

class Editor;

/// The Chapters tab (see the file comment).
class ChaptersPanel : public QWidget {
	Q_OBJECT

public:
	explicit ChaptersPanel(Editor* editor, QWidget* parent = nullptr);

	/// Reads the mod's chapters again (also done when the game changes).
	void reload();
	/// Writes everything into the mod. False (with `error`) when something could not be written.
	bool save(QString* error = nullptr);
	/// Puts the mod's chapter files back as they were before Mapsmith changed them.
	bool restore(QString* error = nullptr);

	/// The chapters as edited so far.
	[[nodiscard]] const ChapterSet& chapters() const { return m_set; }
	[[nodiscard]] bool modified() const { return m_modified; }
	/// Language the title fields show ("english", "german" ...).
	[[nodiscard]] QString language() const { return m_language; }
	void setLanguage(const QString& language);

	/// Editing, as the buttons on the cards do.
	void addChapter();
	void removeChapter(int index);
	void moveChapter(int from, int to);
	void setTitle(int index, const QString& title);
	void setMap(int index, const QString& map);
	void setMoreMaps(int index, const QString& maps);
	void setBackground(int index, const QString& map);
	/// A photo becomes the chapter's picture (cropped to New Game's frame).
	void setPicture(int index, const ImageRgba& photo);
	/// A photo becomes the loading picture of a background map (written on save).
	void setLoadingPicture(const QString& background, const ImageRgba& photo);

	/// The checks shown under the cards.
	[[nodiscard]] const std::vector<ChapterProblem>& problems() const { return m_problems; }
	/// Maps of the mod offered in the map fields (compiled maps and sources).
	[[nodiscard]] QStringList mapNames() const;

signals:
	/// After a successful save or restore (other tabs read the chapter files again).
	void saved();
	/// "Play": start the game at this map.
	void playRequested(const QString& map);

private:
	void rebuild();
	QWidget* makeCard(int index);
	void changed(bool rebuildCards = false);
	void updateChecks();
	void setStatus(const QString& text, bool error = false);
	[[nodiscard]] MapState mapState(const std::string& map) const;
	[[nodiscard]] bool hasLoadingPicture(const std::string& background) const;
	[[nodiscard]] QImage loadingPicture(const std::string& background) const;
	/// Asks for a picture file; null image when cancelled or unreadable (the reason in the status).
	[[nodiscard]] std::optional<ImageRgba> askPicture(const QString& title);

	Editor* m_editor;
	ChapterSet m_set;
	QString m_language = QStringLiteral("english");
	bool m_modified = false;
	std::vector<ChapterProblem> m_problems;
	QVBoxLayout* m_cards = nullptr;
	QComboBox* m_languageBox = nullptr;
	QListWidget* m_checks = nullptr;
	QLabel* m_status = nullptr;
	QPushButton* m_save = nullptr;
	QPushButton* m_restore = nullptr;
};

} // namespace sme::app
