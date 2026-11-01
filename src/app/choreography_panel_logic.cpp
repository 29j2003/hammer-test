// choreography_panel_logic.cpp - The Choreography dock's scenes list (grouped by NPC, search, new,
// copy, rename, delete, select in map) and the Starts, Conditions and Variables tabs plus the
// "Set variable" step row (see choreography_panel.h; the logic itself is sme/choreography.h).

#include "choreography_panel.h"

#include "editor.h"
#include "icons.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <set>

#include <sme/animation_use.h>
#include <sme/text.h>

namespace sme::app {

namespace {

constexpr int SCENE_ROLE = Qt::UserRole;        ///< Scene items: the scene's id (0 for NPC groups)
constexpr int ACTOR_ROLE = Qt::UserRole + 1;    ///< The NPC the item is listed under

QWidget* line(std::initializer_list<QWidget*> widgets, bool stretch = true) {
	auto* box = new QWidget;
	auto* layout = new QHBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);
	for (auto* widget : widgets) layout->addWidget(widget, !stretch && qobject_cast<QComboBox*>(widget) ? 1 : 0);   // fields take the room
	if (stretch) layout->addStretch(1);
	return box;
}

QDoubleSpinBox* spin(const char* objectName, double max, double step, const QString& suffix) {
	auto* box = new QDoubleSpinBox;
	box->setObjectName(QLatin1String(objectName));
	box->setRange(0, max);
	box->setDecimals(1);
	box->setSingleStep(step);
	box->setSuffix(suffix);
	box->setKeyboardTracking(false);
	return box;
}

QString startKindText(ChoreoStart::Kind kind) {
	switch (kind) {
		case ChoreoStart::Kind::MapStart: return ChoreographyPanel::tr("At map start");
		case ChoreoStart::Kind::Area: return ChoreographyPanel::tr("Player enters an area");
		case ChoreoStart::Kind::Use: return ChoreographyPanel::tr("Player uses an entity");
		case ChoreoStart::Kind::LookAt: return ChoreographyPanel::tr("Player looks at an entity");
		case ChoreoStart::Kind::Near: return ChoreographyPanel::tr("Player comes near the NPC");
		case ChoreoStart::Kind::AfterScene: return ChoreographyPanel::tr("After another scene");
		case ChoreoStart::Kind::Output: return ChoreographyPanel::tr("Output of another entity");
		case ChoreoStart::Kind::Timer: return ChoreographyPanel::tr("Timer");
		case ChoreoStart::Kind::Input: return ChoreographyPanel::tr("Only by input (your own I/O)");
	}
	return {};
}

QString conditionKindText(ChoreoCondition::Kind kind) {
	switch (kind) {
		case ChoreoCondition::Kind::ScenePlayed: return ChoreographyPanel::tr("Scene has played");
		case ChoreoCondition::Kind::SceneNotPlayed: return ChoreographyPanel::tr("Scene has not played yet");
		case ChoreoCondition::Kind::Variable: return ChoreographyPanel::tr("Variable");
		case ChoreoCondition::Kind::Global: return ChoreographyPanel::tr("Global state (counter)");
		case ChoreoCondition::Kind::Random: return ChoreographyPanel::tr("Chance");
	}
	return {};
}

/// Global states of Half-Life 2 a mapper is likely to test (any name works).
const char* const GLOBAL_STATES[] = {"gordon_precriminal", "gordon_invulnerable", "super_phys_gun", "friendly_encounter", "antlion_allied",
                                     "citizens_passive",   "suit_no_sprint",      "ep_alyx_darknessmode", "hunters_to_run_over"};

/// The generated entity of a scene's start (for "Select box"), else the entity the start listens to.
std::string startEntityName(const ChoreoScene& scene, std::size_t index) {
	const auto& start = scene.starts[index];
	const std::string number = std::to_string(index + 1);
	switch (start.kind) {
		case ChoreoStart::Kind::MapStart: return scene.name + "_auto" + number;
		case ChoreoStart::Kind::Area: return scene.name + "_area" + number;
		case ChoreoStart::Kind::LookAt: return scene.name + "_look" + number;
		case ChoreoStart::Kind::Near: return scene.name + "_near" + number;
		case ChoreoStart::Kind::Timer: return scene.name + "_timer" + number;
		case ChoreoStart::Kind::Use:
		case ChoreoStart::Kind::Output: {
			if (!trim(start.target).empty()) return start.target;
			const auto actors = choreoActors(scene);
			return actors.empty() ? std::string() : actors.front();
		}
		case ChoreoStart::Kind::AfterScene: return start.target;
		case ChoreoStart::Kind::Input: return scene.name + "_start";
	}
	return {};
}

} // namespace

// --- Scenes list ------------------------------------------------------------------------------

