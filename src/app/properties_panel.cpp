// properties_panel.cpp - Smart object properties (see properties_panel.h).

#include "properties_panel.h"

#include "asset_browser.h"
#include "editor.h"
#include "material_cache.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <set>

#include <sme/assets.h>
#include <sme/brush.h>
#include <sme/game.h>
#include <sme/history.h>
#include <sme/text.h>

namespace sme::app {

namespace {

const QStringList SPECIAL_TARGETS = {"!player", "!self", "!activator", "!caller", "!picker", "!pvsplayer"};

/// Keys that hold an animation sequence name (HL2/Mapbase entities).
bool isSequenceKey(const std::string& key) {
	static const std::set<std::string> keys = {"defaultanim", "m_iszidle", "m_iszplay", "m_iszentry", "m_iszpostidle", "m_iszcustommove",
	                                           "sequence", "m_iszsequence", "startanim", "idleanim"};
	return keys.contains(toLower(key));
}

QLabel* descriptionLabel(const QString& text) {
	auto* label = new QLabel(text);
	label->setWordWrap(true);
	label->setProperty("role", "description");
	return label;
}

QString qs(const std::string& s) {
	return QString::fromStdString(s);
}

QString studioPath(const std::string& helper) {
	QString path = qs(helper).trimmed();
	path.remove('"');
	return path;
}

} // namespace

QString shortDescription(const std::string& description, int maxLength) {
	QString text = qs(description).simplified();
	const auto end = text.indexOf(QRegularExpression("[.!?](\\s|$)"));
	if (end > 0 && end < maxLength) {
		return text.left(end + 1);
	}
	return text.length() > maxLength ? text.left(maxLength - 3) + "..." : text;
}

PropertiesPanel::PropertiesPanel(Editor* editor, AssetBrowser* assets, MaterialCache* materials, QWidget* parent)
	: QWidget(parent), m_editor(editor), m_assets(assets), m_materials(materials) {
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_filter = new QLineEdit;
	m_filter->setPlaceholderText(tr("Filter keys ..."));
	m_filter->setClearButtonEnabled(true);
	layout->addWidget(m_filter);
	m_scroll = new QScrollArea;
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	layout->addWidget(m_scroll, 1);
	connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
		m_filterText = text;
		scheduleRebuild();
	});
	connect(editor, &Editor::selectionChanged, this, &PropertiesPanel::scheduleRebuild);
	connect(editor, &Editor::documentChanged, this, &PropertiesPanel::scheduleRebuild);
	connect(editor, &Editor::gameChanged, this, &PropertiesPanel::scheduleRebuild);
	connect(editor, &Editor::toolChanged, this, &PropertiesPanel::scheduleRebuild);
	scheduleRebuild();
}

void PropertiesPanel::scheduleRebuild() {
	if (m_pending) {
		return;
	}
	m_pending = true;
	QTimer::singleShot(0, this, [this] {
		m_pending = false;
		rebuild();
	});
}

void PropertiesPanel::setKey(const std::vector<int>& ids, const std::string& key, const QString& value) {
	m_editor->commit(edit::setKeyValue(m_editor->doc(), ids, key, value.toStdString()));
}

QStringList PropertiesPanel::targetNames() const {
	QStringList names;
	std::set<QString> seen;
	for (const auto& entity : m_editor->doc().entities) {
		const QString name = qs(entity.get("targetname"));
		if (!name.isEmpty() && seen.insert(name).second) {
			names << name;
		}
	}
	names.sort(Qt::CaseInsensitive);
	return names;
}

QString PropertiesPanel::modelFor(const Entity& entity) const {
	QString model = qs(entity.get("model"));
	if (!model.isEmpty()) {
		return model;
	}
	// scripted_sequence & co.: the model of the NPC they control.
	for (const char* key : {"m_iszEntity", "target"}) {
		const auto target = entity.get(key);
		if (target.empty()) {
			continue;
		}
		for (const auto& other : m_editor->doc().entities) {
			if (other.id != entity.id && iequals(other.get("targetname"), target)) {
				return modelFor(other);
			}
		}
	}
	// NPCs without a model key use their class default (the FGD's studio() helper).
	if (const auto* catalog = m_editor->entities()) {
		if (const auto* cls = catalog->find(entity.classname())) {
			return studioPath(cls->helper("studio"));
		}
	}
	return {};
}

QStringList PropertiesPanel::sequencesFor(const Entity& entity) const {
	QStringList out;
	const QString model = modelFor(entity);
	if (model.isEmpty() || !m_editor->game()) {
		return out;
	}
	for (const auto& sequence : modelSequences(m_editor->game()->files, model.toStdString())) {
		out << qs(sequence);
	}
	return out;
}

void PropertiesPanel::rebuild() {
	auto* content = new QWidget;
	auto* layout = new QVBoxLayout(content);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);
	const auto& selection = m_editor->selection();

	if (!selection.faces.empty()) {
		buildFaces(layout);
	} else if (!selection.entities.empty()) {
		buildEntity(layout, selection.entities);
	} else if (!selection.solids.empty()) {
		buildBrushes(layout);
	} else {
		auto* title = new QLabel(tr("<b>Map</b> (nothing selected)"));
		layout->addWidget(title);
		const auto& doc = m_editor->doc();
		layout->addWidget(descriptionLabel(tr("%1 brushes, %2 entities. Below are the world settings (worldspawn).").arg(doc.solidCount()).arg(doc.entities.size())));
		buildEntity(layout, {0});
	}
	layout->addStretch(1);
	m_scroll->setWidget(content);   // deletes the previous content
	m_content = content;
}

