// properties_panel.cpp - Smart object properties (see properties_panel.h).

#include "properties_panel.h"

#include "class_picker.h"

#include "copy_paste.h"

#include "asset_browser.h"
#include "editor.h"
#include "enhancements.h"
#include "material_cache.h"
#include "toggle_switch.h"
#include "wheel_guard.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QWheelEvent>
#include <QCompleter>
#include <QListView>
#include <QStringListModel>
#include <QMenu>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QButtonGroup>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QSlider>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <map>
#include <QTableWidget>
#include <QHeaderView>
#include <QShortcut>
#include <QClipboard>
#include <QApplication>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QTabBar>
#include <QtConcurrentRun>

#include <cstdlib>
#include <cstring>
#include <set>

#include <sme/assets.h>
#include <sme/brush.h>
#include <sme/doors.h>
#include <sme/game.h>
#include <sme/history.h>
#include <sme/io_links.h>
#include <sme/map_tools.h>
#include <sme/sky_presets.h>
#include <sme/soundscape_area.h>
#include <sme/text.h>
#include <sme/texture.h>

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

/// Choices that are just yes/no ("0" No / "1" Yes): shown as a switch. Other two-way choices
/// ("Do nothing" / "Cancel script") keep their list - a switch would not say what "on" means.
bool isYesNo(const EntityProperty& property) {
	if (property.kind != EntityProperty::Kind::Choices || property.choices.size() != 2) return false;
	static const QRegularExpression no("^(no|false|off|disabled?)$", QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression yes("^(yes|true|on|enabled?)$", QRegularExpression::CaseInsensitiveOption);
	bool hasNo = false, hasYes = false;
	for (const auto& choice : property.choices) {
		const QString name = QString::fromStdString(choice.displayName).trimmed();
		hasNo = hasNo || (choice.value == "0" && no.match(name).hasMatch());
		hasYes = hasYes || (choice.value == "1" && yes.match(name).hasMatch());
	}
	return hasNo && hasYes;
}

QString helpText(const QString& title, const QString& key, const QString& type, const QString& description) {
	QString text = QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped());
	if (!key.isEmpty() && key.compare(title, Qt::CaseInsensitive) != 0) text += QStringLiteral(" <span style='color:#8a909c'>%1</span>").arg(key.toHtmlEscaped());
	if (!type.isEmpty()) text += QStringLiteral(" <span style='color:#8a909c'>(%1)</span>").arg(type.toHtmlEscaped());
	if (!description.isEmpty()) text += QStringLiteral("<br>") + description.simplified().toHtmlEscaped();
	return text;
}