QWidget* ChoreographyPanel::buildManager() {
	auto* box = new QWidget;
	auto* layout = new QVBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	m_filter = new QLineEdit;
	m_filter->setObjectName("choreoSceneFilter");
	m_filter->setPlaceholderText(tr("Search scenes, NPCs, starts, conditions ..."));
	m_filter->setClearButtonEnabled(true);
	layout->addWidget(m_filter);

	m_tree = new QTreeWidget;
	m_tree->setObjectName("choreoSceneTree");
	m_tree->setColumnCount(4);
	m_tree->setHeaderLabels({tr("Scene"), tr("Starts"), tr("Conditions"), tr("State")});
	m_tree->setRootIsDecorated(true);
	m_tree->setUniformRowHeights(true);
	m_tree->setMinimumHeight(140);
	m_tree->setMaximumHeight(260);
	m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
	m_tree->header()->setStretchLastSection(false);
	m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
	m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	m_tree->setToolTip(tr("Every scene of the map under each NPC in it (a scene with two NPCs is listed under both). "
	                      "Click to edit it, F2 or click again to rename."));
	layout->addWidget(m_tree);

	m_newScene = new QPushButton(tr("New"));
	m_newScene->setObjectName("choreoNewScene");
	m_newScene->setToolTip(tr("A new scene for the selected NPC (the first one starts at map start, the next ones when the player walks into an area)"));
	m_copyScene = new QPushButton(tr("Copy"));
	m_copyScene->setObjectName("choreoCopyScene");
	m_copyScene->setToolTip(tr("A copy of the scene with its steps, starts and conditions (name_2), its trigger boxes where the original's are"));
	m_deleteScene = new QPushButton(tr("Delete"));
	m_deleteScene->setObjectName("choreoDeleteScene");
	m_deleteScene->setToolTip(tr("Removes the scene with its points, triggers and logic (Undo brings it back)"));
	m_selectScene = new QPushButton(tr("Select in map"));
	m_selectScene->setObjectName("choreoSelectScene");
	m_selectScene->setToolTip(tr("Selects the scene's NPC and its trigger boxes and shows them in the map views"));
	auto* buttons = new QHBoxLayout;
	buttons->setContentsMargins(0, 0, 0, 0);
	for (auto* button : {m_newScene, m_copyScene, m_deleteScene, m_selectScene}) buttons->addWidget(button);
	buttons->addStretch(1);
	layout->addLayout(buttons);

	connect(m_filter, &QLineEdit::textChanged, this, [this] { refreshManager(); });
	connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
		if (!item || m_filling) return;
		const int id = item->data(0, SCENE_ROLE).toInt();
		const std::string actor = item->data(0, ACTOR_ROLE).toString().toStdString();
		if (!actor.empty()) m_actor = actor;
		if (id != 0 && id != m_sceneId) showScene(id);
	});
	connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
		if (m_filling || column != 0 || !item) return;
		const int id = item->data(0, SCENE_ROLE).toInt();
		const Entity* entity = id ? m_editor->doc().findEntity(id) : nullptr;
		if (!entity) return;
		const std::string name = item->text(0).trimmed().toStdString();
		ChoreoScene scene = readChoreoScene(m_editor->doc(), *entity);
		if (name.empty() || name == scene.name) {
			QSignalBlocker block(m_tree);
			item->setText(0, qs(scene.name));
			return;
		}
		if (const Entity* other = findNamedEntity(m_editor->doc(), name); other && other->id != id) {
			emit m_editor->message(tr("Another entity is called %1 already").arg(qs(name)));
			QTimer::singleShot(0, this, &ChoreographyPanel::refresh);
			return;
		}
		scene.name = name;
		m_sceneId = id;
		QTimer::singleShot(0, this, [this, scene] { apply(scene, "Rename scene"); });
	});
	connect(m_copyScene, &QPushButton::clicked, this, [this] {
		if (m_sceneId == 0) return;
		stopPreview();
		auto change = edit::duplicateChoreoScene(m_editor->doc(), m_sceneId, m_editor->mapName().toStdString());
		int created = 0;
		for (const auto& state : change.entities) {
			if (!state.before && state.after && isChoreoScene(*state.after)) created = state.entityId;
		}
		m_editor->commit(std::move(change));
		if (created) m_sceneId = created;
		m_step = -1;
		if (QString error; !m_editor->writeSceneFiles(&error)) emit m_editor->message(tr("Could not write the scene file %1").arg(error));
		refresh();
	});
	connect(m_selectScene, &QPushButton::clicked, this, &ChoreographyPanel::selectSceneInMap);
	return box;
}

void ChoreographyPanel::showScene(int sceneId) {
	stopPreview();
	const Entity* entity = m_editor->doc().findEntity(sceneId);
	if (!entity || !isChoreoScene(*entity)) return;
	m_sceneId = sceneId;
	m_step = -1;
	m_start = m_condition = -1;
	// Its NPC gets selected, so the views show the scene.
	const auto scene = readChoreoScene(m_editor->doc(), *entity);
	const auto actors = choreoActors(scene);
	std::string actor = actors.empty() ? std::string() : actors.front();
	if (std::any_of(actors.begin(), actors.end(), [this](const std::string& a) { return iequals(a, m_actor); })) actor = m_actor;
	Selection selection;
	if (const Entity* npc = findNamedEntity(m_editor->doc(), actor)) selection.entities.push_back(npc->id);
	else selection.entities.push_back(sceneId);
	m_editor->setSelection(std::move(selection));
	refresh();
}

void ChoreographyPanel::selectSceneInMap() {
	const auto scene = currentScene();
	if (!scene) return;
	Selection selection;
	for (const auto& actor : choreoActors(*scene)) {
		if (const Entity* npc = findNamedEntity(m_editor->doc(), actor)) selection.entities.push_back(npc->id);
	}
	for (const auto& entity : m_editor->doc().entities) {
		if (iequals(entity.get(CHOREO_SCENE_KEY), scene->name) && !entity.solids.empty()) selection.entities.push_back(entity.id);
	}
	if (selection.entities.empty()) selection.entities.push_back(m_sceneId);
	m_editor->setSelection(std::move(selection));
	emit frameSelectionRequested();
}

QString ChoreographyPanel::sceneState(int sceneId, const ChoreoScene& scene, const std::vector<ChoreoLogicIssue>& logic) const {
	QStringList problems;
	for (const auto& issue : checkChoreoScene(m_editor->doc(), scene)) problems << qs(issue.message);
	for (const auto& issue : logic) {
		if (issue.sceneId == sceneId) problems << qs(issue.message);
	}
	if (!problems.isEmpty()) return problems.join('\n');
	return {};
}

