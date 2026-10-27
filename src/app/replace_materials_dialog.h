// replace_materials_dialog.h - Replace one material with another across the whole map or the
// selection (exact name or wildcards, keep the alignment or align to the world, also overlays and
// decals). Shows how many faces will change while typing; applying is one undo step.

#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;

namespace sme::app {

class Editor;

/// The Replace materials dialog (see the file comment).
class ReplaceMaterialsDialog : public QDialog {
	Q_OBJECT

public:
	/// `knownMaterials` feeds the completion of both fields (the map's own materials are always offered).
	ReplaceMaterialsDialog(Editor* editor, const QStringList& knownMaterials, QWidget* parent = nullptr);

	/// Fill in the fields (from menus and tests).
	void setFind(const QString& material);
	void setReplaceWith(const QString& material);
	void setOnlySelection(bool on);
	void setWildcards(bool on);
	/// How many faces and overlay keys the current settings would change.
	[[nodiscard]] int matchCount() const;
	/// Does the replacement (one undo step); returns how many faces and keys changed.
	int apply();

private:
	void updateCount();

	Editor* m_editor;
	QComboBox* m_find;
	QComboBox* m_replace;
	QCheckBox* m_wildcards;
	QCheckBox* m_keepAlignment;
	QCheckBox* m_overlays;
	QRadioButton* m_selectionOnly;
	QRadioButton* m_wholeMap;
	QLabel* m_count;
};

/// Opens the dialog (modal) and applies it when confirmed.
void showReplaceMaterialsDialog(Editor* editor, QWidget* parent, const QStringList& knownMaterials = {});

} // namespace sme::app
