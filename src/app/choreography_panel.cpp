// choreography_panel.cpp - The Choreography dock (see choreography_panel.h).

#include "choreography_panel.h"

#include "editor.h"
#include "icons.h"
#include "scene_poser.h"
#include "sound_player.h"
#include "ui_helpers.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <set>

#include <sme/animation_use.h>
#include <sme/asset_import.h>
#include <sme/audio.h>
#include <sme/game.h>
#include <sme/text.h>

namespace sme::app {

namespace {

constexpr int SCRUB_STEPS = 1000;

const char* kindIcon(ChoreoStep::Kind kind) {
	switch (kind) {
		case ChoreoStep::Kind::Move: return "step:move";
		case ChoreoStep::Kind::Animation: return "step:animation";
		case ChoreoStep::Kind::Say: return "step:say";
		case ChoreoStep::Kind::Wait: return "step:wait";
		case ChoreoStep::Kind::Face: return "step:face";
		case ChoreoStep::Kind::SetVariable: return "step:setvar";
	}
	return "step:wait";
}

QString cardText(const ChoreoStep& step, std::size_t index, double length, bool manyActors) {
	QString text;
	switch (step.kind) {
		case ChoreoStep::Kind::Move:
			text = (step.run ? ChoreographyPanel::tr("Run to %1 %2") : ChoreographyPanel::tr("Walk to %1 %2")).arg(std::lround(step.point.x)).arg(std::lround(step.point.y));
			break;
		case ChoreoStep::Kind::Animation:
			text = step.sequence.empty() ? ChoreographyPanel::tr("Animation (choose one)")
			                             : (step.gesture ? ChoreographyPanel::tr("Gesture %1") : ChoreographyPanel::tr("Animation %1")).arg(qs(step.sequence));
			break;
		case ChoreoStep::Kind::Say:
			text = !step.caption.empty() ? ChoreographyPanel::tr("Say “%1”").arg(qs(step.caption))
			                             : ChoreographyPanel::tr("Say %1").arg(qs(step.sound));
			break;
		case ChoreoStep::Kind::Wait: text = ChoreographyPanel::tr("Wait"); break;
		case ChoreoStep::Kind::Face: text = ChoreographyPanel::tr("Face %1 %2").arg(std::lround(step.point.x)).arg(std::lround(step.point.y)); break;
		case ChoreoStep::Kind::SetVariable: {
			const QString name = step.variable.empty() ? ChoreographyPanel::tr("(choose a variable)") : qs(step.variable);
			text = step.op == ChoreoStep::VariableOp::Toggle ? ChoreographyPanel::tr("Toggle %1").arg(name)
			       : step.op == ChoreoStep::VariableOp::Add  ? ChoreographyPanel::tr("Add %2 to %1").arg(name, qs(formatNumber(step.value)))
			                                                 : ChoreographyPanel::tr("Set %1 = %2").arg(name, qs(formatNumber(step.value)));
			break;
		}
	}
	QString who;
	if (manyActors && step.kind != ChoreoStep::Kind::SetVariable) who = qs(step.actor) + QStringLiteral(": ");
	return QStringLiteral("%1%2  %3%4   (%5 s)")
	    .arg(index + 1)
	    .arg(step.withPrevious ? QStringLiteral("+") : QString())
	    .arg(who, text)
	    .arg(length, 0, 'f', 1);
}

QWidget* row(std::initializer_list<QWidget*> widgets, bool stretch = true) {
	auto* box = new QWidget;
	auto* layout = new QHBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);
	for (auto* widget : widgets) layout->addWidget(widget);
	if (stretch) layout->addStretch(1);
	return box;
}

QPushButton* addButton(const QString& text, const char* iconName, const char* objectName, const QString& tip) {
	auto* button = new QPushButton(icon(QLatin1String(iconName)), text);
	button->setObjectName(QLatin1String(objectName));
	button->setToolTip(tip);
	return button;
}

} // namespace