QImage toQImage(const ImageRgba& image) {
	QImage out(int(image.width), int(image.height), QImage::Format_RGBA8888);
	for (unsigned y = 0; y < image.height; ++y) {
		std::memcpy(out.scanLine(int(y)), image.pixels.data() + std::size_t(y) * image.width * 4, std::size_t(image.width) * 4);
	}
	return out;
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
	layout->setSpacing(0);
	auto* top = new QHBoxLayout;
	top->setContentsMargins(6, 4, 6, 4);
	top->setSpacing(8);
	m_filter = new QLineEdit;
	m_filter->setObjectName("propertyFilter");
	m_filter->setPlaceholderText(tr("Find a property or flag"));
	m_filter->setClearButtonEnabled(true);
	m_smartEdit = new QCheckBox(tr("Smart Edit"));
	m_smartEdit->setObjectName("smartEdit");
	m_smartEdit->setToolTip(tr("On: fitting editors with descriptions. Off: every key as plain text, like Hammer"));
	m_smartEdit->setChecked(QSettings().value("properties/smartEdit", true).toBool());
	top->addWidget(m_filter, 1);
	top->addWidget(m_smartEdit);
	layout->addLayout(top);
	connect(m_smartEdit, &QCheckBox::toggled, this, [this](bool on) {
		QSettings().setValue("properties/smartEdit", on);
		scheduleRebuild();
	});
	m_scroll = new QScrollArea;
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	layout->addWidget(m_scroll, 1);
	// Help area: what the focused key does (instead of a description under every key).
	m_help = new QLabel;
	m_help->setObjectName("propertyHelp");
	m_help->setWordWrap(true);
	m_help->setTextFormat(Qt::RichText);
	m_help->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_help->setContentsMargins(10, 8, 10, 8);
	m_help->setMinimumHeight(64);
	m_help->setProperty("role", "help");
	layout->addWidget(m_help);
	connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) { showHelp(now); });
	for (const auto& name : QSettings().value("properties/collapsed").toStringList()) {
		m_collapsed.insert(name);
	}
	m_tab = QSettings().value("properties/tab", 0).toInt();
	connect(&m_skyWatcher, &QFutureWatcherBase::finished, this, [this] {
		m_skies = m_skyWatcher.result();
		scheduleRebuild();
	});
	connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
		m_filterText = text;
		scheduleRebuild();
	});
	// Ctrl+V outside a text field: "key" "value" lines (from a wiki, another map, Ctrl+C here) go
	// into the selected entities. Inside a field Ctrl+V pastes into the field as usual.
	auto* paste = new QShortcut(QKeySequence::Paste, m_scroll, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
	connect(paste, &QShortcut::activated, this, [this] {
		const QString text = QApplication::clipboard()->text();
		auto keys = parseKeyValueLines(text);
		std::vector<int> ids = m_editor->selection().entities;
		if (ids.empty() && m_editor->selection().empty()) ids = {0};
		const Entity* first = ids.empty() ? nullptr : m_editor->doc().findEntity(ids.front());
		if (!first) return;
		if (!text.contains('"')) {
			// Unquoted text could be any sentence: only keys this entity knows count.
			const auto* definition = m_editor->entities() ? m_editor->entities()->find(first->classname()) : nullptr;
			std::erase_if(keys, [&](const auto& kv) {
				const std::string key = kv.first.toStdString();
				return !(definition && definition->property(key)) && first->get(key).empty();
			});
		}
		if (!keys.empty()) pasteKeys(ids, keys);
	});
	connect(editor, &Editor::selectionChanged, this, &PropertiesPanel::scheduleRebuild);
	connect(editor, &Editor::documentChanged, this, &PropertiesPanel::scheduleRebuild);
	connect(editor, &Editor::gameChanged, this, [this] {
		m_skies.clear();
		m_skiesGame.clear();
		scheduleRebuild();
	});
	connect(editor, &Editor::toolChanged, this, &PropertiesPanel::scheduleRebuild);
	// A clicked connection line (or a new connection): the Outputs tab with that output's card.
	connect(editor, &Editor::connectionFocusRequested, this, [this](int, int index) {
		m_tab = 1;
		m_focusConnection = index;
		scheduleRebuild();
	});
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

void PropertiesPanel::pasteKeys(const std::vector<int>& ids, const std::vector<std::pair<QString, QString>>& keys) {
	Transaction t(m_editor->doc(), "Paste keys");
	for (const int id : ids) {
		Entity* entity = m_editor->doc().findEntity(id);
		if (!entity) continue;
		if (id != 0) t.touchEntity(id);
		else t.touchWorld();
		for (const auto& [key, value] : keys) {
			if (!iequals(key.toStdString(), "id")) entity->set(key.toStdString(), value.toStdString());
		}
	}
	m_editor->commit(t.finish());
	emit m_editor->message(tr("%1 key(s) pasted").arg(keys.size()));
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

QAbstractItemModel* PropertiesPanel::sharedNames(const QString& kind, const QStringList& names) {
	auto& [model, count] = m_sharedNames[kind];
	if (!model) model = new QStringListModel(this);
	// The lists only change when a game is (re)indexed; the count tells that cheaply.
	if (count != names.size() || (model->rowCount() > 0 && model->index(0).data().toString() != names.value(0))) {
		model->setStringList(names);
		count = names.size();
	}
	return model;
}

QMenu* PropertiesPanel::assetFieldMenu(QLineEdit* field, AssetKind kind) const {
	QMenu* menu = field->createStandardContextMenu();
	const QString text = field->text().trimmed();
	if (m_menuFiller && !text.isEmpty()) {
		menu->addSeparator();
		m_menuFiller(menu, {kind, text.toStdString()}, AssetPlace::Properties);
	}
	return menu;
}

void PropertiesPanel::attachAssetMenu(QLineEdit* field, AssetKind kind) {
	field->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(field, &QWidget::customContextMenuRequested, this, [this, field, kind](const QPoint& pos) {
		QMenu* menu = assetFieldMenu(field, kind);
		menu->exec(field->mapToGlobal(pos));
		delete menu;
	});
}

void PropertiesPanel::attachAssetMenu(QWidget* widget, const AssetRef& asset) {
	widget->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(widget, &QWidget::customContextMenuRequested, this, [this, widget, asset](const QPoint& pos) {
		if (!m_menuFiller) return;
		QMenu menu;
		m_menuFiller(&menu, asset, AssetPlace::Properties);
		menu.exec(widget->mapToGlobal(pos));
	});
}

void PropertiesPanel::rebuild() {
	m_defaultHelp.clear();
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
		buildEntity(layout, {0});
	}
	layout->addStretch(1);
	// (The wheel scrolls the panel past drop-downs and number boxes: WheelGuard.)
	// Drop-downs never ask for the width of their longest entry: the panel fits its dock, and a
	// long choice is read in the opened list.
	for (auto* combo : content->findChildren<QComboBox*>()) {
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		combo->setMinimumContentsLength(std::min(combo->minimumContentsLength() > 0 ? combo->minimumContentsLength() : 8, 8));
		// As wide as the row allows, never wider: the arrow always stays in view.
		combo->setSizePolicy(QSizePolicy::Ignored, combo->sizePolicy().verticalPolicy());
		combo->setMinimumWidth(std::max(combo->minimumWidth(), 72));
		if (auto* view = combo->view()) view->setMinimumWidth(std::max(view->minimumWidth(), 220));
	}
	// The panel stays where it was scrolled to: the same group stays at the same height, so
	// picking another object keeps the place one was editing.
	const int oldScroll = m_scroll->verticalScrollBar()->value();
	QString anchorName;
	int anchorOffset = 0;
	if (m_content && oldScroll > 0) {
		for (auto* header : m_content->findChildren<QPushButton*>()) {
			if (!header->objectName().startsWith(QStringLiteral("group:")) || !header->isVisibleTo(m_content)) continue;
			const int y = header->mapTo(m_content, QPoint(0, 0)).y() - oldScroll;
			if (y >= 0 || anchorName.isEmpty()) {
				anchorName = header->objectName();
				anchorOffset = y;
				if (y >= 0) break;
			}
		}
	}
	m_scroll->setWidget(content);   // deletes the previous content
	m_content = content;
	if (oldScroll > 0 && m_focusConnection < 0) {
		const auto restore = [this, content, oldScroll, anchorName, anchorOffset] {
			if (m_content != content) return;
			int target = oldScroll;
			if (auto* header = anchorName.isEmpty() ? nullptr : content->findChild<QPushButton*>(anchorName); header && header->isVisibleTo(content)) {
				target = header->mapTo(content, QPoint(0, 0)).y() - anchorOffset;
			}
			m_scroll->verticalScrollBar()->setValue(target);
		};
		content->layout()->activate();
		content->resize(m_scroll->viewport()->width(), std::max(content->sizeHint().height(), m_scroll->viewport()->height()));
		restore();
		QTimer::singleShot(0, this, restore);   // again once the scroll area knows the new height
	}
	if (m_focusConnection >= 0) {
		if (auto* card = content->findChild<QWidget*>(QStringLiteral("outputCard:%1").arg(m_focusConnection))) {
			QPointer<QWidget> target(card);
			QTimer::singleShot(0, this, [this, target] {
				if (!target) return;
				m_scroll->ensureWidgetVisible(target, 0, 40);
				if (auto* first = target->findChild<QComboBox*>()) first->setFocus();
			});
		}
		m_focusConnection = -1;
	}
	m_help->setText(m_defaultHelp);
	m_help->setVisible(!m_defaultHelp.isEmpty());
}

void PropertiesPanel::showHelp(QWidget* focused) {
	if (!m_content) return;
	for (QWidget* w = focused; w; w = w->parentWidget()) {
		if (w == m_content || w == this) break;
		if (const QVariant help = w->property("help"); help.isValid()) {
			m_help->setText(help.toString());
			m_help->setVisible(true);
			return;
		}
	}
}

QWidget* PropertiesPanel::makeEditor(const EntityProperty& property, const std::vector<int>& ids, const QString& value, bool mixed) {
	const std::string key = property.name;
	const auto commit = [this, ids, key](const QString& v) { setKey(ids, key, v); };
	const QString type = qs(property.type);

	if (iequals(key, "skyname")) {
		return makeSkyPicker(value);
	}
	if (isYesNo(property)) {
		// Yes/no as a switch.
		auto* holder = new QWidget;
		auto* h = new QHBoxLayout(holder);
		h->setContentsMargins(0, 0, 0, 0);
		auto* toggle = new ToggleSwitch;
		const QString effective = value.isEmpty() ? qs(property.defaultValue) : value;
		toggle->setChecked(!mixed && effective == "1");
		if (mixed) toggle->setToolTip(tr("(different)"));
		connect(toggle, &QCheckBox::toggled, this, [commit](bool on) { commit(on ? QStringLiteral("1") : QStringLiteral("0")); });
		h->addWidget(toggle);
		auto* state = new QLabel(mixed ? tr("(different)") : QString());
		state->setProperty("role", "description");
		h->addWidget(state);
		h->addStretch(1);
		holder->setFocusProxy(toggle);
		return holder;
	}

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
	const bool isParticle = type == "particlesystem" || toLower(key) == "effect_name";
	const bool isSoundscape = iequals(key, "soundscape") || type == "soundscape";
	const bool isMaterial = type == "material" || type == "decal" || type == "sprite";

	if (isTarget || isSequence || isSound || isModel || isParticle || isSoundscape || isMaterial) {
		auto* combo = new QComboBox;
		combo->setEditable(true);
		combo->setInsertPolicy(QComboBox::NoInsert);
		QStringList items;
		QAbstractItemModel* shared = nullptr;   // big game-wide lists: one model for every field
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
			shared = sharedNames(QStringLiteral("sound"), m_assets->soundNames());
		} else if (isModel) {
			shared = sharedNames(QStringLiteral("model"), m_assets->modelNames());
		} else if (isParticle) {
			shared = sharedNames(QStringLiteral("particle"), m_assets->particleNames());
		} else if (isSoundscape) {
			shared = sharedNames(QStringLiteral("soundscape"), m_assets->soundscapeNames());
		} else if (isMaterial) {
			// Sprites come from materials/sprites, everything else from all materials.
			const QString kind = type == "sprite" ? QStringLiteral("sprite") : QStringLiteral("material");
			auto& cached = m_materialNames[kind];
			const auto all = m_assets->materialNames();
			if (cached.isEmpty() || m_materialNameCount != all.size()) {
				m_materialNameCount = all.size();
				for (auto& [k, list] : m_materialNames) list.clear();
				for (const auto& name : all) {
					const QString q = QString::fromStdString(name).toLower();
					m_materialNames[QStringLiteral("material")] << q;
					if (q.startsWith(QLatin1String("sprites/"))) m_materialNames[QStringLiteral("sprite")] << q;
				}
			}
			shared = sharedNames(kind, m_materialNames[kind]);
		}
		// Never measure every name for the box's width (that alone took seconds with all of
		// HL2's models); the popup lists them with one row height.
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		combo->setMinimumContentsLength(8);
		combo->setMaxVisibleItems(20);   // a long list to scroll through, not a handful of rows
		if (auto* list = qobject_cast<QListView*>(combo->view())) list->setUniformItemSizes(true);
		if (shared) {
			combo->setModel(shared);
		} else {
			combo->addItems(items);
		}
		auto* completer = shared ? new QCompleter(shared, combo) : new QCompleter(items, combo);
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
		QWidget* after = nullptr;   // a button right of the box
		if (isSound) attachAssetMenu(combo->lineEdit(), AssetKind::Sound);
		if (isModel) attachAssetMenu(combo->lineEdit(), AssetKind::Model);
		if (isParticle) attachAssetMenu(combo->lineEdit(), AssetKind::Particle);
		if (isSequence && ids.size() == 1) {
			// ▶: the animation plays on the Animations page (the model this entity animates).
			auto* play = new QToolButton;
			play->setObjectName("previewSequence");
			play->setText(QStringLiteral("▶"));
			play->setToolTip(tr("Watch this animation on the Animations page"));
			const int entityId = ids.front();
			connect(play, &QToolButton::clicked, this, [this, combo, entityId] {
				if (const auto* entity = m_editor->doc().findEntity(entityId)) emit animationPreviewRequested(modelFor(*entity), combo->currentText().trimmed());
			});
			after = play;
		}
		if (isMaterial) {
			attachAssetMenu(combo->lineEdit(), AssetKind::Material);
			auto* use = new QToolButton;
			use->setText(tr("Use current"));
			use->setToolTip(tr("Set the material selected in the material browser"));
			connect(use, &QToolButton::clicked, this, [this, commit] { commit(m_editor->currentMaterial().toLower()); });
			after = use;
		}
		// Target fields also list the entities without a name (NPCs first for a Target NPC):
		// picking one names it in the same undo step, so any entity can be the target.
		const bool namesEntities = isTarget && type != "filterclass" && type != "npcclass";
		if (namesEntities && !shared) {
			const bool wantsNpc = toLower(key) == "m_iszentity" || type == "target_name_or_class";
			std::vector<const Entity*> unnamed;
			for (const auto& entity : m_editor->doc().entities) {
				if (!entity.get("targetname").empty() || std::find(ids.begin(), ids.end(), entity.id) != ids.end()) continue;
				unnamed.push_back(&entity);
			}
			const auto isNpc = [](const Entity* e) { return startsWithI(e->classname(), "npc_") || startsWithI(e->classname(), "monster_"); };
			if (wantsNpc) std::stable_partition(unnamed.begin(), unnamed.end(), isNpc);
			if (unnamed.size() > 200) unnamed.resize(200);
			for (const Entity* entity : unnamed) {
				combo->addItem(tr("%1  #%2  (no name yet - gets one)").arg(qs(entity->classname())).arg(entity->id));
				combo->setItemData(combo->count() - 1, entity->id, TARGET_ENTITY_ROLE);
			}
			combo->setEditText(mixed ? QString() : value);   // adding items must not change the text shown
		}
		const auto ids2 = ids;
		connect(combo, &QComboBox::activated, this, [this, combo, commit, ids2, key](int index) {
			if (const int targetId = combo->itemData(index, TARGET_ENTITY_ROLE).toInt(); targetId > 0) {
				auto [change, name] = edit::setTarget(m_editor->doc(), ids2, key, targetId);
				if (!name.empty()) {
					m_editor->commit(std::move(change));
					emit m_editor->message(tr("Target set to %1 (the entity got this name)").arg(qs(name)));
				}
				return;
			}
			commit(combo->currentText());
		});
		connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [combo, commit, value] {
			if (combo->currentIndex() >= 0 && combo->itemData(combo->currentIndex(), TARGET_ENTITY_ROLE).toInt() > 0 &&
			    combo->currentText() == combo->itemText(combo->currentIndex())) {
				return;   // an unnamed entity was picked: activated() names it
			}
			if (combo->currentText() != value) {
				commit(combo->currentText());
			}
		});
		h->addWidget(combo, 1);
		if (after) h->addWidget(after);
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

void PropertiesPanel::loadSkies() {
	const auto* game = m_editor->game();
	const QString key = m_editor->gameEntry() ? QString::fromStdString(m_editor->gameEntry()->key()) : QString();
	if (!game || key == m_skiesGame || m_skyWatcher.isRunning()) {
		return;
	}
	m_skiesGame = key;
	const GameFileSystem* fs = &game->files;
	// Names and a small picture of each sky's front, read in the background (VPKs can be slow).
	m_skyWatcher.setFuture(QtConcurrent::run([fs] {
		std::vector<std::pair<QString, QImage>> skies;
		for (const auto& name : skyboxNames(*fs)) {
			QImage picture;
			if (const auto image = loadTexture(*fs, "skybox/" + name + "ft", 64)) {
				picture = toQImage(*image);
			}
			skies.emplace_back(QString::fromStdString(name), std::move(picture));
		}
		return skies;
	}));
}

QWidget* PropertiesPanel::makeSkyPicker(const QString& value) {
	loadSkies();
	auto* combo = new QComboBox;
	combo->setObjectName("skyPicker");
	WheelGuard::setWheelWhenResting(combo);   // rest the mouse on it, then wheel through the skies
	combo->setMaxVisibleItems(16);
	combo->setEditable(true);
	combo->setInsertPolicy(QComboBox::NoInsert);
	combo->setIconSize(QSize(28, 28));
	QStringList names;
	for (const auto& [name, picture] : m_skies) {
		combo->addItem(picture.isNull() ? QIcon() : QIcon(QPixmap::fromImage(picture)), name);
		if (const auto* preset = findSkyPreset(name.toStdString())) {
			combo->setItemData(combo->count() - 1, tr("Brings its sun and ambient light (as in %1)").arg(QString::fromUtf8(preset->game)), Qt::ToolTipRole);
		}
		names << name;
	}
	auto* completer = new QCompleter(names, combo);
	completer->setCaseSensitivity(Qt::CaseInsensitive);
	completer->setFilterMode(Qt::MatchContains);
	combo->setCompleter(completer);
	const int index = combo->findText(value, Qt::MatchFixedString);
	combo->setCurrentIndex(index);   // -1: no picture of another sky next to an unknown name
	if (index < 0) {
		combo->setEditText(value);
	}
	if (m_skies.empty()) {
		combo->lineEdit()->setPlaceholderText(m_skyWatcher.isRunning() ? tr("reading the game's skies ...") : tr("no skies found in materials/skybox"));
	} else if (!value.isEmpty() && index < 0) {
		combo->setToolTip(tr("This game has no sky called \"%1\" (materials/skybox/%1up.vmt)").arg(value));
		combo->setProperty("problem", true);
	}
	// The light that belongs to the sky comes along (one undo step); a second signal for the
	// same name does nothing.
	connect(combo, &QComboBox::activated, this, [this, combo] { m_editor->setSky(combo->currentText()); });
	connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [this, combo] { m_editor->setSky(combo->currentText()); });
	return combo;
}