void ChoreographyPanel::refreshManager() {
	const auto& doc = m_editor->doc();
	const bool wasFilling = m_filling;
	m_filling = true;
	const auto logic = checkChoreoLogic(doc);
	const QString filter = m_filter->text().trimmed();
	// The NPC selected in the map: its group is opened, the others closed.
	std::string selectedActor;
	for (const int id : m_editor->selection().entities) {
		const Entity* entity = doc.findEntity(id);
		if (entity && isNpcClass(entity->classname()) && !entity->get("targetname").empty()) selectedActor = entity->get("targetname");
	}
	std::set<QString> closed;
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		if (!m_tree->topLevelItem(i)->isExpanded()) closed.insert(m_tree->topLevelItem(i)->data(0, ACTOR_ROLE).toString().toLower());
	}
	QSignalBlocker block(m_tree);
	m_tree->clear();
	std::map<std::string, QTreeWidgetItem*> groups;
	QTreeWidgetItem* current = nullptr;
	for (const int id : choreoScenes(doc)) {
		const Entity* entity = doc.findEntity(id);
		const ChoreoScene scene = readChoreoScene(doc, *entity);
		QStringList starts, conditions;
		for (const auto& start : scene.starts) starts << qs(describeChoreoStart(start));
		for (const auto& condition : scene.conditions) conditions << qs(describeChoreoCondition(condition));
		const QString startText = starts.isEmpty() ? tr("only by input") : starts.join(tr(" or "));
		const QString conditionText = conditions.join(tr(" and "));
		const QString problems = sceneState(id, scene, logic);
		auto actors = choreoActors(scene);
		if (!filter.isEmpty()) {
			QString haystack = qs(scene.name) + ' ' + startText + ' ' + conditionText;
			for (const auto& actor : actors) haystack += ' ' + qs(actor);
			if (!haystack.contains(filter, Qt::CaseInsensitive)) continue;
		}
		if (actors.empty()) actors.push_back({});
		for (const auto& actor : actors) {
			auto& group = groups[toLower(actor)];
			if (!group) {
				group = new QTreeWidgetItem(m_tree);
				group->setData(0, SCENE_ROLE, 0);
				group->setData(0, ACTOR_ROLE, qs(actor));
				group->setIcon(0, icon("tool:entity"));
				group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
			}
			auto* item = new QTreeWidgetItem(group);
			item->setText(0, qs(scene.name));
			item->setText(1, startText);
			item->setText(2, conditionText.isEmpty() ? tr("-") : conditionText);
			item->setText(3, scene.steps.empty() ? tr("no steps") : problems.isEmpty() ? tr("ok") : tr("⚠ %n problem(s)", nullptr, int(problems.count('\n') + 1)));
			item->setToolTip(1, startText);
			item->setToolTip(2, conditionText);
			item->setToolTip(3, problems);
			item->setData(0, SCENE_ROLE, id);
			item->setData(0, ACTOR_ROLE, qs(actor));
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
			if (!problems.isEmpty()) item->setForeground(3, QColor(255, 180, 106));
			if (id == m_sceneId && (!current || iequals(actor, m_actor))) current = item;
		}
	}
	for (auto& [key, group] : groups) {
		const QString actor = group->data(0, ACTOR_ROLE).toString();
		group->setText(0, (actor.isEmpty() ? tr("(no NPC yet)") : actor) + QStringLiteral("  (%1)").arg(group->childCount()));
		bool open = !closed.contains(actor.toLower());
		if (!selectedActor.empty()) open = iequals(actor.toStdString(), selectedActor);
		if (!filter.isEmpty()) open = true;
		group->setExpanded(open);
	}
	if (current) {
		if (current->parent()) current->parent()->setExpanded(true);
		m_tree->setCurrentItem(current);
		m_tree->scrollToItem(current);
	}
	m_filling = wasFilling;
}

QStringList ChoreographyPanel::entityNames() const {
	QStringList names;
	for (const auto& entity : m_editor->doc().entities) {
		const std::string name = entity.get("targetname");
		if (name.empty() || isChoreoHelper(entity)) continue;
		names << qs(name);
	}
	names.removeDuplicates();
	names.sort(Qt::CaseInsensitive);
	return names;
}

// --- Starts -----------------------------------------------------------------------------------