ChoreographyPanel::ChoreographyPanel(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	// Scenes: all of the map, grouped by NPC.
	layout->addWidget(buildManager());

	m_name = new QLineEdit;
	m_name->setObjectName("choreoName");
	m_name->setToolTip(tr("The scene's name (targetname): fire \"Start\" on it (or Trigger on <name>_start) from your own I/O"));
	auto* startRow = new QHBoxLayout;
	startRow->addWidget(new QLabel(tr("Name")));
	startRow->addWidget(m_name, 1);
	layout->addLayout(startRow);

	// The scene's tabs: steps, what starts it, conditions, the map's variables.
	m_tabs = new QTabWidget;
	m_tabs->setObjectName("choreoTabs");
	m_tabs->setDocumentMode(true);
	auto* stepsPage = new QWidget;
	auto* steps = new QVBoxLayout(stepsPage);
	steps->setContentsMargins(0, 6, 0, 0);
	steps->setSpacing(6);
	m_tabs->addTab(stepsPage, tr("Steps"));
	m_tabs->addTab(buildStartsPage(), tr("Starts"));
	m_tabs->addTab(buildConditionsPage(), tr("Conditions"));
	m_tabs->addTab(buildVariablesPage(), tr("Variables"));
	layout->addWidget(m_tabs);

	// Steps as cards; drag to reorder.
	m_steps = new QListWidget;
	m_steps->setObjectName("choreoSteps");
	m_steps->setDragDropMode(QAbstractItemView::InternalMove);
	m_steps->setDefaultDropAction(Qt::MoveAction);
	m_steps->setIconSize(QSize(20, 20));
	m_steps->setMinimumHeight(110);
	m_steps->setMaximumHeight(240);
	m_steps->setToolTip(tr("The steps in order - drag to reorder. A step marked + starts together with the one before."));
	steps->addWidget(m_steps);

	auto* addWalk = addButton(tr("Walk"), "step:move", "choreoAddWalk", tr("Add a walk: click the point in the map"));
	auto* addAnimation = addButton(tr("Animation"), "step:animation", "choreoAddAnimation", tr("Add an animation the NPC plays where it stands"));
	auto* addSay = addButton(tr("Say"), "step:say", "choreoAddSay", tr("Add a line the NPC says (import your own voice file)"));
	auto* addWait = addButton(tr("Wait"), "step:wait", "choreoAddWait", tr("Add a pause"));
	auto* addFace = addButton(tr("Face"), "step:face", "choreoAddFace", tr("Add a turn towards a point: click it in the map"));
	auto* addSet = addButton(tr("Set variable"), "step:setvar", "choreoAddSetVariable",
	                         tr("Add a step that changes a variable when the scene gets there (other scenes can check it in their conditions)"));
	m_remove = new QPushButton(tr("Remove"));
	m_remove->setObjectName("choreoRemove");
	auto* addRow = new QGridLayout;
	addRow->setContentsMargins(0, 0, 0, 0);
	int slot = 0;
	for (auto* button : {addWalk, addAnimation, addSay, addWait, addFace, addSet, m_remove}) {
		addRow->addWidget(button, slot / 3, slot % 3);
		++slot;
	}
	steps->addLayout(addRow);

	// The current step.
	m_editor_box = new QWidget;
	auto* stepLayout = new QVBoxLayout(m_editor_box);
	stepLayout->setContentsMargins(0, 4, 0, 0);
	m_actorBox = new QComboBox;
	m_actorBox->setObjectName("choreoActor");
	m_actorBox->setEditable(true);
	m_actorBox->setToolTip(tr("Which NPC does the step (its name)"));
	m_withPrevious = new QCheckBox(tr("With previous"));
	m_withPrevious->setObjectName("choreoWithPrevious");
	m_withPrevious->setToolTip(tr("Starts together with the step before instead of after it (two NPCs at once, a gesture while walking)"));
	stepLayout->addWidget(row({new QLabel(tr("NPC")), m_actorBox, m_withPrevious}));

	m_run = new QCheckBox(tr("Run"));
	m_run->setObjectName("choreoRun");
	m_place = new QPushButton(tr("Place again"));
	m_place->setObjectName("choreoPlace");
	m_place->setToolTip(tr("Click the new point in the map (or select the marker and move it)"));
	m_moveRow = row({m_run, m_place});
	stepLayout->addWidget(m_moveRow);

	m_sequence = new QComboBox;
	m_sequence->setObjectName("choreoSequence");
	m_sequence->setEditable(true);
	m_sequence->setMinimumContentsLength(16);
	m_sequence->setToolTip(tr("The NPC model's animation"));
	m_gesture = new QCheckBox(tr("Gesture"));
	m_gesture->setObjectName("choreoGesture");
	m_gesture->setToolTip(tr("Played over what the NPC does (it keeps walking or talking)"));
	m_animRow = row({new QLabel(tr("Animation")), m_sequence, m_gesture}, false);
	stepLayout->addWidget(m_animRow);

	m_sound = new QLineEdit;
	m_sound->setObjectName("choreoSound");
	m_sound->setToolTip(tr("Sound script entry the NPC says (imported lines get one; any game entry works too)"));
	m_voice = new QPushButton(tr("Voice file ..."));
	m_voice->setObjectName("choreoVoice");
	m_voice->setToolTip(tr("Import your own recording (WAV, MP3, OGG) as this line: sound/<mod>/vo/<scene>/, played on the voice channel"));
	m_caption = new QLineEdit;
	m_caption->setObjectName("choreoCaption");
	m_caption->setPlaceholderText(tr("Subtitle (shown with closed captions on)"));
	m_sayRow = new QWidget;
	auto* sayLayout = new QVBoxLayout(m_sayRow);
	sayLayout->setContentsMargins(0, 0, 0, 0);
	sayLayout->addWidget(row({new QLabel(tr("Line")), m_sound, m_voice}, false));
	sayLayout->addWidget(m_caption);
	stepLayout->addWidget(m_sayRow);

	m_seconds = new QDoubleSpinBox;
	m_seconds->setObjectName("choreoSeconds");
	m_seconds->setRange(0, 600);
	m_seconds->setDecimals(1);
	m_seconds->setSingleStep(0.5);
	m_seconds->setSuffix(tr(" s"));
	m_seconds->setSpecialValueText(tr("auto"));
	m_seconds->setKeyboardTracking(false);
	m_seconds->setToolTip(tr("How long the step takes (auto: the animation's or the line's length)"));
	m_secondsRow = row({new QLabel(tr("Length")), m_seconds});
	stepLayout->addWidget(m_secondsRow);
	stepLayout->addWidget(buildVariableStepRow());

	m_scrub = new QSlider(Qt::Horizontal);
	m_scrub->setObjectName("choreoScrub");
	m_scrub->setRange(0, SCRUB_STEPS);
	m_scrub->setToolTip(tr("Where in the step the ghost NPC in the map is shown"));
	m_playStep = new QPushButton(icon("run"), QString());
	m_playStep->setObjectName("choreoPlayStep");
	m_playStep->setToolTip(tr("Play this step on the ghost NPC"));
	m_scrubRow = new QWidget;
	auto* scrubLayout = new QHBoxLayout(m_scrubRow);
	scrubLayout->setContentsMargins(0, 0, 0, 0);
	scrubLayout->addWidget(m_playStep);
	scrubLayout->addWidget(m_scrub, 1);
	stepLayout->addWidget(m_scrubRow);
	steps->addWidget(m_editor_box);

	m_issues = new QLabel;
	m_issues->setObjectName("choreoIssues");
	m_issues->setWordWrap(true);
	m_issues->setStyleSheet(QStringLiteral("color: #ffb46a;"));
	steps->addWidget(m_issues);

	m_preview = new QPushButton(icon("run"), tr("Preview scene"));
	m_preview->setObjectName("choreoPreview");
	m_preview->setCheckable(true);
	m_preview->setToolTip(tr("Plays the whole scene with ghost NPCs in the map views, with the lines and subtitles"));
	m_length = new QLabel;
	m_length->setObjectName("choreoLength");
	auto* previewRow = new QHBoxLayout;
	previewRow->addWidget(m_preview);
	previewRow->addWidget(m_length, 1);
	steps->addLayout(previewRow);
	m_subtitle = new QLabel;
	m_subtitle->setObjectName("choreoSubtitle");
	m_subtitle->setWordWrap(true);
	m_subtitle->setAlignment(Qt::AlignCenter);
	m_subtitle->setStyleSheet(QStringLiteral("color: #ffffff; background: rgba(0,0,0,140); padding: 4px;"));
	m_subtitle->hide();
	steps->addWidget(m_subtitle);
	steps->addWidget(noteLabel(tr("Select the NPC to see its scene in the map: numbered markers, arrows for walks. "
	                               "Click a marker to edit its step; move point markers with the gizmo. "
	                               "Mouths move with the voice's loudness (no phoneme data).")));
	layout->addStretch(1);

	m_timer.setInterval(33);
	connect(&m_timer, &QTimer::timeout, this, &ChoreographyPanel::previewTick);

	// Scene level.
	connect(m_newScene, &QPushButton::clicked, this, [this] {
		int npc = 0;
		for (const int id : m_editor->selection().entities) {
			const Entity* entity = m_editor->doc().findEntity(id);
			if (entity && isNpcClass(entity->classname())) npc = id;
		}
		if (npc == 0 && !m_actor.empty()) {
			if (const Entity* entity = findNamedEntity(m_editor->doc(), m_actor)) npc = entity->id;
		}
		if (npc == 0) {
			emit m_editor->message(tr("Select the NPC the scene is for first"));
			return;
		}
		auto change = edit::createChoreoScene(m_editor->doc(), npc, m_editor->mapName().toStdString());
		int created = 0;
		for (const auto& state : change.entities) {
			if (!state.before && state.after && isChoreoScene(*state.after)) created = state.entityId;
		}
		m_editor->commit(std::move(change));
		if (const Entity* entity = m_editor->doc().findEntity(npc)) m_actor = entity->get("targetname");
		m_sceneId = created;
		m_step = -1;
		refresh();
	});
	connect(m_deleteScene, &QPushButton::clicked, this, [this] {
		if (m_sceneId == 0) return;
		stopPreview();
		m_editor->commit(edit::deleteChoreoScene(m_editor->doc(), m_sceneId));
		m_sceneId = 0;
		m_step = -1;
		m_editor->setSceneFocus({});
		refresh();
	});
	connect(m_name, &QLineEdit::editingFinished, this, [this] {
		auto scene = currentScene();
		const std::string name = m_name->text().trimmed().toStdString();
		if (!scene || name.empty() || name == scene->name) return;
		if (const Entity* other = findNamedEntity(m_editor->doc(), name); other && other->id != m_sceneId) {
			emit m_editor->message(tr("Another entity is called %1 already").arg(qs(name)));
			m_name->setText(qs(scene->name));
			return;
		}
		scene->name = name;
		apply(*scene, "Rename scene");
	});
	// Steps.
	connect(m_steps, &QListWidget::currentRowChanged, this, [this](int row) {
		if (m_filling) return;
		m_step = row;
		m_stepTime = 0;
		stopPreview();
		fillStepEditor();
		pushFocus();
	});
	connect(m_steps->model(), &QAbstractItemModel::rowsMoved, this, [this] { QTimer::singleShot(0, this, &ChoreographyPanel::stepsMoved); });
	connect(addWalk, &QPushButton::clicked, this, [this] { addPointStep(ChoreoStep::Kind::Move); });
	connect(addFace, &QPushButton::clicked, this, [this] { addPointStep(ChoreoStep::Kind::Face); });
	connect(addAnimation, &QPushButton::clicked, this, [this] {
		ChoreoStep step;
		step.kind = ChoreoStep::Kind::Animation;
		step.actor = defaultActor();
		addStep(step, "Add animation");
	});
	connect(addSay, &QPushButton::clicked, this, [this] {
		const auto scene = currentScene();
		if (!scene) return;
		ChoreoStep step;
		step.kind = ChoreoStep::Kind::Say;
		step.actor = defaultActor();
		int n = 1;
		const auto used = [&](const std::string& name) {
			return std::any_of(scene->steps.begin(), scene->steps.end(), [&](const ChoreoStep& s) { return iequals(s.sound, name); });
		};
		while (used(voiceLineName(scene->name, "line" + std::to_string(n)))) ++n;
		step.sound = voiceLineName(scene->name, "line" + std::to_string(n));
		addStep(step, "Add line");
	});
	connect(addSet, &QPushButton::clicked, this, [this] {
		ChoreoStep step;
		step.kind = ChoreoStep::Kind::SetVariable;
		const auto variables = choreoVariables(m_editor->doc());
		if (!variables.empty()) {
			step.variable = variables.front().name;
			step.value = variables.front().flag ? 1 : 0;
		}
		addStep(step, "Add set variable");
	});
	connect(addWait, &QPushButton::clicked, this, [this] {
		ChoreoStep step;
		step.kind = ChoreoStep::Kind::Wait;
		step.actor = defaultActor();
		step.seconds = 1;
		addStep(step, "Add wait");
	});
	connect(m_remove, &QPushButton::clicked, this, [this] {
		auto scene = currentScene();
		if (!scene || m_step < 0 || m_step >= int(scene->steps.size())) return;
		scene->steps.erase(scene->steps.begin() + m_step);
		m_step = std::min(m_step, int(scene->steps.size()) - 1);
		apply(*scene, "Remove step");
	});

	// The current step.
	connect(m_actorBox, &QComboBox::activated, this, [this](int) {
		const std::string actor = m_actorBox->currentText().toStdString();
		editStep([&](ChoreoStep& step) { step.actor = actor; }, "Step NPC");
	});
	connect(m_withPrevious, &QCheckBox::toggled, this, [this](bool on) { editStep([&](ChoreoStep& step) { step.withPrevious = on; }, "With previous"); });
	connect(m_run, &QCheckBox::toggled, this, [this](bool on) { editStep([&](ChoreoStep& step) { step.run = on; }, "Run"); });
	connect(m_place, &QPushButton::clicked, this, [this] {
		const int sceneId = m_sceneId, index = m_step;
		m_editor->requestScenePoint(tr("Click the new point in the map - Esc cancels"), [this, sceneId, index](const Vec3& point) {
			if (sceneId != m_sceneId || index != m_step) return;
			editStep([&](ChoreoStep& step) { step.point = point; }, "Move scene point");
		});
	});
	connect(m_sequence, &QComboBox::textActivated, this, [this](const QString& text) {
		const auto scene = currentScene();
		if (!scene || m_step < 0) return;
		const QString model = actorModel(scene->steps[static_cast<std::size_t>(m_step)].actor);
		const auto length = m_editor->poser()->sequenceSeconds(model, text);
		bool layer = false;
		if (const auto* animated = m_editor->poser()->animated(model)) {
			const int index = animated->findSequence(text.toStdString());
			layer = index >= 0 && animated->sequences()[static_cast<std::size_t>(index)].layer;
		}
		editStep(
		    [&](ChoreoStep& step) {
			    step.sequence = text.toStdString();
			    step.gesture = layer;
			    step.seconds = length && *length > 0 ? std::round(*length * 100) / 100 : 0;
		    },
		    "Step animation");
	});
	connect(m_gesture, &QCheckBox::toggled, this, [this](bool on) { editStep([&](ChoreoStep& step) { step.gesture = on; }, "Gesture"); });
	connect(m_sound, &QLineEdit::editingFinished, this, [this] {
		const std::string sound = m_sound->text().trimmed().toStdString();
		const auto scene = currentScene();
		if (!scene || m_step < 0 || scene->steps[static_cast<std::size_t>(m_step)].sound == sound) return;
		const auto length = soundSeconds(sound);
		editStep(
		    [&](ChoreoStep& step) {
			    step.sound = sound;
			    if (length) step.seconds = std::round(*length * 100) / 100;
		    },
		    "Step line");
	});
	connect(m_voice, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Voice line"), QString(), tr("Sounds (*.wav *.mp3 *.ogg)"));
		if (!path.isEmpty()) importVoiceLine(path, m_caption->text());
	});
	connect(m_caption, &QLineEdit::editingFinished, this, [this] {
		const auto scene = currentScene();
		if (!scene || m_step < 0) return;
		const auto& step = scene->steps[static_cast<std::size_t>(m_step)];
		const std::string caption = m_caption->text().toStdString();
		if (step.caption == caption) return;
		if (const auto* game = m_editor->gameEntry(); game && !step.sound.empty() && !caption.empty()) {
			std::string error;
			if (!setVoiceCaption(game->gameDir, step.sound, caption, &error)) emit m_editor->message(tr("Subtitle not saved: %1").arg(qs(error)));
		}
		editStep([&](ChoreoStep& s) { s.caption = caption; }, "Subtitle");
	});
	connect(m_seconds, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		if (m_filling) return;
		editStep([&](ChoreoStep& step) { step.seconds = value; }, "Step length");
	});
	connect(m_scrub, &QSlider::valueChanged, this, [this](int value) {
		if (m_filling) return;
		m_stepTime = currentStepLength() * value / SCRUB_STEPS;
		pushFocus();
	});
	connect(m_playStep, &QPushButton::clicked, this, &ChoreographyPanel::playStep);
	connect(m_preview, &QPushButton::toggled, this, [this](bool on) { on ? startPreview() : stopPreview(); });

	// The map.
	connect(editor, &Editor::documentChanged, this, [this] {
		if (isVisible()) refresh();
	});
	connect(editor, &Editor::documentReplaced, this, [this] {
		stopPreview();
		m_sceneId = 0;
		m_step = -1;
		m_actor.clear();
		if (isVisible()) refresh();
	});
	connect(editor, &Editor::selectionChanged, this, &ChoreographyPanel::onSelectionChanged);
	connect(editor, &Editor::sceneFocusChanged, this, &ChoreographyPanel::onFocusChanged);
	connect(editor, &Editor::gameChanged, this, [this] {
		m_scripts.reset();
		if (isVisible()) refresh();
	});
	connect(editor->poser(), &ScenePoser::ready, this, [this] {
		if (isVisible()) fillStepEditor();
	});
	refresh();
}