QWidget* PropertiesPanel::makeEditor(const EntityProperty& property, const std::vector<int>& ids, const QString& value, bool mixed) {
	const std::string key = property.name;
	const auto commit = [this, ids, key](const QString& v) { setKey(ids, key, v); };
	const QString type = qs(property.type);

	if (property.kind == EntityProperty::Kind::Choices) {
		auto* combo = new QComboBox;
		combo->setEditable(true);
		int current = -1;
		for (const auto& choice : property.choices) {
			combo->addItem(QStringLiteral("%1  (%2)").arg(qs(choice.displayName), qs(choice.value)), qs(choice.value));
			if (qs(choice.value) == value) {
				current = combo->count() - 1;
			}
		}
		if (mixed) {
			combo->setCurrentIndex(-1);
			combo->setEditText(tr("(different)"));
		} else if (current >= 0) {
			combo->setCurrentIndex(current);
		} else {
			combo->setEditText(value);
		}
		connect(combo, &QComboBox::activated, this, [combo, commit](int index) { commit(combo->itemData(index).toString()); });
		connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [combo, commit] {
			if (combo->currentIndex() < 0 || combo->currentText() != combo->itemText(combo->currentIndex())) {
				commit(combo->currentText());
			}
		});
		return combo;
	}

	auto* row = new QWidget;
	auto* h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(4);

	const bool isTarget = type == "target_destination" || type == "target_name_or_class" || type == "filterclass" || type == "npcclass" ||
	                      toLower(key) == "m_iszentity" || toLower(key) == "target";
	const bool isSequence = isSequenceKey(key);
	const bool isSound = type == "sound";
	const bool isModel = type == "studio" || toLower(key) == "model";

	if (isTarget || isSequence || isSound || isModel) {
		auto* combo = new QComboBox;
		combo->setEditable(true);
		combo->setInsertPolicy(QComboBox::NoInsert);
		QStringList items;
		if (isTarget) {
			items = targetNames();
			if (type == "target_destination") {
				items << SPECIAL_TARGETS;
			}
		} else if (isSequence && ids.size() == 1) {
			if (const auto* entity = m_editor->doc().findEntity(ids.front())) {
				items = sequencesFor(*entity);
			}
		} else if (isSound) {
			items = m_assets->soundNames();
		} else if (isModel) {
			items = m_assets->modelNames();
		}
		combo->addItems(items);
		auto* completer = new QCompleter(items, combo);
		completer->setCaseSensitivity(Qt::CaseInsensitive);
		completer->setFilterMode(Qt::MatchContains);
		combo->setCompleter(completer);
		combo->setEditText(mixed ? QString() : value);
		if (mixed) {
			combo->lineEdit()->setPlaceholderText(tr("(different)"));
		}
		if (isTarget && !value.isEmpty() && !value.startsWith('!') && !value.contains('*') && !items.contains(value, Qt::CaseInsensitive)) {
			combo->setToolTip(tr("No entity is named \"%1\"").arg(value));
			combo->setProperty("problem", true);
		}
		if (isSequence && items.isEmpty()) {
			combo->lineEdit()->setPlaceholderText(tr("set a model first to list its animations"));
		}
		connect(combo, &QComboBox::activated, this, [combo, commit](int) { commit(combo->currentText()); });
		connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [combo, commit, value] {
			if (combo->currentText() != value) {
				commit(combo->currentText());
			}
		});
		h->addWidget(combo, 1);
		return row;
	}

	auto* edit = new QLineEdit(mixed ? QString() : value);
	if (mixed) {
		edit->setPlaceholderText(tr("(different)"));
	} else if (value.isEmpty() && !property.defaultValue.empty()) {
		edit->setPlaceholderText(qs(property.defaultValue));
	}
	h->addWidget(edit, 1);
	connect(edit, &QLineEdit::editingFinished, this, [edit, commit, value] {
		if (edit->text() != value) {
			commit(edit->text());
		}
	});

	if (type == "color255" || type == "color1") {
		auto* pick = new QToolButton;
		pick->setText(QStringLiteral("■"));
		const auto parts = value.split(' ', Qt::SkipEmptyParts);
		const double scale = type == "color1" ? 255.0 : 1.0;
		if (parts.size() >= 3) {
			const QColor c(std::clamp(int(parts[0].toDouble() * scale), 0, 255), std::clamp(int(parts[1].toDouble() * scale), 0, 255),
			               std::clamp(int(parts[2].toDouble() * scale), 0, 255));
			pick->setStyleSheet(QStringLiteral("color: %1; font-size: 16px;").arg(c.name()));
		}
		connect(pick, &QToolButton::clicked, this, [this, parts, scale, commit, type] {
			QColor initial(255, 255, 255);
			if (parts.size() >= 3) {
				initial = QColor(std::clamp(int(parts[0].toDouble() * scale), 0, 255), std::clamp(int(parts[1].toDouble() * scale), 0, 255),
				                 std::clamp(int(parts[2].toDouble() * scale), 0, 255));
			}
			const QColor c = QColorDialog::getColor(initial, this);
			if (!c.isValid()) {
				return;
			}
			QString text = type == "color1" ? QStringLiteral("%1 %2 %3").arg(c.redF(), 0, 'g', 3).arg(c.greenF(), 0, 'g', 3).arg(c.blueF(), 0, 'g', 3)
			                                 : QStringLiteral("%1 %2 %3").arg(c.red()).arg(c.green()).arg(c.blue());
			if (parts.size() >= 4) {
				text += ' ' + parts[3];   // keep the brightness
			}
			commit(text);
		});
		h->addWidget(pick);
	} else if (type == "material" || type == "decal" || type == "sprite") {
		auto* use = new QToolButton;
		use->setText(tr("Use current"));
		use->setToolTip(tr("Set the material selected in the material browser"));
		connect(use, &QToolButton::clicked, this, [this, commit] { commit(m_editor->currentMaterial().toLower()); });
		h->addWidget(use);
	} else if (type == "angle") {
		for (const int yaw : {0, 90, 180, 270}) {
			auto* button = new QToolButton;
			button->setText(QString::number(yaw));
			button->setToolTip(tr("Face %1°").arg(yaw));
			connect(button, &QToolButton::clicked, this, [commit, value, yaw] {
				auto parts = value.split(' ', Qt::SkipEmptyParts);
				while (parts.size() < 3) parts << "0";
				parts[1] = QString::number(yaw);
				commit(parts.join(' '));
			});
			h->addWidget(button);
		}
	}
	return row;
}

