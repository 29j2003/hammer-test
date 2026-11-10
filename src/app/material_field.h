// material_field.h - A material setting that is easy to fill: the name to type, a small picture
// of the material, a short list of fitting suggestions (▾) and a picker with the asset browser's
// list (search, thumbnails; "..."). MaterialPickerDialog is that picker on its own: any dialog
// that asks for a material can use it.

#pragma once

#include <QDialog>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListView;
class QToolButton;

namespace sme::app {

class AssetBrowser;
class Editor;
class MaterialFilter;

/// Picks one of the game's materials: search words (like the asset browser), thumbnails, double
/// click or OK takes it.
class MaterialPickerDialog : public QDialog {
	Q_OBJECT

public:
	MaterialPickerDialog(AssetBrowser* assets, const QString& current, const QString& title, QWidget* parent = nullptr);
	/// The chosen material in upper case ("CONCRETE/WALL01"), empty if none.
	[[nodiscard]] QString material() const;
	/// Search words (tests).
	void setSearch(const QString& words);

private:
	QLineEdit* m_search;
	QListView* m_list;
	MaterialFilter* m_filter;
};

/// The material field of a dialog: text, picture, suggestions and picker.
class MaterialField : public QWidget {
	Q_OBJECT

public:
	/// `name`: object name of the text field (the buttons get name + "Suggest" / "Browse").
	MaterialField(const QString& name, Editor* editor, QWidget* parent = nullptr);
	[[nodiscard]] QLineEdit* lineEdit() const { return m_text; }
	[[nodiscard]] QString text() const;
	void setText(const QString& text);
	void setPlaceholderText(const QString& text);
	/// Where the picture and the picker come from (nothing to browse without it).
	void setAssets(AssetBrowser* assets);
	/// The short list under ▾ (best first; upper-cased when taken).
	void setSuggestions(const QStringList& materials);
	[[nodiscard]] const QStringList& suggestions() const { return m_suggestions; }
	/// Opens the picker (the "..." button).
	void browse();
	/// Sets the material as the person's choice (a suggestion, the picker): textEdited follows.
	void choose(const QString& material);

signals:
	void textChanged(const QString& text);
	/// Typed by hand (not set from code).
	void textEdited(const QString& text);

private:
	void updatePicture();

	Editor* m_editor;
	AssetBrowser* m_assets = nullptr;
	QLineEdit* m_text;
	QLabel* m_picture;
	QToolButton* m_suggest;
	QToolButton* m_browse;
	QStringList m_suggestions;
};

} // namespace sme::app