ChoreographyPanel::~ChoreographyPanel() = default;

void ChoreographyPanel::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	refresh();
}

void ChoreographyPanel::openFor(int entityId) {
	stopPreview();
	const Entity* entity = m_editor->doc().findEntity(entityId);
	if (!entity || entityId == 0) {
		return;
	}
	if (isChoreoScene(*entity)) {
		m_sceneId = entityId;
		m_step = -1;
		refresh();
		return;
	}
	m_actor = entity->get("targetname");
	const auto scenes = m_actor.empty() ? std::vector<int>{} : choreoScenesOfActor(m_editor->doc(), m_actor);
	if (!scenes.empty()) {
		m_sceneId = scenes.front();
		m_step = -1;
		refresh();
		return;
	}
	// No scene yet: make one (named after the NPC, started at map start).
	auto change = edit::createChoreoScene(m_editor->doc(), entityId, m_editor->mapName().toStdString());
	int created = 0;
	for (const auto& state : change.entities) {
		if (!state.before && state.after && isChoreoScene(*state.after)) created = state.entityId;
	}
	m_editor->commit(std::move(change));
	if (const Entity* npc = m_editor->doc().findEntity(entityId)) m_actor = npc->get("targetname");
	m_sceneId = created;
	m_step = -1;
	refresh();
}