void PropertiesPanel::buildEntity(QVBoxLayout* layout, const std::vector<int>& entityIds) {
	const auto& doc = m_editor->doc();
	const auto* catalog = m_editor->entities();
	const Entity* first = doc.findEntity(entityIds.front());
	if (!first) {
		return;
	}
	const std::string classname = first->classname();
	const bool sameClass = std::all_of(entityIds.begin(), entityIds.end(), [&](int id) {
		const auto* e = doc.findEntity(id);
		return e && iequals(e->classname(), classname);
	});
	const EntityClass* definition = catalog ? catalog->find(classname) : nullptr;

	// Header: class with completion, description.
	if (entityIds.front() != 0) {
		auto* header = new QHBoxLayout;
		auto* title = new QLabel(entityIds.size() > 1 ? tr("<b>%1 entities</b>").arg(entityIds.size()) : tr("<b>Entity</b> (id %1)").arg(first->id));
		header->addWidget(title);
		auto* classCombo = new QComboBox;
		classCombo->setEditable(true);
		QStringList classes;
		if (catalog) {
			for (const auto* cls : catalog->placeable()) {
				if (cls->isSolid() == !first->solids.empty()) {
					classes << qs(cls->name);
				}
			}
		}
		classCombo->addItems(classes);
		auto* completer = new QCompleter(classes, classCombo);
		completer->setCaseSensitivity(Qt::CaseInsensitive);
		completer->setFilterMode(Qt::MatchContains);
		classCombo->setCompleter(completer);
		classCombo->setEditText(sameClass ? qs(classname) : tr("(different classes)"));
		connect(classCombo, &QComboBox::activated, this, [this, classCombo, entityIds] { setKey(entityIds, "classname", classCombo->currentText()); });
		header->addWidget(classCombo, 1);
		layout->addLayout(header);
	}
	if (!sameClass) {
		layout->addWidget(descriptionLabel(tr("Select entities of one class to edit their keys together.")));
		return;
	}
	if (definition && !definition->description.empty()) {
		auto* desc = descriptionLabel(qs(definition->description).simplified());
		desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
		layout->addWidget(desc);
	} else if (!definition && catalog) {
		auto* warn = new QLabel(tr("Class \"%1\" is not in the loaded FGDs - only raw keys can be edited.").arg(qs(classname)));
		warn->setProperty("role", "warning");
		warn->setWordWrap(true);
		layout->addWidget(warn);
	}

	// NPC helper first: posing NPCs is the common job.
	if (entityIds.size() == 1 && entityIds.front() != 0) {
		buildNpcHelper(layout, *first, definition);
	}

	// Smart keyvalue editors.
	const auto valueOf = [&](const std::string& key, bool& mixed) {
		const QString v = qs(first->get(key));
		mixed = false;
		for (const int id : entityIds) {
			if (const auto* e = doc.findEntity(id); e && qs(e->get(key)) != v) {
				mixed = true;
			}
		}
		return v;
	};
	auto* keysBox = new QGroupBox(tr("Properties"));
	auto* keys = new QVBoxLayout(keysBox);
	keys->setSpacing(2);
	std::set<std::string> known = {"classname", "id"};
	if (definition) {
		for (const auto& property : definition->properties) {
			known.insert(toLower(property.name));
			if (property.kind == EntityProperty::Kind::Flags) {
				continue;
			}
			const QString display = property.displayName.empty() ? qs(property.name) : qs(property.displayName);
			if (!m_filterText.isEmpty() && !display.contains(m_filterText, Qt::CaseInsensitive) && !qs(property.name).contains(m_filterText, Qt::CaseInsensitive)) {
				continue;
			}
			bool mixed = false;
			const QString value = valueOf(property.name, mixed);
			auto* label = new QLabel(display);
			label->setProperty("role", "key");
			label->setToolTip(QStringLiteral("<b>%1</b> (%2)<br>%3").arg(qs(property.name), qs(property.type), qs(property.description).toHtmlEscaped()));
			keys->addWidget(label);
			auto* editor = makeEditor(property, entityIds, value, mixed);
			editor->setToolTip(label->toolTip());
			keys->addWidget(editor);
			if (!property.description.empty()) {
				keys->addWidget(descriptionLabel(shortDescription(property.description)));
			}
			keys->addSpacing(4);
		}
	}
	// Keys the FGD does not know.
	bool anyOther = false;
	for (const auto& [key, value] : first->keyValues) {
		if (known.contains(toLower(key)) || iequals(key, "origin") || iequals(key, "angles")) {
			continue;
		}
		if (!m_filterText.isEmpty() && !qs(key).contains(m_filterText, Qt::CaseInsensitive)) {
			continue;
		}
		if (!anyOther) {
			keys->addWidget(new QLabel(tr("<i>Other keys</i>")));
			anyOther = true;
		}
		auto* row = new QHBoxLayout;
		auto* keyLabel = new QLabel(qs(key));
		keyLabel->setMinimumWidth(110);
		auto* edit = new QLineEdit(qs(value));
		auto* remove = new QToolButton;
		remove->setText(QStringLiteral("✕"));
		remove->setToolTip(tr("Remove this key"));
		const std::string k = key;
		connect(edit, &QLineEdit::editingFinished, this, [this, edit, entityIds, k, value] {
			if (edit->text().toStdString() != value) {
				setKey(entityIds, k, edit->text());
			}
		});
		connect(remove, &QToolButton::clicked, this, [this, entityIds, k] { m_editor->commit(edit::setKeyValue(m_editor->doc(), entityIds, k, std::nullopt)); });
		row->addWidget(keyLabel);
		row->addWidget(edit, 1);
		row->addWidget(remove);
		keys->addLayout(row);
	}
	// Position and rotation of point entities.
	if (first->solids.empty() && entityIds.front() != 0) {
		for (const char* key : {"origin", "angles"}) {
			if (known.contains(key)) {
				continue;
			}
			auto* row = new QHBoxLayout;
			auto* keyLabel = new QLabel(key == std::string("origin") ? tr("Position") : tr("Rotation (pitch yaw roll)"));
			keyLabel->setMinimumWidth(110);
			bool mixed = false;
			const QString value = valueOf(key, mixed);
			auto* edit = new QLineEdit(mixed ? QString() : value);
			const std::string k = key;
			connect(edit, &QLineEdit::editingFinished, this, [this, edit, entityIds, k, value] {
				if (edit->text() != value) {
					setKey(entityIds, k, edit->text());
				}
			});
			row->addWidget(keyLabel);
			row->addWidget(edit, 1);
			keys->addLayout(row);
		}
	}
	auto* addKey = new QPushButton(tr("Add key ..."));
	connect(addKey, &QPushButton::clicked, this, [this, entityIds] {
		bool ok = false;
		const QString key = QInputDialog::getText(this, tr("Add key"), tr("Key name:"), QLineEdit::Normal, {}, &ok);
		if (ok && !key.trimmed().isEmpty()) {
			const QString value = QInputDialog::getText(this, tr("Add key"), tr("Value for %1:").arg(key), QLineEdit::Normal, {}, &ok);
			if (ok) {
				setKey(entityIds, key.trimmed().toStdString(), value);
			}
		}
	});
	keys->addWidget(addKey);
	layout->addWidget(keysBox);

	// Flags as checkboxes with their names.
	if (definition) {
		for (const auto& property : definition->properties) {
			if (property.kind != EntityProperty::Kind::Flags || property.flags.empty()) {
				continue;
			}
			auto* box = new QGroupBox(property.displayName.empty() ? tr("Flags") : qs(property.displayName));
			auto* v = new QVBoxLayout(box);
			v->setSpacing(1);
			bool mixed = false;
			const auto current = valueOf(property.name, mixed).toULongLong();
			for (const auto& flag : property.flags) {
				auto* check = new QCheckBox(qs(flag.displayName));
				check->setChecked(first->get(property.name).empty() ? flag.enabledByDefault : (current & flag.value) != 0);
				check->setToolTip(tr("Flag value %1").arg(flag.value));
				const std::string key = property.name;
				const auto value = flag.value;
				connect(check, &QCheckBox::toggled, this, [this, entityIds, key, value](bool on) {
					// Toggle the bit on every selected entity, keeping their other bits.
					Transaction t(m_editor->doc(), "Edit flags");
					for (const int id : entityIds) {
						if (auto* e = m_editor->doc().findEntity(id)) {
							t.touchEntity(id);
							auto bits = std::stoull(e->get(key, "0").empty() ? "0" : e->get(key, "0"));
							bits = on ? (bits | value) : (bits & ~value);
							e->set(key, std::to_string(bits));
						}
					}
					m_editor->commit(t.finish());
				});
				v->addWidget(check);
			}
			layout->addWidget(box);
		}
	}

	if (entityIds.size() == 1 && entityIds.front() != 0) {
		buildOutputs(layout, entityIds.front());
		buildIncoming(layout, *first);
	}
}