void PropertiesPanel::buildEntity(QVBoxLayout* layout, const std::vector<int>& entityIds) {
	const auto& doc = m_editor->doc();
	const auto* catalog = m_editor->entities();
	const Entity* first = doc.findEntity(entityIds.front());
	if (!first) {
		return;
	}
	const bool world = entityIds.front() == 0;
	const std::string classname = first->classname();
	const bool sameClass = std::all_of(entityIds.begin(), entityIds.end(), [&](int id) {
		const auto* e = doc.findEntity(id);
		return e && iequals(e->classname(), classname);
	});
	const EntityClass* definition = catalog ? catalog->find(classname) : nullptr;

	// Class line: the class (a drop-down changes it, keeping the keys) and what is selected.
	auto* header = new QHBoxLayout;
	header->setSpacing(8);
	if (world) {
		auto* worldLabel = new QLabel(QStringLiteral("<b>worldspawn</b>"));
		worldLabel->setObjectName("entityClass");
		header->addWidget(worldLabel, 1);
		header->addWidget(descriptionLabel(tr("%1 brushes, %2 entities").arg(doc.solidCount()).arg(doc.entities.size())));
	} else {
		auto* classPicker = new ClassPicker(catalog, !first->solids.empty());
		classPicker->setClassName(sameClass ? qs(classname) : QString(), tr("(different classes)"));
		connect(classPicker, &ClassPicker::classChosen, this, [this, entityIds](const QString& name) { setKey(entityIds, "classname", name); });
		header->addWidget(classPicker, 1);
		header->addWidget(descriptionLabel(entityIds.size() > 1 ? tr("%1 entities").arg(entityIds.size()) : tr("id %1").arg(first->id)));
	}
	layout->addLayout(header);
	if (!sameClass) {
		layout->addWidget(descriptionLabel(tr("Select entities of one class to edit their keys together.")));
		return;
	}
	if (definition && !definition->description.empty()) {
		// The class description: in the help area while no key has the focus, short above.
		// The class description is in the help area while no key has the focus.
		m_defaultHelp = helpText(qs(classname), QString(), QString(), qs(definition->description));
	} else if (!definition && catalog) {
		auto* warn = new QLabel(tr("Class \"%1\" is not in the loaded FGDs - only raw keys can be edited.").arg(qs(classname)));
		warn->setProperty("role", "warning");
		warn->setWordWrap(true);
		layout->addWidget(warn);
	}

	// NPC helper first: posing NPCs is the common job.
	if (entityIds.size() == 1 && !world) {
		buildNpcHelper(layout, *first, definition);
	}
	if (entityIds.size() == 1 && iequals(classname, "env_cubemap")) {
		buildCubemapBox(layout, *first);
	}
	if (entityIds.size() == 1 && iequals(classname, "func_instance")) {
		buildInstanceBox(layout, *first);
	}
	if (entityIds.size() == 1 && isSoundscapeEntity(*first)) {
		buildSoundscapeArea(layout, *first);
	}
	// Game enhancements of the prop's model (mapbase-fidelity): its switches hide when none applies.
	if (const QString model = qs(first->get("model")); entityIds.size() == 1 && m_editor->enhancements()->available() &&
	                                                    model.endsWith(QStringLiteral(".mdl"), Qt::CaseInsensitive)) {
		auto* switches = new EnhancementSwitches(m_editor->enhancements());
		layout->addWidget(switches);
		switches->setModel(model);
	}
	if (entityIds.size() == 1 && isDoor(*first)) {
		buildDoorBox(layout, *first);
	}
	if (entityIds.size() == 1 && iequals(classname, "ambient_generic")) {
		buildSoundLoopBox(layout, *first);
	}

	// Tabs: Keyvalues / Outputs / Inputs with their counts (outputs and inputs for one entity).
	const bool single = entityIds.size() == 1 && !world;
	auto* keyPage = new QWidget;
	auto* keyLayout = new QVBoxLayout(keyPage);
	keyLayout->setContentsMargins(0, 4, 0, 0);
	keyLayout->setSpacing(6);
	int keyCount = 0;
	if (!m_smartEdit->isChecked()) {
		buildRawKeys(keyLayout, entityIds, definition);
		keyCount = int(first->keyValues.size());
	} else {
		keyCount = buildKeyvalues(keyLayout, entityIds, definition);
	}
	if (!single) {
		layout->addWidget(keyPage);
		return;
	}
	auto* outputPage = new QWidget;
	auto* outputLayout = new QVBoxLayout(outputPage);
	outputLayout->setContentsMargins(0, 4, 0, 0);
	buildOutputs(outputLayout, first->id);
	auto* inputPage = new QWidget;
	auto* inputLayout = new QVBoxLayout(inputPage);
	inputLayout->setContentsMargins(0, 4, 0, 0);
	buildIncoming(inputLayout, *first);

	auto* tabs = new QTabBar;
	tabs->setObjectName("propertyTabs");
	tabs->setDrawBase(false);
	tabs->setExpanding(false);
	tabs->addTab(tr("Keyvalues  %1").arg(keyCount));
	tabs->addTab(tr("Outputs  %1").arg(first->connections.size()));
	tabs->addTab(tr("Inputs  %1").arg(incomingCount(*first)));
	layout->addWidget(tabs);
	QWidget* pages[3] = {keyPage, outputPage, inputPage};
	const int current = std::clamp(m_tab, 0, 2);
	for (int i = 0; i < 3; ++i) {
		layout->addWidget(pages[i]);
		pages[i]->setVisible(i == current);
	}
	tabs->setCurrentIndex(current);
	connect(tabs, &QTabBar::currentChanged, this, [this, keyPage, outputPage, inputPage](int index) {
		m_tab = index;
		QSettings().setValue("properties/tab", index);
		keyPage->setVisible(index == 0);
		outputPage->setVisible(index == 1);
		inputPage->setVisible(index == 2);
	});
}