void ChoreographyPanel::setCurrentStep(int step) {
	m_steps->setCurrentRow(step);
}

std::optional<ChoreoScene> ChoreographyPanel::currentScene() const {
	const Entity* entity = m_sceneId ? m_editor->doc().findEntity(m_sceneId) : nullptr;
	if (!entity || !isChoreoScene(*entity)) {
		return std::nullopt;
	}
	return readChoreoScene(m_editor->doc(), *entity);
}

std::string ChoreographyPanel::defaultActor() const {
	const auto scene = currentScene();
	if (scene && m_step >= 0 && m_step < int(scene->steps.size()) && !scene->steps[static_cast<std::size_t>(m_step)].actor.empty()) {
		return scene->steps[static_cast<std::size_t>(m_step)].actor;
	}
	if (!m_actor.empty()) return m_actor;
	if (scene && !scene->steps.empty()) return scene->steps.back().actor;
	return {};
}

QString ChoreographyPanel::actorModel(const std::string& actor) const {
	const Entity* npc = findNamedEntity(m_editor->doc(), actor);
	return npc ? m_editor->entityModel(*npc) : QString();
}

void ChoreographyPanel::apply(const ChoreoScene& scene, const char* undoName) {
	if (m_sceneId == 0) {
		return;
	}
	m_editor->commit(edit::setChoreoScene(m_editor->doc(), m_sceneId, scene, undoName));
	if (QString error; !m_editor->writeSceneFiles(&error)) {
		emit m_editor->message(tr("Could not write the scene file %1").arg(error));
	}
	refresh();
}