void PropertiesPanel::buildNpcHelper(QVBoxLayout* layout, const Entity& entity, const EntityClass* definition) {
	const std::string classname = entity.classname();
	const bool isNpc = startsWithI(classname, "npc_") || startsWithI(classname, "monster_") || iequals(classname, "generic_actor") ||
	                   iequals(classname, "cycler");
	const bool isProp = startsWithI(classname, "prop_dynamic");
	if (!isNpc && !isProp) {
		return;
	}
	const QStringList sequences = sequencesFor(entity);
	auto* box = new QGroupBox(isNpc ? tr("Animation / pose") : tr("Animation"));
	auto* v = new QVBoxLayout(box);
	const QString model = modelFor(entity);
	v->addWidget(descriptionLabel(model.isEmpty() ? tr("No model known for this entity - set \"model\" to list its animations.")
	                                              : tr("Model: %1 - %2 animations").arg(model).arg(sequences.size())));
	auto* combo = new QComboBox;
	combo->setEditable(true);
	combo->addItems(sequences);
	auto* completer = new QCompleter(sequences, combo);
	completer->setCaseSensitivity(Qt::CaseInsensitive);
	completer->setFilterMode(Qt::MatchContains);
	combo->setCompleter(completer);
	combo->lineEdit()->setPlaceholderText(tr("Search an animation (idle, sit, lean, ...)"));
	if (isProp) {
		combo->setEditText(qs(entity.get("DefaultAnim")));
	}
	v->addWidget(combo);

	const int entityId = entity.id;
	if (isProp) {
		auto* apply = new QPushButton(tr("Play this animation (DefaultAnim)"));
		connect(apply, &QPushButton::clicked, this, [this, combo, entityId] { setKey({entityId}, "DefaultAnim", combo->currentText()); });
		v->addWidget(apply);
		layout->addWidget(box);
		return;
	}

	auto* gag = new QCheckBox(tr("Keep quiet (no idle speech)"));
	gag->setChecked(true);
	auto* noDrop = new QCheckBox(tr("Don't drop weapons"));
	noDrop->setChecked(true);
	v->addWidget(gag);
	v->addWidget(noDrop);

	auto* pose = new QPushButton(tr("Pose NPC in this animation"));
	pose->setToolTip(tr("Adds a scripted_sequence that keeps this NPC in the chosen idle animation on the spot.\n"
	                    "The NPC stays a real NPC (can react when the script is cancelled)."));
	auto* toProp = new QPushButton(tr("Turn into a static prop_dynamic"));
	toProp->setToolTip(tr("Cheapest option: the NPC becomes a prop_dynamic with this model playing the animation.\nNo AI, no sounds, no reactions."));
	v->addWidget(pose);
	v->addWidget(toProp);
	v->addWidget(descriptionLabel(tr("Pose = real NPC, idle loop via scripted_sequence. Static prop = only the animated model.")));
	layout->addWidget(box);

	// Sets flags by their FGD display name, so the right bits are used in every game.
	const auto setFlagsByName = [](Entity& e, const EntityClass* cls, const std::vector<std::pair<QString, bool>>& wanted) {
		if (!cls) {
			return;
		}
		for (const auto& property : cls->properties) {
			if (property.kind != EntityProperty::Kind::Flags) {
				continue;
			}
			auto bits = std::stoull(e.get(property.name, "0").empty() ? "0" : e.get(property.name, "0"));
			for (const auto& flag : property.flags) {
				for (const auto& [name, on] : wanted) {
					if (qs(flag.displayName).contains(name, Qt::CaseInsensitive)) {
						bits = on ? (bits | flag.value) : (bits & ~flag.value);
					}
				}
			}
			e.set(property.name, std::to_string(bits));
		}
	};

	connect(pose, &QPushButton::clicked, this, [this, combo, gag, noDrop, entityId, definition, setFlagsByName] {
		const QString sequence = combo->currentText().trimmed();
		if (sequence.isEmpty()) {
			emit m_editor->message(tr("Choose an animation first"));
			return;
		}
		auto& doc = m_editor->doc();
		Transaction t(doc, "Pose NPC");
		auto* npc = doc.findEntity(entityId);
		if (!npc) {
			return;
		}
		t.touchEntity(entityId);
		std::string name = npc->get("targetname");
		if (name.empty()) {
			name = npc->classname() + "_" + std::to_string(npc->id);
			npc->set("targetname", name);
		}
		setFlagsByName(*npc, definition, {{"Gag", gag->isChecked()}, {"drop weapon", noDrop->isChecked()}});
		const auto* scriptClass = m_editor->entities() ? m_editor->entities()->find("scripted_sequence") : nullptr;
		Entity script;
		script.id = doc.newEntityId();
		t.touchEntity(script.id);
		script.set("classname", "scripted_sequence");
		script.set("targetname", name + "_pose");
		script.set("m_iszEntity", name);
		script.set("m_iszIdle", sequence.toStdString());
		script.set("m_fMoveTo", "0");
		script.set("onplayerdeath", "0");
		script.setOrigin(npc->origin());
		script.set("angles", npc->get("angles", "0 0 0"));
		script.set("spawnflags", "0");
		setFlagsByName(script, scriptClass, {{"No Interruptions", true}, {"Override AI", true}, {"Loop in Post Idle", true}});
		doc.entities.push_back(std::move(script));
		m_editor->commit(t.finish());
		emit m_editor->message(tr("%1 now idles in \"%2\" (scripted_sequence %3_pose)").arg(qs(name), sequence, qs(name)));
	});

	connect(toProp, &QPushButton::clicked, this, [this, combo, entityId] {
		auto& doc = m_editor->doc();
		auto* npc = doc.findEntity(entityId);
		if (!npc) {
			return;
		}
		const QString model = modelFor(*npc);
		if (model.isEmpty()) {
			emit m_editor->message(tr("This NPC has no known model"));
			return;
		}
		Transaction t(doc, "NPC to prop_dynamic");
		t.touchEntity(entityId);
		Entity prop;
		prop.id = npc->id;
		prop.extra = npc->extra;
		prop.set("classname", "prop_dynamic");
		if (const auto name = npc->get("targetname"); !name.empty()) {
			prop.set("targetname", name);
		}
		prop.set("model", model.toStdString());
		prop.set("DefaultAnim", combo->currentText().toStdString());
		prop.set("solid", "6");
		prop.set("angles", npc->get("angles", "0 0 0"));
		prop.set("skin", npc->get("skin", "0"));
		prop.setOrigin(npc->origin());
		*npc = std::move(prop);
		m_editor->commit(t.finish());
	});
}