QWidget* ChoreographyPanel::buildStartsPage() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 6, 0, 0);
	layout->setSpacing(6);
	layout->addWidget(noteLabel(tr("Any of these starts the scene - when every condition holds.")));
	m_starts = new QListWidget;
	m_starts->setObjectName("choreoStarts");
	m_starts->setMaximumHeight(130);
	layout->addWidget(m_starts);

	m_addStart = new QPushButton(tr("Add start"));
	m_addStart->setObjectName("choreoAddStart");
	auto* menu = new QMenu(m_addStart);
	menu->setObjectName("choreoAddStartMenu");
	for (const auto kind : {ChoreoStart::Kind::MapStart, ChoreoStart::Kind::Area, ChoreoStart::Kind::Use, ChoreoStart::Kind::LookAt, ChoreoStart::Kind::Near,
	                        ChoreoStart::Kind::AfterScene, ChoreoStart::Kind::Output, ChoreoStart::Kind::Timer, ChoreoStart::Kind::Input}) {
		auto* action = menu->addAction(startKindText(kind));
		action->setData(int(kind));
		connect(action, &QAction::triggered, this, [this, kind] { addStart(kind); });
	}
	m_addStart->setMenu(menu);
	m_removeStart = new QPushButton(tr("Remove"));
	m_removeStart->setObjectName("choreoRemoveStart");
	layout->addWidget(line({m_addStart, m_removeStart}));

	m_startBox = new QWidget;
	auto* box = new QVBoxLayout(m_startBox);
	box->setContentsMargins(0, 0, 0, 0);
	m_startKind = new QLabel;
	m_startKind->setObjectName("choreoStartKind");
	m_startKind->setStyleSheet(QStringLiteral("font-weight: bold;"));
	box->addWidget(m_startKind);
	m_startTarget = new QComboBox;
	m_startTarget->setObjectName("choreoStartTarget");
	m_startTarget->setEditable(true);
	m_startTarget->setMinimumContentsLength(14);
	m_startTargetRow = line({new QLabel(tr("Entity")), m_startTarget}, false);
	box->addWidget(m_startTargetRow);
	m_startOutput = new QComboBox;
	m_startOutput->setObjectName("choreoStartOutput");
	m_startOutput->setEditable(true);
	m_startOutput->setMinimumContentsLength(14);
	m_startOutputRow = line({new QLabel(tr("Output")), m_startOutput}, false);
	box->addWidget(m_startOutputRow);
	m_startOnce = new QCheckBox(tr("Only once"));
	m_startOnce->setObjectName("choreoStartOnce");
	m_startOnce->setToolTip(tr("On: this start works until the scene has played. Off: every time (the scene never restarts while it plays)."));
	m_startDelay = spin("choreoStartDelay", 3600, 0.5, tr(" s"));
	m_startDelay->setToolTip(tr("Seconds from the start firing to the scene starting"));
	box->addWidget(line({m_startOnce, new QLabel(tr("Delay")), m_startDelay}));
	m_startALabel = new QLabel;
	m_startA = spin("choreoStartA", 100000, 1, QString());
	m_startBLabel = new QLabel(tr("up to"));
	m_startB = spin("choreoStartB", 100000, 1, tr(" s"));
	m_startB->setToolTip(tr("Random time between the two (0: always the first)"));
	m_startARow = line({m_startALabel, m_startA, m_startBLabel, m_startB});
	box->addWidget(m_startARow);
	m_startSelect = new QPushButton(tr("Select box"));
	m_startSelect->setObjectName("choreoStartSelect");
	box->addWidget(line({m_startSelect}));
	m_startNote = noteLabel(QString());
	m_startNote->setObjectName("choreoStartNote");
	box->addWidget(m_startNote);
	layout->addWidget(m_startBox);
	layout->addStretch(1);

	connect(m_starts, &QListWidget::currentRowChanged, this, [this](int row) {
		if (m_filling) return;
		m_start = row;
		fillStartEditor();
	});
	connect(m_removeStart, &QPushButton::clicked, this, [this] {
		auto scene = currentScene();
		if (!scene || m_start < 0 || m_start >= int(scene->starts.size())) return;
		scene->starts.erase(scene->starts.begin() + m_start);
		m_start = std::min(m_start, int(scene->starts.size()) - 1);
		apply(*scene, "Remove start");
	});
	const auto takeTarget = [this] {
		const std::string text = m_startTarget->currentText().trimmed().toStdString();
		editStart([&](ChoreoStart& start) { start.target = text; }, "Start entity");
	};
	connect(m_startTarget, &QComboBox::activated, this, takeTarget);
	connect(m_startTarget->lineEdit(), &QLineEdit::editingFinished, this, takeTarget);
	const auto takeOutput = [this] {
		const std::string text = m_startOutput->currentText().trimmed().toStdString();
		editStart([&](ChoreoStart& start) { start.output = text; }, "Start output");
	};
	connect(m_startOutput, &QComboBox::activated, this, takeOutput);
	connect(m_startOutput->lineEdit(), &QLineEdit::editingFinished, this, takeOutput);
	connect(m_startOnce, &QCheckBox::toggled, this, [this](bool on) { editStart([&](ChoreoStart& start) { start.once = on; }, "Start once"); });
	connect(m_startDelay, &QDoubleSpinBox::valueChanged, this, [this](double v) { editStart([&](ChoreoStart& start) { start.delay = v; }, "Start delay"); });
	connect(m_startA, &QDoubleSpinBox::valueChanged, this, [this](double v) { editStart([&](ChoreoStart& start) { start.a = v; }, "Start time"); });
	connect(m_startB, &QDoubleSpinBox::valueChanged, this, [this](double v) { editStart([&](ChoreoStart& start) { start.b = v; }, "Start time"); });
	connect(m_startSelect, &QPushButton::clicked, this, [this] {
		const auto scene = currentScene();
		if (!scene || m_start < 0 || m_start >= int(scene->starts.size())) return;
		const Entity* entity = findNamedEntity(m_editor->doc(), startEntityName(*scene, static_cast<std::size_t>(m_start)));
		if (!entity) return;
		Selection selection;
		selection.entities.push_back(entity->id);
		m_editor->setSelection(std::move(selection));
		emit frameSelectionRequested();
	});
	return page;
}

void ChoreographyPanel::addStart(ChoreoStart::Kind kind) {
	auto scene = currentScene();
	if (!scene) return;
	ChoreoStart start;
	start.kind = kind;
	switch (kind) {
		case ChoreoStart::Kind::Timer: start.a = 10; break;
		case ChoreoStart::Kind::Near: start.a = 128; break;
		case ChoreoStart::Kind::Output: start.output = "OnTrigger"; break;
		case ChoreoStart::Kind::AfterScene:
			for (const int id : choreoScenes(m_editor->doc())) {
				if (id != m_sceneId) {
					start.target = m_editor->doc().findEntity(id)->get("targetname");
					break;
				}
			}
			break;
		default: break;
	}
	scene->starts.push_back(start);
	m_start = int(scene->starts.size()) - 1;
	m_tabs->setCurrentIndex(1);
	apply(*scene, "Add start");
	if (kind == ChoreoStart::Kind::Area || kind == ChoreoStart::Kind::LookAt) {
		emit m_editor->message(tr("A trigger box in front of the NPC: Select box, then move and size it like any brush"));
	}
}

void ChoreographyPanel::editStart(const std::function<void(ChoreoStart&)>& change, const char* undoName) {
	if (m_filling) return;
	auto scene = currentScene();
	if (!scene || m_start < 0 || m_start >= int(scene->starts.size())) return;
	auto& start = scene->starts[static_cast<std::size_t>(m_start)];
	const ChoreoStart before = start;
	change(start);
	if (start == before) return;
	apply(*scene, undoName);
}

void ChoreographyPanel::refreshStarts(const ChoreoScene& scene) {
	const bool wasFilling = m_filling;
	m_filling = true;
	{
		QSignalBlocker block(m_starts);
		m_starts->clear();
		for (const auto& start : scene.starts) m_starts->addItem(qs(describeChoreoStart(start)));
		if (scene.starts.empty()) m_start = -1;
		else m_start = std::clamp(m_start, 0, int(scene.starts.size()) - 1);
		m_starts->setCurrentRow(m_start);
	}
	m_tabs->setTabText(1, scene.starts.empty() ? tr("Starts") : tr("Starts (%1)").arg(scene.starts.size()));
	m_filling = wasFilling;
	fillStartEditor();
}