QGridLayout* PropertiesPanel::addGroup(QVBoxLayout* layout, const QString& name, int count) {
	auto* header = new QPushButton;
	header->setObjectName(QStringLiteral("group:%1").arg(name));
	header->setProperty("role", "groupHeader");
	header->setCursor(Qt::PointingHandCursor);
	header->setFocusPolicy(Qt::NoFocus);
	auto* body = new QWidget;
	auto* grid = new QGridLayout(body);
	grid->setContentsMargins(10, 4, 2, 6);
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(5);
	grid->setColumnStretch(1, 1);
	// While searching every group with a match is open.
	const bool open = !m_filterText.isEmpty() || !m_collapsed.contains(name);
	const auto label = [name, count](bool isOpen) {
		return QStringLiteral("%1  %2   %3").arg(isOpen ? QStringLiteral("▾") : QStringLiteral("▸"), name).arg(count);
	};
	header->setText(label(open));
	connect(header, &QPushButton::clicked, this, [this, header, body, name, label] {
		const bool nowOpen = !body->isVisible();
		body->setVisible(nowOpen);
		header->setText(label(nowOpen));
		if (nowOpen) m_collapsed.erase(name);
		else m_collapsed.insert(name);
		QStringList list(m_collapsed.begin(), m_collapsed.end());
		QSettings().setValue("properties/collapsed", list);
	});
	layout->addWidget(header);
	layout->addWidget(body);
	body->setVisible(open);   // only now: shown without a parent it would flash up as its own window
	return grid;
}

void PropertiesPanel::addRow(QGridLayout* grid, const QString& label, QWidget* editor, const QString& help) {
	const int row = grid->rowCount();
	auto* name = new QLabel(label);
	name->setProperty("role", "rowLabel");
	name->setWordWrap(true);
	name->setMinimumWidth(84);   // long names wrap: the panel fits narrow docks
	name->setMaximumWidth(160);
	name->setToolTip(help);
	name->setBuddy(editor);
	editor->setProperty("help", help);
	if (editor->toolTip().isEmpty()) editor->setToolTip(help);
	grid->addWidget(name, row, 0, Qt::AlignVCenter);
	grid->addWidget(editor, row, 1);
}