void PropertiesPanel::buildOutputs(QVBoxLayout* layout, int entityId) {
	const auto* entity = m_editor->doc().findEntity(entityId);
	const auto* catalog = m_editor->entities();
	const EntityClass* definition = catalog ? catalog->find(entity->classname()) : nullptr;
	auto* box = new QGroupBox(tr("Outputs (%1)").arg(entity->connections.size()));
	auto* v = new QVBoxLayout(box);
	v->setSpacing(6);

	const QStringList targets = targetNames() + SPECIAL_TARGETS;
	auto connections = std::make_shared<std::vector<Connection>>(entity->connections);
	const auto commit = [this, entityId, connections] { m_editor->commit(edit::setConnections(m_editor->doc(), entityId, *connections)); };

	const auto inputsFor = [this, catalog](const std::string& target) {
		QStringList inputs;
		std::set<QString> seen;
		for (const auto& e : m_editor->doc().entities) {
			if (!iequals(e.get("targetname"), target) && !iequals(e.classname(), target)) {
				continue;
			}
			if (const auto* cls = catalog ? catalog->find(e.classname()) : nullptr) {
				for (const auto& io : cls->inputs) {
					if (seen.insert(qs(io.name)).second) {
						inputs << qs(io.name);
					}
				}
			}
		}
		return inputs;
	};
	const auto editable = [](const QStringList& items, const QString& text) {
		auto* combo = new QComboBox;
		combo->setEditable(true);
		combo->setInsertPolicy(QComboBox::NoInsert);
		combo->addItems(items);
		auto* completer = new QCompleter(items, combo);
		completer->setCaseSensitivity(Qt::CaseInsensitive);
		completer->setFilterMode(Qt::MatchContains);
		combo->setCompleter(completer);
		combo->setEditText(text);
		combo->setMinimumContentsLength(8);
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		return combo;
	};

	QStringList outputNames;
	if (definition) {
		for (const auto& io : definition->outputs) {
			outputNames << qs(io.name);
		}
	}

	// One card per output: "When <output>  ->  <target>.<input>", then parameter, delay, once.
	for (std::size_t index = 0; index < connections->size(); ++index) {
		const auto& c = (*connections)[index];
		auto* card = new QFrame;
		card->setFrameShape(QFrame::StyledPanel);
		card->setProperty("role", "card");
		auto* grid = new QGridLayout(card);
		grid->setContentsMargins(6, 6, 6, 6);
		grid->setHorizontalSpacing(4);
		grid->setVerticalSpacing(4);

		auto* output = editable(outputNames, qs(c.output));
		auto* target = editable(targets, qs(c.target));
		auto* input = editable(inputsFor(c.target), qs(c.input));
		if (!c.target.empty() && !c.target.starts_with('!') && c.target.find('*') == std::string::npos && !targets.contains(qs(c.target), Qt::CaseInsensitive)) {
			target->setProperty("problem", true);
			target->setToolTip(tr("No entity is named \"%1\"").arg(qs(c.target)));
		}
		if (definition) {
			for (const auto& io : definition->outputs) {
				if (iequals(io.name, c.output)) output->setToolTip(qs(io.description));
			}
		}
		auto* parameter = new QLineEdit(qs(c.parameter));
		parameter->setPlaceholderText(tr("parameter"));
		auto* delay = new QDoubleSpinBox;
		delay->setRange(0, 100000);
		delay->setDecimals(2);
		delay->setSuffix(tr(" s"));
		delay->setValue(c.delay);
		auto* once = new QCheckBox(tr("once"));
		once->setChecked(c.timesToFire == 1);
		auto* remove = new QToolButton;
		remove->setText(QStringLiteral("✕"));
		remove->setToolTip(tr("Remove this output"));

		grid->addWidget(new QLabel(tr("When")), 0, 0);
		grid->addWidget(output, 0, 1, 1, 3);
		grid->addWidget(remove, 0, 4);
		grid->addWidget(new QLabel(tr("tell")), 1, 0);
		grid->addWidget(target, 1, 1);
		grid->addWidget(new QLabel(tr("to")), 1, 2);
		grid->addWidget(input, 1, 3, 1, 2);
		grid->addWidget(new QLabel(tr("with")), 2, 0);
		grid->addWidget(parameter, 2, 1);
		grid->addWidget(delay, 2, 2, 1, 2);
		grid->addWidget(once, 2, 4);
		grid->setColumnStretch(1, 1);
		grid->setColumnStretch(3, 1);
		v->addWidget(card);

		connect(output, &QComboBox::currentTextChanged, this, [connections, index](const QString& t) { (*connections)[index].output = t.toStdString(); });
		connect(output->lineEdit(), &QLineEdit::editingFinished, this, commit);
		connect(output, &QComboBox::activated, this, commit);
		connect(target, &QComboBox::currentTextChanged, this, [connections, index, input, inputsFor](const QString& t) {
			(*connections)[index].target = t.toStdString();
			const QString current = input->currentText();
			input->clear();
			input->addItems(inputsFor(t.toStdString()));
			input->setEditText(current);
		});
		connect(target->lineEdit(), &QLineEdit::editingFinished, this, commit);
		connect(target, &QComboBox::activated, this, commit);
		connect(input, &QComboBox::currentTextChanged, this, [connections, index](const QString& t) { (*connections)[index].input = t.toStdString(); });
		connect(input->lineEdit(), &QLineEdit::editingFinished, this, commit);
		connect(input, &QComboBox::activated, this, commit);
		connect(parameter, &QLineEdit::editingFinished, this, [connections, index, parameter, commit] {
			(*connections)[index].parameter = parameter->text().toStdString();
			commit();
		});
		connect(delay, &QDoubleSpinBox::editingFinished, this, [connections, index, delay, commit] {
			(*connections)[index].delay = delay->value();
			commit();
		});
		connect(once, &QCheckBox::toggled, this, [connections, index, commit](bool on) {
			(*connections)[index].timesToFire = on ? 1 : -1;
			commit();
		});
		connect(remove, &QToolButton::clicked, this, [connections, index, commit] {
			connections->erase(connections->begin() + static_cast<std::ptrdiff_t>(index));
			commit();
		});
	}

	auto* add = new QPushButton(tr("+ Add output"));
	connect(add, &QPushButton::clicked, this, [connections, definition, commit] {
		Connection c;
		c.output = definition && !definition->outputs.empty() ? definition->outputs.front().name : "OnUser1";
		connections->push_back(c);
		commit();
	});
	v->addWidget(add);
	layout->addWidget(box);
}