void ChoreographyPanel::editStep(const std::function<void(ChoreoStep&)>& change, const char* undoName) {
	if (m_filling) return;
	auto scene = currentScene();
	if (!scene || m_step < 0 || m_step >= int(scene->steps.size())) return;
	auto& step = scene->steps[static_cast<std::size_t>(m_step)];
	const ChoreoStep before = step;
	change(step);
	if (step == before) return;
	apply(*scene, undoName);
}

void ChoreographyPanel::addStep(ChoreoStep step, const char* undoName) {
	auto scene = currentScene();
	if (!scene) {
		emit m_editor->message(tr("Make a scene first: select an NPC and press New"));
		return;
	}
	const int at = m_step >= 0 && m_step < int(scene->steps.size()) ? m_step + 1 : int(scene->steps.size());
	scene->steps.insert(scene->steps.begin() + at, std::move(step));
	m_step = at;
	m_stepTime = 0;
	apply(*scene, undoName);
}

void ChoreographyPanel::addPointStep(ChoreoStep::Kind kind) {
	if (!currentScene()) {
		emit m_editor->message(tr("Make a scene first: select an NPC and press New"));
		return;
	}
	const int sceneId = m_sceneId;
	const QString hint = kind == ChoreoStep::Kind::Move ? tr("Click where the NPC walks to - Esc cancels") : tr("Click what the NPC turns to - Esc cancels");
	m_editor->requestScenePoint(hint, [this, sceneId, kind](const Vec3& point) {
		if (sceneId != m_sceneId) return;
		ChoreoStep step;
		step.kind = kind;
		step.actor = defaultActor();
		step.point = point;
		addStep(step, kind == ChoreoStep::Kind::Move ? "Add walk" : "Add face");
	});
}

void ChoreographyPanel::stepsMoved() {
	auto scene = currentScene();
	if (!scene || m_steps->count() != int(scene->steps.size())) return;
	std::vector<ChoreoStep> steps;
	int current = -1;
	for (int i = 0; i < m_steps->count(); ++i) {
		const int old = m_steps->item(i)->data(Qt::UserRole).toInt();
		if (old < 0 || old >= int(scene->steps.size())) return;
		steps.push_back(scene->steps[static_cast<std::size_t>(old)]);
		if (old == m_step) current = i;
	}
	scene->steps = std::move(steps);
	m_step = current;
	apply(*scene, "Reorder steps");
}