int PropertiesPanel::buildKeyvalues(QVBoxLayout* layout, const std::vector<int>& entityIds, const EntityClass* definition) {
	const auto& doc = m_editor->doc();
	const Entity* first = doc.findEntity(entityIds.front());
	const bool world = entityIds.front() == 0;
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
	const auto matches = [this](const QString& display, const std::string& key) {
		return m_filterText.isEmpty() || display.contains(m_filterText, Qt::CaseInsensitive) || qs(key).contains(m_filterText, Qt::CaseInsensitive);
	};
	int shown = 0;
	std::set<std::string> known = {"classname", "id"};
	if (world) {
		known.insert("mapversion");   // written by the editor on save, like Hammer
	}

	// Groups by the FGD class that defines the key, in FGD order (Targetname, Parentname, ..., the class).
	struct Row {
		const EntityProperty* property;
		QString display;
	};
	std::vector<std::pair<QString, std::vector<Row>>> groups;
	const auto groupFor = [&](const QString& name) -> std::vector<Row>& {
		for (auto& [groupName, rows] : groups) {
			if (groupName == name) return rows;
		}
		groups.emplace_back(name, std::vector<Row>{});
		return groups.back().second;
	};
	// Every entity can have a name (outputs and scripts find it by that), even when its FGD forgot it.
	static EntityProperty nameProperty = [] {
		EntityProperty name;
		name.name = "targetname";
		name.type = "target_source";
		name.origin = "Targetname";
		return name;
	}();
	nameProperty.displayName = tr("Name").toStdString();
	nameProperty.description = tr("The name that other entities refer to this entity by.").toStdString();
	const bool hasName = definition && definition->property("targetname");
	if (!hasName && !world) {
		known.insert("targetname");
		if (matches(tr("Name"), "targetname")) groupFor(QStringLiteral("Targetname")).push_back({&nameProperty, tr("Name")});
	}
	std::vector<const EntityProperty*> flagProperties;
	if (definition) {
		for (const auto& property : definition->properties) {
			known.insert(toLower(property.name));
			if (property.kind == EntityProperty::Kind::Flags) {
				flagProperties.push_back(&property);
				continue;
			}
			const QString display = property.displayName.empty() ? qs(property.name) : qs(property.displayName);
			if (!matches(display, property.name)) continue;
			groupFor(property.origin.empty() ? qs(definition->name) : qs(property.origin)).push_back({&property, display});
		}
	}
	for (auto& [groupName, rows] : groups) {
		if (rows.empty()) continue;
		auto* grid = addGroup(layout, groupName, int(rows.size()));
		for (const auto& [property, display] : rows) {
			bool mixed = false;
			const QString value = valueOf(property->name, mixed);
			auto* editor = makeEditor(*property, entityIds, value, mixed);
			addRow(grid, display, editor, helpText(display, qs(property->name), qs(property->type), qs(property->description)));
			++shown;
		}
	}

	// Flags as switches, in a group of their own.
	for (const auto* property : flagProperties) {
		std::vector<const EntityFlag*> flags;
		for (const auto& flag : property->flags) {
			if (matches(qs(flag.displayName), property->name)) flags.push_back(&flag);
		}
		if (flags.empty()) continue;
		auto* grid = addGroup(layout, property->displayName.empty() || iequals(property->displayName, "spawnflags") ? tr("Flags") : qs(property->displayName),
		                      int(flags.size()));
		bool mixed = false;
		const auto current = valueOf(property->name, mixed).toULongLong();
		for (const auto* flag : flags) {
			auto* toggle = new ToggleSwitch;
			toggle->setChecked(first->get(property->name).empty() ? flag->enabledByDefault : (current & flag->value) != 0);
			const std::string key = property->name;
			const auto bit = flag->value;
			connect(toggle, &QCheckBox::toggled, this, [this, entityIds, key, bit](bool on) {
				// Toggle the bit on every selected entity, keeping their other bits.
				Transaction t(m_editor->doc(), "Edit flags");
				for (const int id : entityIds) {
					if (auto* e = m_editor->doc().findEntity(id)) {
						t.touchEntity(id);
						auto bits = std::stoull(e->get(key, "0").empty() ? "0" : e->get(key, "0"));
						bits = on ? (bits | bit) : (bits & ~bit);
						e->set(key, std::to_string(bits));
					}
				}
				m_editor->commit(t.finish());
			});
			auto* holder = new QWidget;
			auto* h = new QHBoxLayout(holder);
			h->setContentsMargins(0, 0, 0, 0);
			h->addWidget(toggle);
			h->addStretch(1);
			holder->setFocusProxy(toggle);
			addRow(grid, qs(flag->displayName), holder, helpText(qs(flag->displayName), qs(property->name), tr("flag %1").arg(flag->value), QString()));
			toggle->setProperty("help", holder->property("help"));
			++shown;
		}
	}

	// Keys the FGD does not know.
	std::vector<std::pair<std::string, std::string>> others;
	for (const auto& [key, value] : first->keyValues) {
		if (known.contains(toLower(key)) || iequals(key, "origin") || iequals(key, "angles")) continue;
		if (!matches(qs(key), key)) continue;
		others.emplace_back(key, value);
	}
	if (!others.empty()) {
		auto* grid = addGroup(layout, tr("Other keys"), int(others.size()));
		for (const auto& [key, value] : others) {
			auto* row = new QWidget;
			auto* h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(4);
			auto* edit = new QLineEdit(qs(value));
			auto* remove = new QToolButton;
			remove->setText(QStringLiteral("✕"));
			remove->setToolTip(tr("Remove this key"));
			const std::string k = key;
			const std::string v = value;
			connect(edit, &QLineEdit::editingFinished, this, [this, edit, entityIds, k, v] {
				if (edit->text().toStdString() != v) setKey(entityIds, k, edit->text());
			});
			connect(remove, &QToolButton::clicked, this, [this, entityIds, k] { m_editor->commit(edit::setKeyValue(m_editor->doc(), entityIds, k, std::nullopt)); });
			h->addWidget(edit, 1);
			h->addWidget(remove);
			row->setFocusProxy(edit);
			addRow(grid, qs(key), row, helpText(qs(key), QString(), QString(), tr("A key the FGD does not list.")));
			++shown;
		}
	}

	// Position and rotation of point entities.
	if (first->solids.empty() && !world) {
		std::vector<const char*> placement;
		for (const char* key : {"origin", "angles"}) {
			if (!known.contains(key) && matches(key == std::string("origin") ? tr("Position") : tr("Rotation"), key)) placement.push_back(key);
		}
		if (!placement.empty()) {
			auto* grid = addGroup(layout, tr("Placement"), int(placement.size()));
			for (const char* key : placement) {
				bool mixed = false;
				const QString value = valueOf(key, mixed);
				auto* edit = new QLineEdit(mixed ? QString() : value);
				const std::string k = key;
				connect(edit, &QLineEdit::editingFinished, this, [this, edit, entityIds, k, value] {
					if (edit->text() != value) setKey(entityIds, k, edit->text());
				});
				const bool origin = key == std::string("origin");
				addRow(grid, origin ? tr("Position") : tr("Rotation"), edit,
				       helpText(origin ? tr("Position") : tr("Rotation"), qs(key), QString(),
				                origin ? tr("Where the entity is (x y z).") : tr("Pitch yaw roll in degrees.")));
				++shown;
			}
		}
	}
	if (shown == 0 && !m_filterText.isEmpty()) {
		layout->addWidget(descriptionLabel(tr("No key or flag matches \"%1\".").arg(m_filterText)));
	}
	buildAddKeyRow(layout, entityIds);
	return shown;
}

void PropertiesPanel::buildCubemapBox(QVBoxLayout* layout, const Entity& cubemap) {
	auto* box = new QGroupBox(tr("Room box (parallax-corrected reflections)"));
	box->setObjectName("cubemapBox");
	auto* v = new QVBoxLayout(box);
	const auto* entry = m_editor->gameEntry();
	const bool supported = (entry && entry->usesMapbase) || (m_editor->entities() && m_editor->entities()->find("parallax_obb"));
	if (!supported) {
		v->addWidget(descriptionLabel(tr("Reflections that line up with the room need Mapbase (parallax_obb). This game does not have it.")));
		layout->addWidget(box);
		return;
	}
	const int id = cubemap.id;
	const Entity* obb = cubemapRoomBox(m_editor->doc(), cubemap);
	if (!obb) {
		v->addWidget(descriptionLabel(tr("Give the cubemap the size of its room: reflections on floors and walls then line up with the "
		                                 "room instead of floating. The box is found from the walls around the cubemap.")));
		auto* add = new QPushButton(tr("Add room box (fit to the walls)"));
		connect(add, &QPushButton::clicked, this, [this, id] { m_editor->fitCubemapRoomBox(id); });
		v->addWidget(add);
		layout->addWidget(box);
		return;
	}
	Bounds bounds;
	for (const auto& solid : obb->solids) bounds.add(buildBrushGeometry(solid).bounds);
	v->addWidget(descriptionLabel(tr("Drag the box's side squares in the view, or type the size. %1 is compiled into the materials "
	                                 "and removed from the map by VBSP.").arg(qs(obb->get("targetname")))));
	auto* form = new QFormLayout;
	const Vec3 size = bounds.size();
	const Vec3 center = bounds.center();
	const char* names[3] = {QT_TR_NOOP("Width (X)"), QT_TR_NOOP("Depth (Y)"), QT_TR_NOOP("Height (Z)")};
	for (int axis = 0; axis < 3; ++axis) {
		auto* spin = new QDoubleSpinBox;
		spin->setRange(1, 32768);
		spin->setDecimals(0);
		spin->setSingleStep(16);
		spin->setValue(size[axis]);
		spin->setKeyboardTracking(false);
		connect(spin, &QDoubleSpinBox::valueChanged, this, [this, id, bounds, axis, center](double value) {
			Bounds resized = bounds;
			if (axis == 2) {
				resized.max.z = bounds.min.z + value;   // height grows upward from the floor
			} else {
				resized.min[axis] = center[axis] - value / 2;
				resized.max[axis] = center[axis] + value / 2;
			}
			m_editor->setCubemapRoomBox(id, resized);
		});
		form->addRow(tr(names[axis]), spin);
	}
	v->addLayout(form);
	auto* row = new QHBoxLayout;
	auto* fit = new QPushButton(tr("Fit to the walls"));
	connect(fit, &QPushButton::clicked, this, [this, id] { m_editor->fitCubemapRoomBox(id); });
	auto* select = new QPushButton(tr("Select box"));
	select->setToolTip(tr("Select the parallax_obb itself (move, rotate, keys)"));
	const int obbId = obb->id;
	connect(select, &QPushButton::clicked, this, [this, obbId] {
		Selection selection;
		selection.entities.push_back(obbId);
		m_editor->setSelection(std::move(selection));
	});
	auto* remove = new QPushButton(tr("Remove"));
	connect(remove, &QPushButton::clicked, this, [this, id] { m_editor->removeCubemapRoomBox(id); });
	row->addWidget(fit);
	row->addWidget(select);
	row->addWidget(remove);
	v->addLayout(row);
	layout->addWidget(box);
}