void ChoreographyPanel::fillStartEditor() {
	const auto scene = currentScene();
	const bool have = scene && m_start >= 0 && m_start < int(scene->starts.size());
	m_startBox->setVisible(have);
	m_removeStart->setEnabled(have);
	if (!have) return;
	const bool wasFilling = m_filling;
	m_filling = true;
	const auto& start = scene->starts[static_cast<std::size_t>(m_start)];
	using Kind = ChoreoStart::Kind;
	m_startKind->setText(startKindText(start.kind));
	const bool target = start.kind == Kind::Use || start.kind == Kind::LookAt || start.kind == Kind::Near || start.kind == Kind::AfterScene || start.kind == Kind::Output;
	m_startTargetRow->setVisible(target);
	if (target) {
		QSignalBlocker block(m_startTarget);
		m_startTarget->clear();
		if (start.kind == Kind::AfterScene) {
			for (const int id : choreoScenes(m_editor->doc())) {
				if (id != m_sceneId) m_startTarget->addItem(qs(m_editor->doc().findEntity(id)->get("targetname")));
			}
		} else {
			m_startTarget->addItems(entityNames());
		}
		m_startTarget->setCurrentText(qs(start.target));
		m_startTarget->lineEdit()->setPlaceholderText(start.kind == Kind::AfterScene ? tr("scene") : tr("(the scene's NPC)"));
	}
	const bool output = start.kind == Kind::Use || start.kind == Kind::Output;
	m_startOutputRow->setVisible(output);
	if (output) {
		QSignalBlocker block(m_startOutput);
		m_startOutput->clear();
		std::string targetName = start.target;
		if (trim(targetName).empty()) {
			const auto actors = choreoActors(*scene);
			if (!actors.empty()) targetName = actors.front();
		}
		const Entity* entity = findNamedEntity(m_editor->doc(), targetName);
		QStringList outputs;
		if (const auto* catalog = m_editor->entities(); catalog && entity) {
			if (const auto* cls = catalog->find(entity->classname())) {
				for (const auto& io : cls->outputs) outputs << qs(io.name);
			}
		}
		if (outputs.isEmpty()) outputs = {"OnTrigger", "OnPressed", "OnPlayerUse", "OnStartTouch", "OnOpen", "OnFullyOpen", "OnBreak", "OnDeath", "OnDamaged", "OnUser1"};
		outputs.removeDuplicates();
		m_startOutput->addItems(outputs);
		m_startOutput->setCurrentText(qs(start.output));
		m_startOutput->lineEdit()->setPlaceholderText(start.kind == Kind::Use && entity ? qs(defaultUseOutput(entity->classname())) : QString());
	}
	{
		QSignalBlocker a(m_startOnce), b(m_startDelay), c(m_startA), d(m_startB);
		m_startOnce->setChecked(start.once);
		m_startOnce->setVisible(start.kind != Kind::MapStart && start.kind != Kind::Input && start.kind != Kind::LookAt);
		m_startDelay->setValue(start.delay);
		m_startDelay->setEnabled(start.kind != Kind::Input);
		m_startA->setValue(start.a);
		m_startB->setValue(start.b);
	}
	m_startARow->setVisible(start.kind == Kind::Timer || start.kind == Kind::Near);
	m_startBLabel->setVisible(start.kind == Kind::Timer);
	m_startB->setVisible(start.kind == Kind::Timer);
	m_startALabel->setText(start.kind == Kind::Timer ? tr("After") : tr("Radius"));
	m_startA->setSuffix(start.kind == Kind::Timer ? tr(" s") : tr(" units"));
	const bool box = start.kind == Kind::Area || start.kind == Kind::LookAt || start.kind == Kind::Near;
	m_startSelect->setText(box ? tr("Select box") : tr("Select entity"));
	m_startSelect->setVisible(start.kind != Kind::Input && start.kind != Kind::MapStart && start.kind != Kind::Timer);
	QString note;
	switch (start.kind) {
		case Kind::MapStart: note = tr("When the map has loaded (a loaded save game does not start it again)."); break;
		case Kind::Area: note = tr("A trigger box (tools/toolstrigger) in front of the NPC: select it and move or size it like any brush."); break;
		case Kind::Use: note = tr("The player presses Use on the entity (empty: the scene's NPC; buttons fire OnPressed)."); break;
		case Kind::LookAt: note = tr("The player stands in the box and looks at the entity for half a second (trigger_look, fires once)."); break;
		case Kind::Near: note = tr("A box around the NPC that moves with it (parented trigger)."); break;
		case Kind::AfterScene: note = tr("When the other scene has finished."); break;
		case Kind::Output: note = tr("Any output of any entity - a door's OnFullyOpen, a breakable's OnBreak, a counter's OnHitMax ..."); break;
		case Kind::Timer: note = tr("Counts from map start (random between the two times when the second is larger)."); break;
		case Kind::Input:
			note = tr("Nothing is made: fire Trigger on %1_start from your own I/O (the conditions apply), or Start on %1 (skips them).").arg(qs(scene->name));
			break;
	}
	m_startNote->setText(note);
	m_filling = wasFilling;
}

// --- Conditions -------------------------------------------------------------------------------

