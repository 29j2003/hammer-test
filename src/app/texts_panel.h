// texts_panel.h - Project › Texts: the mod's localization files (resource/<name>_<language>.txt)
// and its captions (closecaption_<language>.txt → .dat). One table per file set: a row per
// token, a column per language; missing translations are marked. Below the table the selected
// text is edited, with buttons for the caption tags and a preview of how a caption line looks.
// Saving writes UTF-16 files into the mod folder only; "Compile captions" writes the .dat the
// game reads. The file work lives in the core (sme/localization.h).

#pragma once

#include <QWidget>

#include <map>

#include <sme/localization.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace sme::app {

class AssetBrowser;
class Editor;

/// The Texts tab (see the file comment).
class TextsPanel : public QWidget {
	Q_OBJECT

public:
	TextsPanel(Editor* editor, AssetBrowser* assets, QWidget* parent = nullptr);

	/// Reads the mod's files again (unsaved edits are dropped).
	void reload();
	/// Reads again unless there are unsaved edits (after the Menu tab saved chapter titles).
	void reloadIfUnchanged();
	/// Shows a file set by name ("mymod", "closecaption"); false if the mod has none such.
	bool showSet(const QString& name);
	/// Languages of the shown set (lowercase, English first).
	[[nodiscard]] QStringList languages() const;
	/// Text of a token in a language of the shown set ("" if missing).
	[[nodiscard]] QString text(const QString& token, const QString& language = QStringLiteral("english")) const;
	/// Sets a text (adds the token if it is new). Marks the set as changed.
	void setText(const QString& token, const QString& language, const QString& text);
	/// Adds a language to the shown set (a new, empty file once saved).
	void addLanguage(const QString& language);
	/// Writes the shown set into the mod. False with a message in the status line on failure.
	bool save();
	/// Saves, then compiles every language of the shown caption set into its .dat.
	bool compileCaptions();
	[[nodiscard]] bool modified() const { return m_modified; }
	[[nodiscard]] QTableWidget* table() const { return m_table; }
	[[nodiscard]] QLabel* preview() const { return m_preview; }
	[[nodiscard]] QPlainTextEdit* editorField() const { return m_edit; }

signals:
	/// After a successful save (the set's name), so the Menu tab can pick up chapter titles.
	void saved(const QString& setName);

protected:
	void showEvent(QShowEvent* event) override;

private:
	void fillSetBox();
	void rebuildTable();
	void updateRowLook(int row);
	void updateEditor();
	void updatePreview();
	void applyFilter();
	void setModified(bool modified);
	void setStatus(const QString& text, bool error = false);
	void insertTag(const QString& before, const QString& after = {});
	void addToken();
	void renameToken();
	void deleteToken();
	void addCaptionForSound();
	/// Tokens of the shown set: the English file's order, then tokens only other languages have.
	[[nodiscard]] QStringList tokenOrder() const;
	[[nodiscard]] QString currentToken() const;
	[[nodiscard]] QString currentLanguage() const;
	[[nodiscard]] bool isCaptions() const;

	Editor* m_editor;
	AssetBrowser* m_assets;
	std::vector<LocalizationSet> m_sets;
	LocalizationSet m_set;                                 ///< The shown set
	std::map<std::string, LocalizationFile> m_files;       ///< lowercase language -> its file
	bool m_modified = false;
	bool m_reloadOnShow = false;   ///< The game changed while the tab was hidden: read it when shown
	bool m_filling = false;

	QComboBox* m_setBox;
	QLineEdit* m_search;
	QCheckBox* m_onlyMissing;
	QComboBox* m_addLanguage;
	QPushButton* m_save;
	QPushButton* m_compile;
	QPushButton* m_addCaption;
	QPushButton* m_create;
	QTableWidget* m_table;
	QLabel* m_editTitle;
	QPlainTextEdit* m_edit;
	QWidget* m_captionTools;
	QLabel* m_preview;
	QLabel* m_status;
};

} // namespace sme::app