void PropertiesPanel::buildSoundLoopBox(QVBoxLayout* layout, const Entity& ambient) {
	const auto* game = m_editor->game();
	const std::string sound = ambient.get("message");
	if (!game || sound.empty()) {
		return;
	}
	const std::vector<SoundScriptEntry>* scripts = nullptr;
	if (soundFileOf(sound, nullptr).empty()) {
		// A sound script name: the scripts are read once per game, on the first such sound.
		if (m_soundScriptsGame != game || !m_soundScripts) {
			m_soundScripts = soundScripts(game->files);
			m_soundScriptsGame = game;
		}
		scripts = &*m_soundScripts;
	}
	const auto loops = soundLoops(game->files, sound, scripts);
	if (!loops) {
		return;   // file missing or not a WAV/MP3: nothing to compare
	}
	const int flags = std::atoi(ambient.get("spawnflags", "0").c_str());
	const bool notLooped = (flags & AMBIENT_NOT_LOOPED) != 0;
	auto* box = new QGroupBox(tr("Loop"));
	box->setObjectName("soundLoop");
	auto* v = new QVBoxLayout(box);
	auto* state = new QLabel(*loops ? tr("The sound file loops (it has a loop point).") : tr("The sound file plays once (no loop point)."));
	state->setObjectName("soundLoopState");
	state->setWordWrap(true);
	v->addWidget(state);
	if (*loops == notLooped) {
		auto* warn = new QLabel(*loops ? tr("\"Is NOT Looped\" is on: the sound plays forever and cannot be stopped.")
		                               : tr("\"Is NOT Looped\" is off: the sound plays only the first time."));
		warn->setProperty("role", "warning");
		warn->setWordWrap(true);
		v->addWidget(warn);
		auto* fix = new QPushButton(*loops ? tr("Turn \"Is NOT Looped\" off") : tr("Turn \"Is NOT Looped\" on"));
		fix->setObjectName("soundLoopFix");
		fix->setProperty("accent", true);
		const int id = ambient.id;
		const int fixed = flags ^ AMBIENT_NOT_LOOPED;
		connect(fix, &QPushButton::clicked, this, [this, id, fixed] { setKey({id}, "spawnflags", QString::number(fixed)); });
		v->addWidget(fix);
	} else {
		v->addWidget(descriptionLabel(tr("\"Is NOT Looped\" matches the file.")));
	}
	layout->addWidget(box);
}

void PropertiesPanel::buildSoundscapeArea(QVBoxLayout* layout, const Entity& soundscape) {
	auto* box = new QGroupBox(tr("Soundscape area"));
	box->setObjectName("soundscapeArea");
	auto* v = new QVBoxLayout(box);
	const int id = soundscape.id;

	// Which soundscape plays, with a way to open it in the Sounds page.
	const QString name = qs(soundscape.get("soundscape"));
	auto* nameRow = new QHBoxLayout;
	auto* nameLabel = new QLabel(name.isEmpty() ? tr("No soundscape chosen yet") : QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped()));
	nameLabel->setObjectName("soundscapeName");
	auto* edit = new QPushButton(tr("Edit soundscape"));
	edit->setObjectName("editSoundscape");
	edit->setEnabled(!name.isEmpty());
	connect(edit, &QPushButton::clicked, this, [this, name] { emit editSoundscapeRequested(name); });
	nameRow->addWidget(nameLabel, 1);
	nameRow->addWidget(edit);
	v->addLayout(nameRow);

	// Shape: sphere (radius) or block (trigger_soundscape).
	const Entity* trigger = soundscapeTrigger(m_editor->doc(), soundscape);
	auto* shapeRow = new QHBoxLayout;
	shapeRow->addWidget(new QLabel(tr("Shape")));
	auto* sphere = new QPushButton(tr("Sphere"));
	sphere->setObjectName("soundscapeSphere");
	auto* block = new QPushButton(tr("Block"));
	block->setObjectName("soundscapeBlock");
	auto* group = new QButtonGroup(box);
	for (auto* b : {sphere, block}) {
		b->setCheckable(true);
		group->addButton(b);
		shapeRow->addWidget(b);
	}
	shapeRow->addStretch(1);
	(trigger ? block : sphere)->setChecked(true);
	if (iequals(soundscape.classname(), "env_soundscape_proxy") && !trigger) {
		block->setEnabled(false);   // a proxy plays another entity's soundscape; triggers need env_soundscape_triggerable
		block->setToolTip(tr("Only env_soundscape can get a block (it becomes env_soundscape_triggerable)"));
	}
	v->addLayout(shapeRow);
	const bool hasBlock = trigger != nullptr;
	connect(block, &QPushButton::clicked, this, [this, id, hasBlock] {
		if (hasBlock) return;
		const auto* triggerClass = m_editor->entities() ? m_editor->entities()->find("trigger_soundscape") : nullptr;
		auto [change, triggerId] = edit::makeSoundscapeBlock(m_editor->doc(), id, {}, triggerClass);
		if (triggerId != 0) m_editor->commit(std::move(change));
	});
	connect(sphere, &QPushButton::clicked, this, [this, id, hasBlock, block] {
		if (!hasBlock) return;
		const auto answer = QMessageBox::question(this, tr("Soundscape area"),
		                                          tr("Remove the trigger_soundscape block? The soundscape is then heard in a sphere around the entity again."));
		if (answer != QMessageBox::Yes) {
			block->setChecked(true);
			return;
		}
		m_editor->commit(edit::removeSoundscapeBlock(m_editor->doc(), id));
	});

	if (!trigger) {
		// Sphere: whole map, or a radius with a field and a slider (the view's squares do the same).
		const double radius = soundscapeRadius(soundscape);
		const bool everywhere = radius < 0;
		auto* wholeRow = new QHBoxLayout;
		auto* whole = new ToggleSwitch;
		whole->setObjectName("soundscapeEverywhere");
		whole->setChecked(everywhere);
		wholeRow->addWidget(whole);
		wholeRow->addWidget(new QLabel(tr("Whole map (heard everywhere the entity can see)")), 1);
		v->addLayout(wholeRow);
		auto* radiusRow = new QHBoxLayout;
		radiusRow->addWidget(new QLabel(tr("Radius")));
		auto* spin = new QSpinBox;
		spin->setObjectName("soundscapeRadius");
		spin->setRange(1, 32768);
		spin->setSingleStep(16);
		spin->setSuffix(tr(" units"));
		spin->setKeyboardTracking(false);
		spin->setValue(everywhere ? int(DEFAULT_SOUNDSCAPE_RADIUS) : int(radius));
		auto* slider = new QSlider(Qt::Horizontal);
		slider->setObjectName("soundscapeRadiusSlider");
		slider->setRange(16, 4096);
		slider->setValue(spin->value());
		for (QWidget* w : std::initializer_list<QWidget*>{spin, slider}) w->setEnabled(!everywhere);
		radiusRow->addWidget(spin);
		radiusRow->addWidget(slider, 1);
		v->addLayout(radiusRow);
		connect(whole, &QCheckBox::toggled, this, [this, id, spin](bool on) {
			m_editor->commit(edit::setSoundscapeRadius(m_editor->doc(), id, on ? SOUNDSCAPE_EVERYWHERE : spin->value()));
		});
		connect(spin, &QSpinBox::valueChanged, this, [this, id](int value) { m_editor->commit(edit::setSoundscapeRadius(m_editor->doc(), id, value)); });
		// The slider shows the value while it moves and changes the map once when let go (the panel
		// is rebuilt after every change, which would take the slider away mid-drag).
		connect(slider, &QSlider::sliderMoved, spin, [spin](int value) {
			const QSignalBlocker quiet(spin);
			spin->setValue(value);
		});
		connect(slider, &QSlider::sliderReleased, this, [this, id, slider] {
			m_editor->commit(edit::setSoundscapeRadius(m_editor->doc(), id, slider->value()));
		});
		v->addWidget(descriptionLabel(tr("Drag the pink squares on the sphere in the view to change the radius.")));
	} else {
		// Block: the trigger's size, and a way to select it for the resize squares.
		Bounds bounds;
		for (const auto& solid : trigger->solids) bounds.add(buildBrushGeometry(solid).bounds);
		v->addWidget(descriptionLabel(tr("A trigger_soundscape switches to this soundscape when a player walks into the block. "
		                                 "Select the block to move it or drag its sides.")));
		auto* form = new QFormLayout;
		const Vec3 size = bounds.size();
		const Vec3 center = bounds.center();
		const char* names[3] = {QT_TR_NOOP("Width (X)"), QT_TR_NOOP("Depth (Y)"), QT_TR_NOOP("Height (Z)")};
		for (int axis = 0; axis < 3; ++axis) {
			auto* spin = new QDoubleSpinBox;
			spin->setObjectName(QStringLiteral("soundscapeBlockSize%1").arg(axis));
			spin->setRange(1, 32768);
			spin->setDecimals(0);
			spin->setSingleStep(16);
			spin->setValue(size[axis]);
			spin->setKeyboardTracking(false);
			connect(spin, &QDoubleSpinBox::valueChanged, this, [this, id, bounds, axis, center](double value) {
				Bounds resized = bounds;
				if (axis == 2) {
					resized.max.z = bounds.min.z + value;   // height grows upward from the floor
				} else {
					resized.min[axis] = center[axis] - value / 2;
					resized.max[axis] = center[axis] + value / 2;
				}
				const auto* triggerClass = m_editor->entities() ? m_editor->entities()->find("trigger_soundscape") : nullptr;
				auto [change, triggerId] = edit::makeSoundscapeBlock(m_editor->doc(), id, resized, triggerClass);
				if (triggerId != 0) m_editor->commit(std::move(change));
			});
			form->addRow(tr(names[axis]), spin);
		}
		v->addLayout(form);
		auto* select = new QPushButton(tr("Select the block"));
		select->setObjectName("selectSoundscapeBlock");
		const int triggerId = trigger->id;
		connect(select, &QPushButton::clicked, this, [this, triggerId] {
			Selection selection;
			selection.entities.push_back(triggerId);
			m_editor->setSelection(std::move(selection));
		});
		v->addWidget(select);
	}
	layout->addWidget(box);
}