void ChoreographyPanel::onSelectionChanged() {
	if (!isVisible()) return;
	// A selected point helper shows its step; a selected NPC or scene its scene.
	const auto& doc = m_editor->doc();
	for (const int id : m_editor->selection().entities) {
		const Entity* entity = doc.findEntity(id);
		if (!entity) continue;
		const std::string tag = entity->get(std::string(CHOREO_SCENE_KEY));
		if (!tag.empty() && tag != "1") {
			const Entity* scene = findNamedEntity(doc, tag);
			if (!scene) continue;
			const std::string prefix = tag + "_p";
			const std::string name = entity->get("targetname");
			const std::string role = entity->get(std::string(CHOREO_ROLE_KEY));
			int step = m_step;
			if (startsWithI(name, prefix) && (role.empty() || iequals(role, "point"))) step = std::atoi(name.c_str() + prefix.size()) - 1;
			// A start's box or timer: the Starts tab with that start.
			if (startsWithI(role, "start")) {
				const int start = std::atoi(role.c_str() + 5) - 1;
				if (start >= 0 && (start != m_start || scene->id != m_sceneId)) {
					m_start = start;
					m_tabs->setCurrentIndex(1);
					if (scene->id == m_sceneId) refresh();
				}
			}
			if (scene->id != m_sceneId || step != m_step) {
				m_sceneId = scene->id;
				m_step = step;
				m_stepTime = 0;
				refresh();
			}
			return;
		}
		if (isChoreoScene(*entity) && entity->id != m_sceneId) {
			m_sceneId = entity->id;
			m_step = -1;
			refresh();
			return;
		}
		if (isNpcClass(entity->classname())) {
			const std::string name = entity->get("targetname");
			m_actor = name;
			const auto scenes = name.empty() ? std::vector<int>{} : choreoScenesOfActor(doc, name);
			if (!scenes.empty() && std::find(scenes.begin(), scenes.end(), m_sceneId) == scenes.end()) {
				m_sceneId = scenes.front();
				m_step = -1;
				refresh();
				return;
			}
			refreshManager();   // its group opens in the list
		}
	}
	pushFocus();
}

void ChoreographyPanel::onFocusChanged() {
	const auto& focus = m_editor->sceneFocus();
	if (focus.sceneId == 0 || focus.playTime >= 0) return;
	if (focus.sceneId != m_sceneId || focus.step != m_step) {
		m_sceneId = focus.sceneId;
		m_step = focus.step;
		m_stepTime = focus.stepTime;
		refresh();
	}
}

void ChoreographyPanel::pushFocus() {
	if (m_previewing) return;
	m_editor->setSceneFocus({m_sceneId, m_step, m_stepTime, -1});
}

void ChoreographyPanel::refresh() {
	const auto& doc = m_editor->doc();
	m_filling = true;
	// Scenes of the map.
	const auto scenes = choreoScenes(doc);
	if (m_sceneId != 0 && std::find(scenes.begin(), scenes.end(), m_sceneId) == scenes.end()) {
		m_sceneId = 0;
	}
	if (m_sceneId == 0) {
		const auto visible = m_editor->visibleScenes();
		if (!visible.empty()) m_sceneId = visible.front();
	}
	refreshManager();
	refreshVariables();
	const auto scene = currentScene();
	m_deleteScene->setEnabled(scene.has_value());
	m_copyScene->setEnabled(scene.has_value());
	m_selectScene->setEnabled(scene.has_value());
	m_name->setEnabled(scene.has_value());
	m_tabs->setEnabled(scene.has_value());
	m_preview->setEnabled(scene && !scene->steps.empty());
	if (!scene) {
		m_name->clear();
		m_steps->clear();
		m_starts->clear();
		m_conditions->clear();
		m_start = m_condition = -1;
		fillStartEditor();
		fillConditionEditor();
		m_issues->setText(scenes.empty() ? tr("No scene yet: select an NPC and press New (or Choreography ... in its Object Properties).")
		                                 : tr("Choose a scene above or select an NPC with a scene."));
		m_length->clear();
		m_step = -1;
		m_filling = false;
		fillStepEditor();
		m_editor->setSceneFocus({});
		return;
	}
	m_name->setText(qs(scene->name));
	refreshStarts(*scene);
	refreshConditions(*scene);
	// Step cards.
	const auto timeline = choreoTimeline(doc, *scene);
	bool manyActors = false;
	const auto actors = choreoActors(*scene);
	manyActors = actors.size() > 1;
	{
		QSignalBlocker block(m_steps);
		m_steps->clear();
		for (std::size_t i = 0; i < scene->steps.size(); ++i) {
			const auto& step = scene->steps[i];
			const double length = i < timeline.size() ? timeline[i].end - timeline[i].start : 0;
			auto* item = new QListWidgetItem(icon(QLatin1String(kindIcon(step.kind))), cardText(step, i, length, manyActors));
			item->setData(Qt::UserRole, int(i));
			m_steps->addItem(item);
		}
		m_step = std::clamp(m_step, -1, int(scene->steps.size()) - 1);
		if (m_step < 0 && !scene->steps.empty()) m_step = 0;
		m_steps->setCurrentRow(m_step);
	}
	m_length->setText(tr("%1 steps, %2 s").arg(scene->steps.size()).arg(choreoLength(timeline), 0, 'f', 1));
	m_filling = false;
	updateIssues(*scene);
	fillStepEditor();
	pushFocus();
}

void ChoreographyPanel::updateIssues(const ChoreoScene& scene) {
	std::set<std::string> known;
	for (const auto& entry : scripts()) known.insert(toLower(entry.name));
	const auto soundKnown = [&](std::string_view name) { return known.contains(toLower(std::string(name))); };
	const auto sequenceKnown = [this](std::string_view actor, std::string_view sequence) -> std::optional<bool> {
		const auto* animated = m_editor->poser()->animated(actorModel(std::string(actor)));
		if (!animated) return std::nullopt;
		return animated->findSequence(sequence) >= 0;
	};
	QStringList lines;
	for (const auto& issue : checkChoreoScene(m_editor->doc(), scene, soundKnown, sequenceKnown)) {
		lines << (issue.step >= 0 ? tr("Step %1: %2").arg(issue.step + 1).arg(qs(issue.message)) : qs(issue.message));
	}
	m_issues->setText(lines.join('\n'));
	m_issues->setVisible(!lines.isEmpty());
}