QWidget* ChoreographyPanel::buildConditionsPage() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 6, 0, 0);
	layout->setSpacing(6);
	layout->addWidget(noteLabel(tr("All of these must hold when a start fires, else nothing happens (an area keeps checking while the player is inside).")));
	m_conditions = new QListWidget;
	m_conditions->setObjectName("choreoConditions");
	m_conditions->setMaximumHeight(130);
	layout->addWidget(m_conditions);
	m_addCondition = new QPushButton(tr("Add condition"));
	m_addCondition->setObjectName("choreoAddCondition");
	auto* menu = new QMenu(m_addCondition);
	menu->setObjectName("choreoAddConditionMenu");
	for (const auto kind : {ChoreoCondition::Kind::ScenePlayed, ChoreoCondition::Kind::SceneNotPlayed, ChoreoCondition::Kind::Variable, ChoreoCondition::Kind::Global,
	                        ChoreoCondition::Kind::Random}) {
		auto* action = menu->addAction(conditionKindText(kind));
		action->setData(int(kind));
		connect(action, &QAction::triggered, this, [this, kind] { addCondition(kind); });
	}
	m_addCondition->setMenu(menu);
	m_removeCondition = new QPushButton(tr("Remove"));
	m_removeCondition->setObjectName("choreoRemoveCondition");
	layout->addWidget(line({m_addCondition, m_removeCondition}));

	m_conditionBox = new QWidget;
	auto* box = new QVBoxLayout(m_conditionBox);
	box->setContentsMargins(0, 0, 0, 0);
	m_conditionKind = new QLabel;
	m_conditionKind->setStyleSheet(QStringLiteral("font-weight: bold;"));
	box->addWidget(m_conditionKind);
	m_conditionName = new QComboBox;
	m_conditionName->setObjectName("choreoConditionName");
	m_conditionName->setEditable(true);
	m_conditionName->setMinimumContentsLength(14);
	m_conditionNameRow = line({new QLabel(tr("Name")), m_conditionName}, false);
	box->addWidget(m_conditionNameRow);
	m_conditionCompare = new QComboBox;
	m_conditionCompare->setObjectName("choreoConditionCompare");
	m_conditionCompare->addItem(QStringLiteral("="), int(ChoreoCondition::Compare::Equal));
	m_conditionCompare->addItem(QStringLiteral("≠"), int(ChoreoCondition::Compare::NotEqual));
	m_conditionCompare->addItem(QStringLiteral(">"), int(ChoreoCondition::Compare::Greater));
	m_conditionCompare->addItem(QStringLiteral("<"), int(ChoreoCondition::Compare::Less));
	m_conditionValue = new QDoubleSpinBox;
	m_conditionValue->setObjectName("choreoConditionValue");
	m_conditionValue->setRange(-1000000, 1000000);
	m_conditionValue->setDecimals(2);
	m_conditionValue->setKeyboardTracking(false);
	m_conditionCompareRow = line({m_conditionCompare, m_conditionValue});
	box->addWidget(m_conditionCompareRow);
	layout->addWidget(m_conditionBox);
	layout->addStretch(1);

	connect(m_conditions, &QListWidget::currentRowChanged, this, [this](int row) {
		if (m_filling) return;
		m_condition = row;
		fillConditionEditor();
	});
	connect(m_removeCondition, &QPushButton::clicked, this, [this] {
		auto scene = currentScene();
		if (!scene || m_condition < 0 || m_condition >= int(scene->conditions.size())) return;
		scene->conditions.erase(scene->conditions.begin() + m_condition);
		m_condition = std::min(m_condition, int(scene->conditions.size()) - 1);
		apply(*scene, "Remove condition");
	});
	const auto takeName = [this] {
		const std::string text = m_conditionName->currentText().trimmed().toStdString();
		editCondition([&](ChoreoCondition& c) { c.name = text; }, "Condition");
	};
	connect(m_conditionName, &QComboBox::activated, this, takeName);
	connect(m_conditionName->lineEdit(), &QLineEdit::editingFinished, this, takeName);
	connect(m_conditionCompare, &QComboBox::activated, this, [this](int) {
		const auto compare = static_cast<ChoreoCondition::Compare>(m_conditionCompare->currentData().toInt());
		editCondition([&](ChoreoCondition& c) { c.compare = compare; }, "Condition");
	});
	connect(m_conditionValue, &QDoubleSpinBox::valueChanged, this, [this](double v) { editCondition([&](ChoreoCondition& c) { c.value = v; }, "Condition"); });
	return page;
}

void ChoreographyPanel::addCondition(ChoreoCondition::Kind kind) {
	auto scene = currentScene();
	if (!scene) return;
	ChoreoCondition condition;
	condition.kind = kind;
	switch (kind) {
		case ChoreoCondition::Kind::ScenePlayed:
		case ChoreoCondition::Kind::SceneNotPlayed:
			for (const int id : choreoScenes(m_editor->doc())) {
				if (id != m_sceneId) {
					condition.name = m_editor->doc().findEntity(id)->get("targetname");
					break;
				}
			}
			break;
		case ChoreoCondition::Kind::Variable: {
			const auto variables = choreoVariables(m_editor->doc());
			if (!variables.empty()) {
				condition.name = variables.front().name;
				condition.value = variables.front().flag ? 1 : 0;
			}
			break;
		}
		case ChoreoCondition::Kind::Global: condition.name = GLOBAL_STATES[0]; condition.value = 1; break;
		case ChoreoCondition::Kind::Random: condition.value = 50; break;
	}
	scene->conditions.push_back(condition);
	m_condition = int(scene->conditions.size()) - 1;
	m_tabs->setCurrentIndex(2);
	apply(*scene, "Add condition");
	if (kind == ChoreoCondition::Kind::Variable && condition.name.empty()) {
		emit m_editor->message(tr("No variable yet: make one in the Variables tab, then choose it here"));
	}
}

void ChoreographyPanel::editCondition(const std::function<void(ChoreoCondition&)>& change, const char* undoName) {
	if (m_filling) return;
	auto scene = currentScene();
	if (!scene || m_condition < 0 || m_condition >= int(scene->conditions.size())) return;
	auto& condition = scene->conditions[static_cast<std::size_t>(m_condition)];
	const ChoreoCondition before = condition;
	change(condition);
	if (condition == before) return;
	apply(*scene, undoName);
}