void PropertiesPanel::buildAddKeyRow(QVBoxLayout* layout, const std::vector<int>& entityIds) {
	auto* row = new QHBoxLayout;
	auto* key = new QLineEdit;
	key->setObjectName("newKey");
	key->setPlaceholderText(tr("new key"));
	auto* value = new QLineEdit;
	value->setObjectName("newValue");
	value->setPlaceholderText(tr("value"));
	auto* add = new QPushButton(tr("+ Add"));
	add->setToolTip(tr("Add (or set) a key - also ones the FGD does not list"));
	const auto apply = [this, key, value, entityIds] {
		const QString name = key->text().trimmed();
		if (name.isEmpty() || name.contains(' ') || name.contains('"')) {
			key->setFocus();
			return;
		}
		setKey(entityIds, name.toStdString(), value->text());
	};
	connect(add, &QPushButton::clicked, this, apply);
	connect(value, &QLineEdit::returnPressed, this, apply);
	connect(key, &QLineEdit::returnPressed, value, qOverload<>(&QWidget::setFocus));
	row->addWidget(key, 2);
	row->addWidget(value, 3);
	row->addWidget(add);
	layout->addLayout(row);
}

void PropertiesPanel::buildRawKeys(QVBoxLayout* layout, const std::vector<int>& entityIds, const EntityClass* definition) {
	const auto& doc = m_editor->doc();
	const Entity* first = doc.findEntity(entityIds.front());
	auto* box = new QGroupBox(tr("Keys and values"));
	auto* v = new QVBoxLayout(box);
	auto* table = new QTableWidget;
	table->setObjectName("rawKeys");
	table->setColumnCount(2);
	table->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
	table->horizontalHeader()->setStretchLastSection(true);
	table->verticalHeader()->setVisible(false);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	// Keys of the entity in their order, then FGD keys it does not have yet (grey: default).
	std::vector<std::pair<std::string, bool>> rows;
	std::set<std::string> seen;
	for (const auto& [key, value] : first->keyValues) {
		if (iequals(key, "id") || !seen.insert(toLower(key)).second) continue;
		rows.emplace_back(key, true);
	}
	if (entityIds.front() != 0 && !seen.contains("targetname")) {
		rows.emplace_back("targetname", false);
		seen.insert("targetname");
	}
	if (definition) {
		for (const auto& property : definition->properties) {
			if (seen.insert(toLower(property.name)).second) rows.emplace_back(property.name, false);
		}
	}
	std::erase_if(rows, [&](const auto& row) { return !m_filterText.isEmpty() && !qs(row.first).contains(m_filterText, Qt::CaseInsensitive); });
	table->setRowCount(static_cast<int>(rows.size()));
	for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
		const auto& [key, present] = rows[static_cast<std::size_t>(i)];
		auto* keyItem = new QTableWidgetItem(qs(key));
		keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
		QString value;
		bool mixed = false;
		for (const int id : entityIds) {
			if (const auto* e = doc.findEntity(id)) {
				const QString v = qs(e->get(key));
				if (id == entityIds.front()) value = v;
				else if (v != value) mixed = true;
			}
		}
		auto* valueItem = new QTableWidgetItem(mixed ? QString() : value);
		if (!present) {
			const EntityProperty* property = definition ? definition->property(key) : nullptr;
			keyItem->setForeground(QColor(140, 146, 160));
			valueItem->setToolTip(property && !property->defaultValue.empty() ? tr("Not set - default: %1").arg(qs(property->defaultValue)) : tr("Not set"));
		}
		if (const EntityProperty* property = definition ? definition->property(key) : nullptr) {
			keyItem->setToolTip(QStringLiteral("<b>%1</b> (%2)<br>%3").arg(qs(property->displayName), qs(property->type), qs(property->description).toHtmlEscaped()));
		}
		table->setItem(i, 0, keyItem);
		table->setItem(i, 1, valueItem);
	}
	table->resizeColumnToContents(0);
	table->setMinimumHeight(std::min(520, 30 + 24 * std::max(4, table->rowCount())));
	connect(table, &QTableWidget::itemChanged, this, [this, table, entityIds](QTableWidgetItem* item) {
		if (item->column() != 1) return;
		const QString key = table->item(item->row(), 0)->text();
		const auto* e = m_editor->doc().findEntity(entityIds.front());
		if (e && qs(e->get(key.toStdString())) == item->text() && entityIds.size() == 1) return;
		setKey(entityIds, key.toStdString(), item->text());
	});
	onStandardKey(table, QKeySequence::Copy, [table] {
		// Selected rows as "key" "value" lines (paste them into another entity or map).
		QStringList lines;
		std::set<int> rowsDone;
		for (const auto* item : table->selectedItems()) {
			if (!rowsDone.insert(item->row()).second) continue;
			lines << QStringLiteral("\"%1\" \"%2\"").arg(table->item(item->row(), 0)->text(), table->item(item->row(), 1)->text());
		}
		if (!lines.isEmpty()) QApplication::clipboard()->setText(lines.join('\n'));
	});
	onStandardKey(table, QKeySequence::Paste, [this, table, entityIds] {
		const QString text = QApplication::clipboard()->text();
		const auto keys = parseKeyValueLines(text);
		if (!keys.empty() && (keys.size() > 1 || text.contains('"') || text.contains('\t'))) {
			pasteKeys(entityIds, keys);
		} else if (table->currentRow() >= 0) {
			// A plain value goes into the selected row.
			setKey(entityIds, table->item(table->currentRow(), 0)->text().toStdString(), text.trimmed());
		}
	});
	v->addWidget(table);
	auto* remove = new QPushButton(tr("− Remove selected key"));
	connect(remove, &QPushButton::clicked, this, [this, table, entityIds] {
		const int row = table->currentRow();
		if (row < 0) return;
		const std::string key = table->item(row, 0)->text().toStdString();
		if (iequals(key, "classname")) return;   // an entity needs its class
		m_editor->commit(edit::setKeyValue(m_editor->doc(), entityIds, key, std::nullopt));
	});
	v->addWidget(remove);
	buildAddKeyRow(v, entityIds);
	layout->addWidget(box);
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
	auto* v = layout;
	v->setSpacing(6);
	if (entity->connections.empty()) {
		v->addWidget(descriptionLabel(tr("No outputs yet. Outputs tell other entities what to do when something happens here "
		                                 "(OnTrigger -> door Open).")));
	}

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
		card->setObjectName(QStringLiteral("outputCard:%1").arg(index));
		card->setFrameShape(QFrame::StyledPanel);
		card->setProperty("role", "card");
		if (int(index) == m_focusConnection) card->setProperty("highlight", true);
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

	auto* addRow = new QHBoxLayout;
	auto* pick = new QPushButton(tr("Connect to ... (click it in the view)"));
	pick->setObjectName("connectPick");
	pick->setToolTip(tr("Click the entity this one should trigger: the likely output and input are suggested, the target gets a name if it has none"));
	connect(pick, &QPushButton::clicked, this, [this, entityId] { m_editor->beginConnect(entityId); });
	auto* add = new QPushButton(tr("+ Add output"));
	connect(add, &QPushButton::clicked, this, [connections, definition, commit] {
		Connection c;
		c.output = definition && !definition->outputs.empty() ? definition->outputs.front().name : "OnUser1";
		connections->push_back(c);
		commit();
	});
	addRow->addWidget(pick, 1);
	addRow->addWidget(add);
	v->addLayout(addRow);
}

int PropertiesPanel::incomingCount(const Entity& entity) const {
	const auto name = entity.get("targetname");
	if (name.empty()) return 0;
	int count = 0;
	for (const auto& other : m_editor->doc().entities) {
		for (const auto& c : other.connections) {
			if (iequals(c.target, name)) ++count;
		}
	}
	return count;
}

