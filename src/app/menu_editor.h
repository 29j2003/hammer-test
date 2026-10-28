// menu_editor.h - Edits what the mod shows in its main menu: title and logo (gameinfo.txt), the
// chapters of "New Game" (map, title, background), and the menu entries (GameMenu.res), with a
// live mock of the menu next to it. The file work lives in the core (sme/mod_menu.h); this is
// only the user interface on top of it.

#pragma once

#include <QImage>
#include <QWidget>

#include <optional>

#include <sme/mod_menu.h>

class QCheckBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace sme::app {

class Editor;

/// Draws a picture of the main menu as the game shows it (Source 2013 style): the background
/// map's screenshot (or a dark gradient), the title on the left and the entries below it.
class MenuPreview : public QWidget {
	Q_OBJECT

public:
	explicit MenuPreview(QWidget* parent = nullptr);

	/// What to draw. `background` may be null (a gradient is used then).
	void setContent(const QString& title, const QString& title2, const QStringList& entries, const QImage& background);
	/// Entries as shown right now (after "In game" filtering), for tests.
	[[nodiscard]] const QStringList& entries() const { return m_entries; }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QString m_title, m_title2;
	QStringList m_entries;
	QImage m_background;
};

/// The Menu editor: a scrolling column of sections on the left, the preview on the right.
class MenuEditor : public QWidget {
	Q_OBJECT

public:
	explicit MenuEditor(Editor* editor, QWidget* parent = nullptr);

	/// Reads the mod's menu files again (also done when the game changes).
	void reload();
	/// Takes the chapter titles from the localization file again (after the Texts tab saved it),
	/// keeping every other edit.
	void refreshTitles();
	/// Writes everything into the mod (with one-time .bak copies of files it replaces).
	/// Returns false and fills `error` when something could not be written.
	bool save(QString* error = nullptr);
	/// The menu as edited so far.
	[[nodiscard]] const ModMenu& menu() const { return m_menu; }
	[[nodiscard]] MenuPreview* preview() const { return m_preview; }
	/// Adds a chapter at the end (map and title to fill in), like the "+ Chapter" button.
	void addChapter();

signals:
	/// After a successful save (the paths written).
	void saved(const QStringList& files);

private:
	void buildInfoSection(QVBoxLayout* layout);
	void rebuildChapters();
	void rebuildItems();
	QWidget* makeChapterCard(int index);
	QWidget* makeItemRow(int index);
	void updatePreview();
	void setStatus(const QString& text, bool error = false);
	/// Text shown for a menu label: "#tokens" resolved through the mod's file or the game's own.
	[[nodiscard]] QString displayLabel(const std::string& label) const;
	/// Maps the mod has (compiled and sources), for the chapter and background pickers.
	[[nodiscard]] QStringList mapNames() const;

	Editor* m_editor;
	ModMenu m_menu;
	QWidget* m_infoBox = nullptr;
	QVBoxLayout* m_chapterList = nullptr;
	QVBoxLayout* m_itemList = nullptr;
	QLabel* m_iconPreview = nullptr;
	QLabel* m_status = nullptr;
	QCheckBox* m_inGame = nullptr;
	MenuPreview* m_preview = nullptr;
	std::map<std::string, QLineEdit*> m_infoFields;
	QCheckBox* m_logo = nullptr;
	bool m_loading = false;
	mutable std::optional<Localization> m_gameUiText;   ///< The game's own menu texts (read when first needed)
};

} // namespace sme::app