void ChoreographyPanel::refreshConditions(const ChoreoScene& scene) {
	const bool wasFilling = m_filling;
	m_filling = true;
	{
		QSignalBlocker block(m_conditions);
		m_conditions->clear();
		for (const auto& condition : scene.conditions) m_conditions->addItem(qs(describeChoreoCondition(condition)));
		if (scene.conditions.empty()) m_condition = -1;
		else m_condition = std::clamp(m_condition, 0, int(scene.conditions.size()) - 1);
		m_conditions->setCurrentRow(m_condition);
	}
	m_tabs->setTabText(2, scene.conditions.empty() ? tr("Conditions") : tr("Conditions (%1)").arg(scene.conditions.size()));
	m_filling = wasFilling;
	fillConditionEditor();
}

void ChoreographyPanel::fillConditionEditor() {
	const auto scene = currentScene();
	const bool have = scene && m_condition >= 0 && m_condition < int(scene->conditions.size());
	m_conditionBox->setVisible(have);
	m_removeCondition->setEnabled(have);
	if (!have) return;
	const bool wasFilling = m_filling;
	m_filling = true;
	const auto& condition = scene->conditions[static_cast<std::size_t>(m_condition)];
	using Kind = ChoreoCondition::Kind;
	m_conditionKind->setText(conditionKindText(condition.kind));
	m_conditionNameRow->setVisible(condition.kind != Kind::Random);
	{
		QSignalBlocker a(m_conditionName), b(m_conditionCompare), c(m_conditionValue);
		m_conditionName->clear();
		switch (condition.kind) {
			case Kind::ScenePlayed:
			case Kind::SceneNotPlayed:
				for (const int id : choreoScenes(m_editor->doc())) {
					if (id != m_sceneId) m_conditionName->addItem(qs(m_editor->doc().findEntity(id)->get("targetname")));
				}
				break;
			case Kind::Variable:
				for (const auto& v : choreoVariables(m_editor->doc())) m_conditionName->addItem(qs(v.name));
				break;
			case Kind::Global:
				for (const char* name : GLOBAL_STATES) m_conditionName->addItem(QLatin1String(name));
				break;
			case Kind::Random: break;
		}
		m_conditionName->setCurrentText(qs(condition.name));
		m_conditionCompare->setCurrentIndex(std::max(0, m_conditionCompare->findData(int(condition.compare))));
		m_conditionCompare->setVisible(condition.kind == Kind::Variable || condition.kind == Kind::Global);
		if (condition.kind == Kind::Random) {
			m_conditionValue->setRange(0, 100);
			m_conditionValue->setSingleStep(10);
			m_conditionValue->setSuffix(tr(" % chance"));
		} else {
			m_conditionValue->setRange(-1000000, 1000000);
			m_conditionValue->setSingleStep(1);
			m_conditionValue->setSuffix(QString());
		}
		m_conditionValue->setValue(condition.value);
		m_conditionValue->setToolTip(condition.kind == Kind::Variable ? tr("True/false variables: 1 = true, 0 = false")
		                             : condition.kind == Kind::Global ? tr("The global state's counter (env_global SetCounter / AddToCounter, kept across maps)")
		                                                              : QString());
	}
	m_conditionCompareRow->setVisible(condition.kind == Kind::Variable || condition.kind == Kind::Global || condition.kind == Kind::Random);
	m_filling = wasFilling;
}

// --- Variables --------------------------------------------------------------------------------

QWidget* ChoreographyPanel::buildVariablesPage() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 6, 0, 0);
	layout->setSpacing(6);
	layout->addWidget(noteLabel(tr("Values the scenes check in their conditions and change with \"Set variable\" steps "
	                               "(math_counter for numbers, logic_branch for true/false). Double-click a name or start value to change it.")));
	m_variables = new QTableWidget(0, 4);
	m_variables->setObjectName("choreoVariables");
	m_variables->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Start value"), tr("Used by")});
	m_variables->verticalHeader()->hide();
	m_variables->horizontalHeader()->setStretchLastSection(true);
	m_variables->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_variables->setSelectionMode(QAbstractItemView::SingleSelection);
	m_variables->setMinimumHeight(110);
	layout->addWidget(m_variables);
	auto* number = new QPushButton(tr("New number"));
	number->setObjectName("choreoNewNumber");
	number->setToolTip(tr("A number (math_counter): count visits, track a choice, ..."));
	auto* flag = new QPushButton(tr("New true/false"));
	flag->setObjectName("choreoNewFlag");
	flag->setToolTip(tr("A true/false value (logic_branch)"));
	m_deleteVariable = new QPushButton(tr("Delete"));
	m_deleteVariable->setObjectName("choreoDeleteVariable");
	layout->addWidget(line({number, flag, m_deleteVariable}));
	layout->addStretch(1);

	const auto create = [this](bool isFlag) {
		Vec3 origin;
		if (const Entity* scene = m_sceneId ? m_editor->doc().findEntity(m_sceneId) : nullptr) origin = scene->origin() + Vec3(0, 32, 0);
		std::string base = isFlag ? "flag" : "count";
		if (!m_actor.empty()) base = m_actor + "_" + base;
		m_editor->commit(edit::createChoreoVariable(m_editor->doc(), base, isFlag, 0, origin));
		refresh();
		m_variables->setCurrentCell(m_variables->rowCount() - 1, 0);
	};
	connect(number, &QPushButton::clicked, this, [create] { create(false); });
	connect(flag, &QPushButton::clicked, this, [create] { create(true); });
	connect(m_deleteVariable, &QPushButton::clicked, this, [this] {
		const int row = m_variables->currentRow();
		if (row < 0) return;
		const std::string name = m_variables->item(row, 0)->data(Qt::UserRole).toString().toStdString();
		m_editor->commit(edit::deleteChoreoVariable(m_editor->doc(), name));
		refresh();
	});
	connect(m_variables, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
		if (m_filling || !item) return;
		const int row = item->row();
		const std::string name = m_variables->item(row, 0)->data(Qt::UserRole).toString().toStdString();
		if (item->column() == 0) {
			const std::string wanted = item->text().trimmed().toStdString();
			auto change = edit::renameChoreoVariable(m_editor->doc(), name, wanted);
			if (change.empty() && wanted != name) emit m_editor->message(tr("%1 cannot be used: another entity has that name").arg(qs(wanted)));
			else m_editor->commit(std::move(change));
		} else if (item->column() == 2) {
			bool ok = false;
			QString text = item->text().trimmed().toLower();
			double value = text.toDouble(&ok);
			if (text == QLatin1String("true")) value = 1, ok = true;
			if (text == QLatin1String("false")) value = 0, ok = true;
			if (ok) m_editor->commit(edit::setChoreoVariableInitial(m_editor->doc(), name, value));
		}
		QTimer::singleShot(0, this, &ChoreographyPanel::refresh);
	});
	return page;
}