void PropertiesPanel::buildIncoming(QVBoxLayout* layout, const Entity& entity) {
	const auto name = entity.get("targetname");
	layout->setSpacing(4);
	if (name.empty()) {
		layout->addWidget(descriptionLabel(tr("Give this entity a name: other entities' outputs reach it by its name.")));
		return;
	}
	int rows = 0;
	for (const auto& other : m_editor->doc().entities) {
		for (const auto& c : other.connections) {
			if (!iequals(c.target, name)) continue;
			const auto otherName = other.get("targetname");
			auto* row = new QFrame;
			row->setProperty("role", "card");
			auto* h = new QHBoxLayout(row);
			h->setContentsMargins(8, 4, 4, 4);
			auto* text = new QLabel(QStringLiteral("%1 <b>%2</b> → %3%4%5")
			                            .arg(qs(otherName.empty() ? other.classname() : otherName).toHtmlEscaped(), qs(c.output).toHtmlEscaped(),
			                                 qs(c.input).toHtmlEscaped(),
			                                 c.parameter.empty() ? QString() : QStringLiteral(" (%1)").arg(qs(c.parameter).toHtmlEscaped()),
			                                 c.delay > 0 ? tr(" after %1 s").arg(c.delay) : QString()));
			text->setWordWrap(true);
			auto* go = new QToolButton;
			go->setText(tr("Go to"));
			go->setToolTip(tr("Select the entity that sends this"));
			const int otherId = other.id;
			connect(go, &QToolButton::clicked, this, [this, otherId] {
				Selection selection;
				selection.entities.push_back(otherId);
				m_editor->setSelection(std::move(selection));
			});
			h->addWidget(text, 1);
			h->addWidget(go);
			layout->addWidget(row);
			++rows;
		}
	}
	if (rows == 0) {
		layout->addWidget(descriptionLabel(tr("No other entity sends anything to \"%1\" yet.").arg(qs(name))));
	}
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
	material->setObjectName("faceMaterial");
	attachAssetMenu(material, AssetKind::Material);
	auto* useCurrent = new QPushButton(tr("Apply current"));
	useCurrent->setToolTip(tr("Apply the material selected in the browser (Shift+T)"));
	materialRow->addWidget(material, 1);
	materialRow->addWidget(useCurrent);
	auto* editMaterial = new QToolButton;
	editMaterial->setText(tr("Edit"));
	editMaterial->setToolTip(tr("Open this material in the Material Editor"));
	const QString materialName = qs(first->material);
	connect(editMaterial, &QToolButton::clicked, this, [this, materialName] { emit editMaterialRequested(materialName); });
	materialRow->addWidget(editMaterial);
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
	addButton(tr("Reset"), tr("World align with scale 0.25, no shift"), 1, 1, [modify] { modify("Reset texture", [](Side& s) { alignToWorld(s, 0.25); }); });
	layout->addLayout(buttons);

	// Justify like Hammer's face edit sheet; "treat as one" spans one texture over all selected faces.
	auto* justifyBox = new QGroupBox(tr("Justify"));
	auto* jv = new QVBoxLayout(justifyBox);
	auto* asOne = new QCheckBox(tr("Treat as one face"));
	asOne->setToolTip(tr("The selected faces are textured as one surface: one texture spans all of them, seamless where they meet"));
	asOne->setChecked(faces.size() > 1 && QSettings().value("faces/treatAsOne", true).toBool());
	connect(asOne, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("faces/treatAsOne", on); });
	jv->addWidget(asOne);
	auto* jg = new QGridLayout;
	const auto textureSize = [this](const std::string& material) {
		if (const auto info = m_materials->get(qs(material)); info && !info->image.isNull()) {
			return std::pair<int, int>{info->image.width(), info->image.height()};
		}
		return std::pair<int, int>{512, 512};
	};
	const std::pair<edit::Justify, QString> modes[] = {{edit::Justify::Fit, tr("Fit")},       {edit::Justify::Left, tr("Left")},
	                                                   {edit::Justify::Right, tr("Right")},   {edit::Justify::Top, tr("Top")},
	                                                   {edit::Justify::Bottom, tr("Bottom")}, {edit::Justify::Center, tr("Center")}};
	int index = 0;
	for (const auto& [mode, label] : modes) {
		auto* b = new QPushButton(label);
		b->setToolTip(mode == edit::Justify::Fit ? tr("Stretch the texture exactly once over the face (or all faces as one)")
		                                         : tr("Move the texture so its edge lines up with the face's edge"));
		connect(b, &QPushButton::clicked, this, [this, faces, mode, asOne, textureSize] {
			m_editor->commit(edit::justifyFaces(m_editor->doc(), faces, mode, asOne->isChecked(), textureSize));
		});
		jg->addWidget(b, index / 3, index % 3);
		++index;
	}
	jv->addLayout(jg);
	layout->addWidget(justifyBox);

	if (faces.size() == 2 && faces[0].solid == faces[1].solid) {
		// Two faces of one brush: the edge between them can be rounded or bevelled.
		auto* edgeBox = new QGroupBox(tr("Edge between the two faces"));
		auto* ef = new QFormLayout(edgeBox);
		auto* radius = new QDoubleSpinBox;
		radius->setRange(1, 4096);
		radius->setSuffix(tr(" units"));
		radius->setValue(QSettings().value("brushes/roundRadius", 16.0).toDouble());
		auto* segments = new QSpinBox;
		segments->setRange(1, 32);
		segments->setValue(QSettings().value("brushes/roundSegments", 4).toInt());
		ef->addRow(tr("Radius"), radius);
		ef->addRow(tr("Segments"), segments);
		auto* row = new QHBoxLayout;
		auto* roundButton = new QPushButton(tr("Round"));
		auto* bevelButton = new QPushButton(tr("Bevel"));
		row->addWidget(roundButton);
		row->addWidget(bevelButton);
		ef->addRow(row);
		connect(roundButton, &QPushButton::clicked, this, [this, radius, segments] {
			QSettings().setValue("brushes/roundRadius", radius->value());
			QSettings().setValue("brushes/roundSegments", segments->value());
			m_editor->roundSelectedEdge(radius->value(), std::max(2, segments->value()));
		});
		connect(bevelButton, &QPushButton::clicked, this, [this, radius] { m_editor->roundSelectedEdge(radius->value(), 1); });
		layout->addWidget(edgeBox);
	}
	layout->addWidget(descriptionLabel(tr("Fast texturing: Ctrl+right mouse paints the current material over faces. Double-click a face selects the "
	                                      "whole wall. Shift+right click (or Ctrl+M) picks a material. Drag materials from the browser onto faces.")));
}

void PropertiesPanel::buildBrushes(QVBoxLayout* layout) {
	const auto& doc = m_editor->doc();
	const auto& selection = m_editor->selection();
	auto* title = new QLabel(tr("%n brush(es)", nullptr, int(selection.solids.size())));
	title->setObjectName("entityClass");
	layout->addWidget(title);
	const auto box = edit::selectionBounds(doc, selection, m_editor->entities());
	if (!box.empty()) {
		const auto size = box.size();
		layout->addWidget(descriptionLabel(tr("%1 × %2 × %3 units").arg(size.x).arg(size.y).arg(size.z)));
	}
	// Its materials, most used first (like the face list of Source 2's object properties).
	std::map<std::string, int> used;
	for (const auto& ref : selection.solids) {
		if (const auto* solid = doc.findSolid(ref)) {
			for (const auto& side : solid->sides) ++used[side.material];
		}
	}
	std::vector<std::pair<int, std::string>> ranked;
	for (const auto& [material, count] : used) ranked.push_back({-count, material});
	std::sort(ranked.begin(), ranked.end());
	auto* materials = new QGroupBox(tr("Materials"));
	auto* mv = new QVBoxLayout(materials);
	for (std::size_t i = 0; i < ranked.size() && i < 8; ++i) {
		auto* row = new QLabel(QStringLiteral("%1  <span style='color:#8a919c'>%2</span>").arg(qs(ranked[i].second)).arg(tr("%n face(s)", nullptr, -ranked[i].first)));
		row->setTextInteractionFlags(Qt::TextSelectableByMouse);
		row->setObjectName("brushMaterial");
		attachAssetMenu(row, {AssetKind::Material, normalizedMaterial(ranked[i].second)});
		mv->addWidget(row);
	}
	auto* apply = new QPushButton(tr("Apply current material to all faces (Shift+T)"));
	connect(apply, &QPushButton::clicked, this, [this] { m_editor->applyMaterialToSelection(m_editor->currentMaterial()); });
	mv->addWidget(apply);
	layout->addWidget(materials);
	layout->addWidget(descriptionLabel(tr("Hollow, Carve, Merge, Tie to entity and more: Tool panel on the left (Select tool). Faces mode (3) edits single faces.")));
}

} // namespace sme::app