void ChoreographyPanel::fillStepEditor() {
	const auto scene = currentScene();
	const bool have = scene && m_step >= 0 && m_step < int(scene->steps.size());
	m_editor_box->setVisible(have);
	m_remove->setEnabled(have);
	if (!have) return;
	const bool wasFilling = m_filling;
	m_filling = true;
	const auto& step = scene->steps[static_cast<std::size_t>(m_step)];
	// NPCs of the map (named ones) for the actor box.
	{
		QSignalBlocker block(m_actorBox);
		m_actorBox->clear();
		QStringList names;
		for (const auto& entity : m_editor->doc().entities) {
			const std::string name = entity.get("targetname");
			if (!name.empty() && isNpcClass(entity.classname())) names << qs(name);
		}
		if (!step.actor.empty() && !names.contains(qs(step.actor))) names.prepend(qs(step.actor));
		names.removeDuplicates();
		m_actorBox->addItems(names);
		m_actorBox->setCurrentText(qs(step.actor));
	}
	{
		QSignalBlocker a(m_withPrevious), b(m_run), c(m_gesture), d(m_seconds);
		m_withPrevious->setChecked(step.withPrevious);
		m_withPrevious->setEnabled(m_step > 0);
		m_run->setChecked(step.run);
		m_gesture->setChecked(step.gesture);
		m_seconds->setValue(step.seconds);
	}
	using Kind = ChoreoStep::Kind;
	m_moveRow->setVisible(step.kind == Kind::Move || step.kind == Kind::Face);
	m_run->setVisible(step.kind == Kind::Move);
	m_animRow->setVisible(step.kind == Kind::Animation);
	m_sayRow->setVisible(step.kind == Kind::Say);
	m_secondsRow->setVisible(step.kind == Kind::Animation || step.kind == Kind::Say || step.kind == Kind::Wait);
	m_actorBox->parentWidget()->setVisible(step.kind != Kind::SetVariable);
	m_scrubRow->setVisible(step.kind != Kind::SetVariable);
	m_varRow->setVisible(step.kind == Kind::SetVariable);
	if (step.kind == Kind::SetVariable) fillVariableStep(step);
	m_seconds->setSpecialValueText(step.kind == Kind::Wait ? QString() : tr("auto"));
	if (step.kind == Kind::Animation) {
		QSignalBlocker block(m_sequence);
		m_sequence->clear();
		const QString model = actorModel(step.actor);
		const auto names = m_editor->poser()->sequences(model);
		m_sequence->addItems(names);
		m_sequence->setCurrentText(qs(step.sequence));
		m_sequence->setToolTip(model.isEmpty() ? tr("The NPC has no model to read animations from")
		                                       : (names.isEmpty() ? tr("Reading the animations of %1 ...").arg(model) : tr("Animations of %1").arg(model)));
	}
	if (step.kind == Kind::Say) {
		QSignalBlocker a(m_sound), b(m_caption);
		m_sound->setText(qs(step.sound));
		m_caption->setText(qs(step.caption));
	}
	{
		QSignalBlocker block(m_scrub);
		const double length = currentStepLength();
		m_scrub->setValue(length > 0 ? int(std::lround(std::clamp(m_stepTime / length, 0.0, 1.0) * SCRUB_STEPS)) : 0);
	}
	m_filling = wasFilling;
}

double ChoreographyPanel::currentStepLength() const {
	const auto scene = currentScene();
	if (!scene || m_step < 0 || m_step >= int(scene->steps.size())) return 0;
	const auto timeline = choreoTimeline(m_editor->doc(), *scene);
	const auto& timing = timeline[static_cast<std::size_t>(m_step)];
	return timing.end - timing.start;
}

void ChoreographyPanel::playStep() {
	if (m_step < 0) return;
	stopPreview();
	m_stepPlaying = true;
	m_clock.start();
	m_timer.start();
}

void ChoreographyPanel::startPreview() {
	const auto scene = currentScene();
	if (!scene || scene->steps.empty()) {
		QSignalBlocker block(m_preview);
		m_preview->setChecked(false);
		return;
	}
	m_stepPlaying = false;
	m_previewing = true;
	m_spoken.assign(scene->steps.size(), false);
	m_clock.start();
	m_timer.start();
	previewTick();
}

void ChoreographyPanel::stopPreview() {
	const bool was = m_previewing || m_stepPlaying;
	m_timer.stop();
	m_previewing = false;
	m_stepPlaying = false;
	m_subtitle->hide();
	if (m_player) m_player->stop();
	{
		QSignalBlocker block(m_preview);
		m_preview->setChecked(false);
	}
	if (was) pushFocus();
}

void ChoreographyPanel::previewTick() {
	const double t = m_clock.elapsed() / 1000.0;
	if (m_stepPlaying) {
		const double length = currentStepLength();
		m_stepTime = std::min(t, length);
		{
			QSignalBlocker block(m_scrub);
			m_scrub->setValue(length > 0 ? int(std::lround(m_stepTime / length * SCRUB_STEPS)) : 0);
		}
		pushFocus();
		if (t >= length) {
			m_timer.stop();
			m_stepPlaying = false;
		}
		return;
	}
	const auto scene = currentScene();
	if (!scene) {
		stopPreview();
		return;
	}
	const auto timeline = choreoTimeline(m_editor->doc(), *scene);
	if (t > choreoLength(timeline) + 0.5) {
		stopPreview();
		return;
	}
	m_editor->setSceneFocus({m_sceneId, -1, 0, t});
	// Lines: the sound starts once, the subtitle shows while the line runs.
	QString subtitle;
	for (std::size_t i = 0; i < scene->steps.size() && i < timeline.size(); ++i) {
		const auto& step = scene->steps[i];
		if (step.kind != ChoreoStep::Kind::Say || t < timeline[i].start || t >= timeline[i].end) continue;
		if (!step.caption.empty()) subtitle = qs(step.caption);
		if (i < m_spoken.size() && !m_spoken[i]) {
			m_spoken[i] = true;
			playSound(step.sound);
		}
	}
	m_subtitle->setText(subtitle);
	m_subtitle->setVisible(!subtitle.isEmpty());
}