void PropertiesPanel::buildIncoming(QVBoxLayout* layout, const Entity& entity) {
	const auto name = entity.get("targetname");
	if (name.empty()) {
		return;
	}
	QStringList lines;
	for (const auto& other : m_editor->doc().entities) {
		for (const auto& c : other.connections) {
			if (iequals(c.target, name)) {
				const auto otherName = other.get("targetname");
				lines << QStringLiteral("%1 <b>%2</b> → %3%4")
				             .arg(qs(otherName.empty() ? other.classname() : otherName), qs(c.output), qs(c.input),
				                  c.parameter.empty() ? QString() : QStringLiteral(" (%1)").arg(qs(c.parameter)));
			}
		}
	}
	if (lines.isEmpty()) {
		return;
	}
	auto* box = new QGroupBox(tr("Inputs from other entities (%1)").arg(lines.size()));
	auto* v = new QVBoxLayout(box);
	auto* label = new QLabel(lines.join("<br>"));
	label->setWordWrap(true);
	v->addWidget(label);
	layout->addWidget(box);
}

void PropertiesPanel::buildFaces(QVBoxLayout* layout) {
	auto& doc = m_editor->doc();
	const auto faces = m_editor->selection().faces;
	const Side* first = nullptr;
	for (const auto& face : faces) {
		if (const auto* solid = doc.findSolid(face.solid)) {
			for (const auto& side : solid->sides) {
				if (side.id == face.sideId) {
					first = &side;
				}
			}
		}
		if (first) {
			break;
		}
	}
	if (!first) {
		return;
	}
	layout->addWidget(new QLabel(tr("<b>%1 face(s)</b>").arg(faces.size())));

	auto* materialRow = new QHBoxLayout;
	auto* material = new QLineEdit(qs(first->material));
	auto* useCurrent = new QPushButton(tr("Apply current"));
	useCurrent->setToolTip(tr("Apply the material selected in the browser (Shift+T)"));
	materialRow->addWidget(material, 1);
	materialRow->addWidget(useCurrent);
	layout->addWidget(new QLabel(tr("Material")));
	layout->addLayout(materialRow);
	auto* selectRow = new QHBoxLayout;
	auto* wall = new QPushButton(tr("Whole wall"));
	wall->setToolTip(tr("Add all touching faces in the same plane (or double-click a face)"));
	auto* same = new QPushButton(tr("All with this material"));
	same->setToolTip(tr("Select every face in the map that uses this material"));
	selectRow->addWidget(wall);
	selectRow->addWidget(same);
	layout->addLayout(selectRow);
	connect(wall, &QPushButton::clicked, this, [this, faces] {
		Selection selection = m_editor->selection();
		for (const auto& seed : faces) {
			for (const auto& face : edit::coplanarConnectedFaces(m_editor->doc(), seed)) {
				if (!selection.contains(face)) selection.faces.push_back(face);
			}
		}
		m_editor->setSelection(std::move(selection));
	});
	const std::string firstMaterial = first->material;
	connect(same, &QPushButton::clicked, this, [this, firstMaterial] {
		Selection selection;
		selection.faces = edit::facesWithMaterial(m_editor->doc(), firstMaterial);
		m_editor->setSelection(std::move(selection));
	});
	connect(material, &QLineEdit::editingFinished, this, [this, material] { m_editor->applyMaterialToSelection(material->text().toUpper()); });
	connect(useCurrent, &QPushButton::clicked, this, [this] { m_editor->applyMaterialToSelection(m_editor->currentMaterial()); });

	auto* form = new QFormLayout;
	const auto spin = [](double value, double min, double max, double step, int decimals) {
		auto* s = new QDoubleSpinBox;
		s->setRange(min, max);
		s->setDecimals(decimals);
		s->setSingleStep(step);
		s->setValue(value);
		s->setKeyboardTracking(false);
		return s;
	};
	auto* scaleU = spin(first->u.scale, -64, 64, 0.05, 4);
	auto* scaleV = spin(first->v.scale, -64, 64, 0.05, 4);
	auto* shiftU = spin(first->u.shift, -16384, 16384, 1, 2);
	auto* shiftV = spin(first->v.shift, -16384, 16384, 1, 2);
	auto* rotation = spin(first->rotation, -360, 360, 15, 2);
	auto* lightmap = new QSpinBox;
	lightmap->setRange(1, 128);
	lightmap->setValue(first->lightmapScale);
	lightmap->setKeyboardTracking(false);
	form->addRow(tr("Scale U / V"), [&] {
		auto* w = new QWidget;
		auto* h = new QHBoxLayout(w);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(scaleU);
		h->addWidget(scaleV);
		return w;
	}());
	form->addRow(tr("Shift U / V"), [&] {
		auto* w = new QWidget;
		auto* h = new QHBoxLayout(w);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(shiftU);
		h->addWidget(shiftV);
		return w;
	}());
	form->addRow(tr("Rotation"), rotation);
	form->addRow(tr("Lightmap scale"), lightmap);
	layout->addLayout(form);
	layout->addWidget(descriptionLabel(tr("Lightmap scale: smaller = sharper shadows but bigger map (16 is default, 32-64 for big plain walls).")));

	const auto modify = [this, faces](const char* name, auto fn) { m_editor->commit(edit::modifyFaces(m_editor->doc(), faces, name, fn)); };
	connect(scaleU, &QDoubleSpinBox::valueChanged, this, [modify](double v) { if (v != 0) modify("Texture scale", [v](Side& s) { s.u.scale = v; }); });
	connect(scaleV, &QDoubleSpinBox::valueChanged, this, [modify](double v) { if (v != 0) modify("Texture scale", [v](Side& s) { s.v.scale = v; }); });
	connect(shiftU, &QDoubleSpinBox::valueChanged, this, [modify](double v) { modify("Texture shift", [v](Side& s) { s.u.shift = v; }); });
	connect(shiftV, &QDoubleSpinBox::valueChanged, this, [modify](double v) { modify("Texture shift", [v](Side& s) { s.v.shift = v; }); });
	connect(lightmap, &QSpinBox::valueChanged, this, [modify](int v) { modify("Lightmap scale", [v](Side& s) { s.lightmapScale = v; }); });
	connect(rotation, &QDoubleSpinBox::valueChanged, this, [modify](double v) {
		modify("Texture rotation", [v](Side& s) {
			// Rotate the texture axes around the face normal by the change in rotation.
			const double delta = v - s.rotation;
			const Vec3 n = s.plane().normal;
			const double r = delta * M_PI / 180.0, c = std::cos(r), sn = std::sin(r);
			for (TextureAxis* axis : {&s.u, &s.v}) {
				const Vec3 a = axis->axis;
				axis->axis = a * c + cross(n, a) * sn + n * dot(n, a) * (1 - c);
			}
			s.rotation = v;
		});
	});

	auto* buttons = new QGridLayout;
	const auto addButton = [&](const QString& text, const QString& tip, int r, int c, auto fn) {
		auto* b = new QPushButton(text);
		b->setToolTip(tip);
		connect(b, &QPushButton::clicked, this, fn);
		buttons->addWidget(b, r, c);
	};
	addButton(tr("World align"), tr("Project the texture along the world axes (seamless across faces)"), 0, 0, [modify] {
		modify("World align", [](Side& s) { alignToWorld(s, s.u.scale != 0 ? std::abs(s.u.scale) : 0.25); });
	});
	addButton(tr("Face align"), tr("Lay the texture flat on each face"), 0, 1, [modify] {
		modify("Face align", [](Side& s) { alignToFace(s, s.u.scale != 0 ? std::abs(s.u.scale) : 0.25); });
	});
	addButton(tr("Fit"), tr("Stretch the texture exactly once over each face"), 1, 0, [this, faces] {
		auto& d = m_editor->doc();
		Transaction t(d, "Fit texture");
		for (const auto& face : faces) {
			t.touchSolid(face.solid);
			auto* solid = d.findSolid(face.solid);
			if (!solid) continue;
			const auto geometry = buildBrushGeometry(*solid);
			for (auto& side : solid->sides) {
				if (side.id != face.sideId) continue;
				QSize size(512, 512);
				if (const auto info = m_materials->get(qs(side.material)); info && !info->image.isNull()) {
					size = info->image.size();
				}
				for (const auto& f : geometry.faces) {
					if (solid->sides[static_cast<std::size_t>(f.sideIndex)].id != side.id) continue;
					double minU = 1e30, maxU = -1e30, minV = 1e30, maxV = -1e30;
					for (const auto& p : f.vertices) {
						minU = std::min(minU, dot(p, side.u.axis));
						maxU = std::max(maxU, dot(p, side.u.axis));
						minV = std::min(minV, dot(p, side.v.axis));
						maxV = std::max(maxV, dot(p, side.v.axis));
					}
					side.u.scale = (maxU - minU) / size.width();
					side.v.scale = (maxV - minV) / size.height();
					side.u.shift = -minU / side.u.scale;
					side.v.shift = -minV / side.v.scale;
				}
			}
		}
		m_editor->commit(t.finish());
	});
	addButton(tr("Reset"), tr("World align with scale 0.25, no shift"), 1, 1, [modify] { modify("Reset texture", [](Side& s) { alignToWorld(s, 0.25); }); });
	layout->addLayout(buttons);
	layout->addWidget(descriptionLabel(tr("Fast texturing: Ctrl+right mouse paints the current material over faces. Double-click a face selects the "
	                                      "whole wall. Shift+right click (or Ctrl+M) picks a material. Drag materials from the browser onto faces.")));
}