void ChoreographyPanel::refreshVariables() {
	const auto& doc = m_editor->doc();
	const bool wasFilling = m_filling;
	m_filling = true;
	const auto variables = choreoVariables(doc);
	// Which scenes test or set each variable.
	std::map<std::string, QStringList> users;
	for (const int id : choreoScenes(doc)) {
		const auto scene = readChoreoScene(*doc.findEntity(id));
		std::set<std::string> names;
		for (const auto& c : scene.conditions) {
			if (c.kind == ChoreoCondition::Kind::Variable) names.insert(toLower(c.name));
		}
		for (const auto& s : scene.steps) {
			if (s.kind == ChoreoStep::Kind::SetVariable) names.insert(toLower(s.variable));
		}
		for (const auto& n : names) users[n] << qs(scene.name);
	}
	QSignalBlocker block(m_variables);
	const int row = m_variables->currentRow();
	m_variables->setRowCount(int(variables.size()));
	for (int i = 0; i < int(variables.size()); ++i) {
		const auto& v = variables[static_cast<std::size_t>(i)];
		auto* name = new QTableWidgetItem(qs(v.name));
		name->setData(Qt::UserRole, qs(v.name));
		auto* type = new QTableWidgetItem(v.flag ? tr("true/false") : tr("number"));
		type->setFlags(type->flags() & ~Qt::ItemIsEditable);
		auto* initial = new QTableWidgetItem(v.flag ? (v.initial != 0 ? QStringLiteral("true") : QStringLiteral("false")) : qs(formatNumber(v.initial)));
		const auto used = users.find(toLower(v.name));
		auto* by = new QTableWidgetItem(used == users.end() ? tr("-") : used->second.join(", "));
		by->setFlags(by->flags() & ~Qt::ItemIsEditable);
		m_variables->setItem(i, 0, name);
		m_variables->setItem(i, 1, type);
		m_variables->setItem(i, 2, initial);
		m_variables->setItem(i, 3, by);
	}
	if (row >= 0 && row < m_variables->rowCount()) m_variables->selectRow(row);
	m_deleteVariable->setEnabled(!variables.empty());
	m_tabs->setTabText(3, variables.empty() ? tr("Variables") : tr("Variables (%1)").arg(variables.size()));
	m_filling = wasFilling;
}

// --- Set variable step ------------------------------------------------------------------------

QWidget* ChoreographyPanel::buildVariableStepRow() {
	m_stepVariable = new QComboBox;
	m_stepVariable->setObjectName("choreoStepVariable");
	m_stepVariable->setMinimumContentsLength(12);
	m_stepVariable->setToolTip(tr("The variable the step changes (make one in the Variables tab)"));
	m_stepOp = new QComboBox;
	m_stepOp->setObjectName("choreoStepOp");
	m_stepValue = new QDoubleSpinBox;
	m_stepValue->setObjectName("choreoStepValue");
	m_stepValue->setRange(-1000000, 1000000);
	m_stepValue->setDecimals(2);
	m_stepValue->setKeyboardTracking(false);
	m_varRow = line({new QLabel(tr("Variable")), m_stepVariable, m_stepOp, m_stepValue}, false);
	connect(m_stepVariable, &QComboBox::activated, this, [this](int) {
		const std::string name = m_stepVariable->currentText().toStdString();
		editStep([&](ChoreoStep& step) { step.variable = name; }, "Step variable");
	});
	connect(m_stepOp, &QComboBox::activated, this, [this](int) {
		const auto op = static_cast<ChoreoStep::VariableOp>(m_stepOp->currentData().toInt());
		editStep([&](ChoreoStep& step) { step.op = op; }, "Step variable");
	});
	connect(m_stepValue, &QDoubleSpinBox::valueChanged, this, [this](double v) {
		if (m_filling) return;
		editStep([&](ChoreoStep& step) { step.value = v; }, "Step variable");
	});
	return m_varRow;
}

void ChoreographyPanel::fillVariableStep(const ChoreoStep& step) {
	const auto variables = choreoVariables(m_editor->doc());
	const auto found = std::find_if(variables.begin(), variables.end(), [&step](const ChoreoVariable& v) { return iequals(v.name, step.variable); });
	const bool flag = found != variables.end() && found->flag;
	QSignalBlocker a(m_stepVariable), b(m_stepOp), c(m_stepValue);
	m_stepVariable->clear();
	for (const auto& v : variables) m_stepVariable->addItem(qs(v.name));
	if (!step.variable.empty() && found == variables.end()) m_stepVariable->addItem(qs(step.variable));   // gone: still shown
	m_stepVariable->setCurrentText(qs(step.variable));
	m_stepOp->clear();
	m_stepOp->addItem(tr("set to"), int(ChoreoStep::VariableOp::Set));
	if (flag) m_stepOp->addItem(tr("toggle"), int(ChoreoStep::VariableOp::Toggle));
	else m_stepOp->addItem(tr("add"), int(ChoreoStep::VariableOp::Add));
	m_stepOp->setCurrentIndex(std::max(0, m_stepOp->findData(int(step.op))));
	m_stepValue->setRange(flag ? 0 : -1000000, flag ? 1 : 1000000);
	m_stepValue->setDecimals(flag ? 0 : 2);
	m_stepValue->setValue(step.value);
	m_stepValue->setVisible(step.op != ChoreoStep::VariableOp::Toggle);
	m_stepValue->setToolTip(flag ? tr("1 = true, 0 = false") : QString());
}

} // namespace sme::app