const std::vector<SoundScriptEntry>& ChoreographyPanel::scripts() {
	if (!m_scripts) {
		const auto* game = m_editor->game();
		m_scripts = game ? soundScripts(game->files) : std::vector<SoundScriptEntry>{};
	}
	return *m_scripts;
}

std::optional<double> ChoreographyPanel::soundSeconds(const std::string& name) {
	const auto* game = m_editor->game();
	if (!game) return std::nullopt;
	for (const auto& entry : scripts()) {
		if (!iequals(entry.name, name) || entry.waves.empty()) continue;
		const auto data = game->files.read("sound/" + entry.waves.front());
		if (!data) return std::nullopt;
		const auto audio = decodeAudio(*data, entry.waves.front());
		if (!audio || audio->sampleRate <= 0 || audio->channels <= 0) return std::nullopt;
		return double(audio->samples.size()) / audio->channels / audio->sampleRate;
	}
	return std::nullopt;
}

void ChoreographyPanel::playSound(const std::string& name) {
	const auto* game = m_editor->game();
	if (!game || !SoundPlayer::available()) return;
	for (const auto& entry : scripts()) {
		if (!iequals(entry.name, name) || entry.waves.empty()) continue;
		const auto data = game->files.read("sound/" + entry.waves.front());
		if (!data) return;
		auto audio = decodeAudio(*data, entry.waves.front());
		if (!audio) return;
		if (!m_player) m_player = std::make_unique<SoundPlayer>();
		m_player->play(std::make_shared<const DecodedAudio>(std::move(*audio)));
		return;
	}
}

bool ChoreographyPanel::importVoiceLine(const QString& path, const QString& caption) {
	const auto fail = [&](const QString& why) {
		emit m_editor->message(tr("Voice line not imported: %1").arg(why));
		return false;
	};
	const auto* game = m_editor->game();
	const auto* entry = m_editor->gameEntry();
	if (!game || !entry) return fail(tr("no game is open"));
	auto scene = currentScene();
	if (!scene) return fail(tr("no scene is shown"));
	if (m_step < 0 || m_step >= int(scene->steps.size()) || scene->steps[static_cast<std::size_t>(m_step)].kind != ChoreoStep::Kind::Say) {
		// A new line after the current step.
		ChoreoStep step;
		step.kind = ChoreoStep::Kind::Say;
		step.actor = defaultActor();
		const int at = m_step >= 0 ? m_step + 1 : int(scene->steps.size());
		scene->steps.insert(scene->steps.begin() + std::min(at, int(scene->steps.size())), step);
		m_step = std::min(at, int(scene->steps.size()) - 1);
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
	const QByteArray bytes = file.readAll();
	std::string error;
	const auto audio = decodeAudio(std::as_bytes(std::span(bytes.constData(), std::size_t(bytes.size()))), QFileInfo(path).fileName().toStdString(), &error);
	if (!audio) return fail(qs(error));
	auto& step = scene->steps[static_cast<std::size_t>(m_step)];
	// The line's name: kept when re-importing, else the next free "lineN" of the scene.
	const std::string prefix = voiceLineName(scene->name, "x").substr(0, voiceLineName(scene->name, "x").size() - 1);
	std::string line;
	if (startsWithI(step.sound, prefix)) {
		line = step.sound.substr(prefix.size());
	} else {
		for (int n = 1;; ++n) {
			line = "line" + std::to_string(n);
			const std::string name = voiceLineName(scene->name, line);
			if (std::none_of(scene->steps.begin(), scene->steps.end(), [&](const ChoreoStep& s) { return iequals(s.sound, name); })) break;
		}
	}
	SoundImportOptions options;
	options.folder = importFolder(entry->gameDir.filename().string() + "/vo/" + scene->name);
	options.name = importName(line);
	options.sampleRate = 44100;
	options.channels = SoundImportOptions::Channels::Mono;
	options.script = true;
	options.scriptName = voiceLineName(scene->name, line);
	options.channel = "CHAN_VOICE";
	options.soundLevel = "SNDLVL_TALKING";
	const std::string manifest = game->files.readText("scripts/game_sounds_manifest.txt").value_or(std::string());
	const auto record = importSound(entry->gameDir, *audio, options, manifest, true, &error);
	if (!record) return fail(qs(error));
	rememberImport(entry->gameDir, *record);
	step.sound = options.scriptName;
	step.seconds = audio->sampleRate > 0 && audio->channels > 0 ? std::round(double(audio->samples.size()) / audio->channels / audio->sampleRate * 100) / 100 : 0;
	if (!caption.isEmpty()) step.caption = caption.toStdString();
	if (!step.caption.empty() && !setVoiceCaption(entry->gameDir, step.sound, step.caption, &error)) {
		emit m_editor->message(tr("Subtitle not saved: %1").arg(qs(error)));
	}
	m_scripts.reset();
	apply(*scene, "Voice line");
	emit m_editor->message(tr("Imported the voice line %1 (sound/%2/%3.wav)").arg(qs(options.scriptName), qs(options.folder), qs(options.name)));
	emit modChanged();
	return true;
}

} // namespace sme::app
