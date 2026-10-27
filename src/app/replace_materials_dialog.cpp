// replace_materials_dialog.cpp - Replace materials dialog (see replace_materials_dialog.h).

#include "replace_materials_dialog.h"

#include "editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <set>

#include <sme/material.h>
#include <sme/material_replace.h>

namespace sme::app {

namespace {

/// Editable material field with completion (matches anywhere in the name).
QComboBox* materialField(const QStringList& names, const QString& objectName) {
	auto* combo = new QComboBox;
	combo->setObjectName(objectName);
	combo->setEditable(true);
	combo->setInsertPolicy(QComboBox::NoInsert);
	combo->addItems(names);
	combo->setCurrentIndex(-1);
	combo->setMinimumWidth(320);
	auto* completer = new QCompleter(names, combo);
	completer->setCaseSensitivity(Qt::CaseInsensitive);
	completer->setFilterMode(Qt::MatchContains);
	combo->setCompleter(completer);
	return combo;
}

} // namespace

ReplaceMaterialsDialog::ReplaceMaterialsDialog(Editor* editor, const QStringList& knownMaterials, QWidget* parent)
	: QDialog(parent), m_editor(editor) {
	setObjectName("replaceMaterialsDialog");
	setWindowTitle(tr("Replace materials"));
	// The map's own materials first, then whatever else the caller knows.
	std::set<QString> used;
	for (const auto& solid : editor->doc().world.solids) {
		for (const auto& side : solid.sides) used.insert(QString::fromStdString(normalizeMaterialName(side.material)));
	}
	for (const auto& entity : editor->doc().entities) {
		for (const auto& solid : entity.solids) {
			for (const auto& side : solid.sides) used.insert(QString::fromStdString(normalizeMaterialName(side.material)));
		}
	}
	QStringList inMap(used.begin(), used.end());
	QStringList all = inMap;
	for (const auto& name : knownMaterials) {
		if (!used.count(name)) all << name;
	}

	auto* layout = new QVBoxLayout(this);
	auto* form = new QFormLayout;
	m_find = materialField(inMap, "replaceFind");
	m_replace = materialField(all, "replaceWith");
	auto* replaceRow = new QHBoxLayout;
	replaceRow->addWidget(m_replace, 1);
	auto* current = new QPushButton(tr("Current material"));
	current->setToolTip(tr("Take the material picked in the browser: %1").arg(editor->currentMaterial()));
	replaceRow->addWidget(current);
	form->addRow(tr("Find"), m_find);
	form->addRow(tr("Replace with"), replaceRow);
	layout->addLayout(form);

	auto* scope = new QHBoxLayout;
	m_wholeMap = new QRadioButton(tr("Whole map"));
	m_wholeMap->setObjectName("replaceWholeMap");
	m_selectionOnly = new QRadioButton(tr("Only the selection"));
	m_selectionOnly->setObjectName("replaceSelection");
	const bool hasSelection = !editor->selection().empty();
	m_selectionOnly->setEnabled(hasSelection);
	(hasSelection ? m_selectionOnly : m_wholeMap)->setChecked(true);
	scope->addWidget(m_wholeMap);
	scope->addWidget(m_selectionOnly);
	scope->addStretch(1);
	layout->addLayout(scope);
	m_wildcards = new QCheckBox(tr("Wildcards in Find (* and ?, e.g. concrete/*)"));
	m_keepAlignment = new QCheckBox(tr("Keep the texture alignment"));
	m_keepAlignment->setChecked(true);
	m_keepAlignment->setToolTip(tr("Off: the new material is aligned to the world at its normal size"));
	m_overlays = new QCheckBox(tr("Also overlays and decals"));
	m_overlays->setChecked(true);
	for (auto* box : {m_wildcards, m_keepAlignment, m_overlays}) layout->addWidget(box);
	m_count = new QLabel;
	m_count->setObjectName("replaceCount");
	layout->addWidget(m_count);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	auto* replace = buttons->addButton(tr("Replace"), QDialogButtonBox::AcceptRole);
	replace->setProperty("accent", true);
	layout->addWidget(buttons);

	connect(current, &QPushButton::clicked, this, [this] { setReplaceWith(m_editor->currentMaterial().toLower()); });
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	for (auto* combo : {m_find, m_replace}) connect(combo, &QComboBox::currentTextChanged, this, &ReplaceMaterialsDialog::updateCount);
	for (auto* box : {m_wildcards, m_overlays}) connect(box, &QCheckBox::toggled, this, &ReplaceMaterialsDialog::updateCount);
	connect(m_selectionOnly, &QRadioButton::toggled, this, &ReplaceMaterialsDialog::updateCount);
	// Start with the material of the selected faces, if any.
	if (!editor->selection().faces.empty()) {
		const auto& face = editor->selection().faces.front();
		if (const auto* solid = editor->doc().findSolid(face.solid)) {
			for (const auto& side : solid->sides) {
				if (side.id == face.sideId) setFind(QString::fromStdString(normalizeMaterialName(side.material)));
			}
		}
	}
	updateCount();
}

void ReplaceMaterialsDialog::setFind(const QString& material) {
	m_find->setEditText(material);
}

void ReplaceMaterialsDialog::setReplaceWith(const QString& material) {
	m_replace->setEditText(material);
}

void ReplaceMaterialsDialog::setOnlySelection(bool on) {
	(on ? m_selectionOnly : m_wholeMap)->setChecked(true);
}

void ReplaceMaterialsDialog::setWildcards(bool on) {
	m_wildcards->setChecked(on);
}

namespace {

MaterialReplaceOptions optionsOf(const QComboBox* find, const QComboBox* replace, const QCheckBox* wildcards, const QCheckBox* keep,
                                 const QCheckBox* overlays, const QRadioButton* selectionOnly) {
	MaterialReplaceOptions options;
	options.find = find->currentText().trimmed().toStdString();
	options.replaceWith = replace->currentText().trimmed().toStdString();
	options.wildcards = wildcards->isChecked();
	options.keepAlignment = keep->isChecked();
	options.overlaysAndDecals = overlays->isChecked();
	options.onlySelection = selectionOnly->isChecked();
	return options;
}

} // namespace

int ReplaceMaterialsDialog::matchCount() const {
	return countMaterialMatches(m_editor->doc(), m_editor->selection(),
	                            optionsOf(m_find, m_replace, m_wildcards, m_keepAlignment, m_overlays, m_selectionOnly));
}

void ReplaceMaterialsDialog::updateCount() {
	const int count = matchCount();
	m_count->setText(m_find->currentText().trimmed().isEmpty() ? tr("Type the material to look for.")
	                                                           : tr("%n face(s) or overlay(s) will change", nullptr, count));
}

int ReplaceMaterialsDialog::apply() {
	const auto options = optionsOf(m_find, m_replace, m_wildcards, m_keepAlignment, m_overlays, m_selectionOnly);
	const int count = countMaterialMatches(m_editor->doc(), m_editor->selection(), options);
	if (count == 0 || options.replaceWith.empty()) {
		return 0;
	}
	m_editor->commit(edit::replaceMaterials(m_editor->doc(), m_editor->selection(), options));
	emit m_editor->message(tr("Replaced %1 on %n face(s) or overlay(s)", nullptr, count).arg(QString::fromStdString(options.find)));
	return count;
}

void showReplaceMaterialsDialog(Editor* editor, QWidget* parent, const QStringList& knownMaterials) {
	ReplaceMaterialsDialog dialog(editor, knownMaterials, parent);
	if (dialog.exec() == QDialog::Accepted) {
		dialog.apply();
	}
}

} // namespace sme::app