void PropertiesPanel::buildBrushes(QVBoxLayout* layout) {
	const auto count = m_editor->selection().solids.size();
	layout->addWidget(new QLabel(tr("<b>%1 world brush(es)</b>").arg(count)));
	auto* apply = new QPushButton(tr("Apply current material to all faces (Shift+T)"));
	connect(apply, &QPushButton::clicked, this, [this] { m_editor->applyMaterialToSelection(m_editor->currentMaterial()); });
	layout->addWidget(apply);

	auto* tieBox = new QGroupBox(tr("Tie to entity"));
	auto* v = new QVBoxLayout(tieBox);
	auto* combo = new QComboBox;
	combo->setEditable(true);
	QStringList classes{"func_detail", "func_brush", "func_wall_toggle", "func_door", "func_button", "trigger_once", "trigger_multiple", "func_illusionary"};
	if (const auto* catalog = m_editor->entities()) {
		for (const auto* cls : catalog->placeable()) {
			if (cls->isSolid() && !classes.contains(qs(cls->name))) {
				classes << qs(cls->name);
			}
		}
	}
	combo->addItems(classes);
	auto* completer = new QCompleter(classes, combo);
	completer->setCaseSensitivity(Qt::CaseInsensitive);
	completer->setFilterMode(Qt::MatchContains);
	combo->setCompleter(completer);
	auto* tie = new QPushButton(tr("Tie (Ctrl+T)"));
	connect(tie, &QPushButton::clicked, this, [this, combo] { m_editor->tieSelectionToEntity(combo->currentText()); });
	v->addWidget(combo);
	v->addWidget(tie);
	v->addWidget(descriptionLabel(tr("func_detail: does not cut visibility - use it for small details, pillars, stairs (faster compile, better performance).")));
	layout->addWidget(tieBox);
	layout->addWidget(descriptionLabel(tr("Tools: T move, R rotate, E scale (gizmo). Shift+drag copies. Arrow keys nudge by the grid.")));
}

} // namespace sme::app
