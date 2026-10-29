// properties_script.cpp - The "Scripted sequence" box of the properties panel and the citizen's
// fixed look: how the script starts (in words - "Start on Spawn" alone only parks the NPC in its
// Pre Action Idle), "Start on map start" and "Start with a trigger ..." (a trigger_once wired to
// BeginSequence, selected to be moved into place), how it loops, how the NPC gets to its spot,
// and whether anything may interrupt it. The rules are in sme/npc_scripts.h; every button is one
// undo step. Part of PropertiesPanel, kept in its own file so the feature stays in one place.

#include "properties_panel.h"

#include "editor.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <sme/npc_scripts.h>
#include <sme/text.h>

namespace sme::app {

namespace {

long long flagsOf(const Entity& entity) {
	try {
		return std::stoll(entity.get("spawnflags", "0"));
	} catch (...) {
		return 0;
	}
}

} // namespace

void PropertiesPanel::buildScriptBox(QVBoxLayout* layout, const Entity& script) {
	const int id = script.id;
	const auto& doc = m_editor->doc();
	const ScriptState state = scriptState(doc, id);
	auto* box = new QGroupBox(tr("Scripted sequence"));
	box->setObjectName("scriptBox");
	auto* v = new QVBoxLayout(box);

	// How it starts, in words.
	QString start;
	switch (state.start) {
		case ScriptState::Start::Never:
			start = tr("Never starts: nothing sends it BeginSequence and Start on Spawn is off.");
			break;
		case ScriptState::Start::Immediately:
			start = tr("Starts at map start (it has no name): the NPC goes to its spot and plays the Action.");
			break;
		case ScriptState::Start::WaitsInIdle:
			start = tr("The NPC waits at its spot in the Pre Action Idle - the Action never plays: a named script with "
			           "Start on Spawn waits for BeginSequence. Start it on map start or with a trigger.");
			break;
		case ScriptState::Start::AtMapStart:
			start = tr("Starts at map start (a logic_auto sends BeginSequence).");
			break;
		case ScriptState::Start::ByOthers: {
			QStringList names;
			for (const int starter : state.starters) {
				if (const Entity* e = doc.findEntity(starter)) names << qs(e->get("targetname", e->classname()));
			}
			start = tr("Starts when %1 sends BeginSequence.").arg(names.join(QStringLiteral(", ")));
			if (state.startOnSpawn) start += tr(" Until then the NPC waits at its spot in the Pre Action Idle.");
			break;
		}
	}
	auto* summary = new QLabel(QStringLiteral("<b>%1</b>").arg(start.toHtmlEscaped()));
	summary->setObjectName("scriptStart");
	summary->setWordWrap(true);
	v->addWidget(summary);
	const std::string npcName = script.get("m_iszEntity");
	if (npcName.empty()) {
		v->addWidget(noteLabel(tr("Set Target NPC to the name of the NPC that plays it."), "warning"));
	} else if (state.npcId == 0 && !npcName.starts_with("npc_")) {
		v->addWidget(noteLabel(tr("No entity named \"%1\" in this map.").arg(qs(npcName)), "warning"));
	}

	auto* buttons = new QVBoxLayout;   // stacked: the panel is often narrow
	auto* atStart = new QPushButton(tr("Start on map start"));
	atStart->setObjectName("scriptAtMapStart");
	atStart->setToolTip(tr("Start on Spawn, and a logic_auto sends BeginSequence when the map starts: the NPC plays the Action at once"));
	atStart->setEnabled(state.start != ScriptState::Start::AtMapStart);
	auto* trigger = new QPushButton(tr("Start with a trigger ..."));
	trigger->setObjectName("scriptTrigger");
	trigger->setToolTip(tr("Adds a trigger box in front of the script (selected - move and size it where the player walks in).\n"
	                       "The NPC waits at its spot in the Pre Action Idle until the player touches the box."));
	buttons->addWidget(atStart);
	buttons->addWidget(trigger);
	v->addLayout(buttons);
	connect(atStart, &QPushButton::clicked, this, [this, id] {
		m_editor->commit(edit::startSequenceAtMapStart(m_editor->doc(), id));
		emit m_editor->message(tr("The script starts when the map starts"));
	});
	connect(trigger, &QPushButton::clicked, this, [this, id] {
		auto& d = m_editor->doc();
		Change change = edit::addSequenceTrigger(d, id, sequenceTriggerBox(d, id));
		if (change.empty()) return;
		const int triggerId = d.entities.back().id;
		m_editor->commit(std::move(change));
		Selection selection;
		selection.entities = {triggerId};
		m_editor->setSelection(selection);
		emit m_editor->message(tr("Trigger added and selected: move and size it where the player should start the scene"));
	});

	auto* form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	auto* loop = new QComboBox;
	loop->setObjectName("scriptLoop");
	loop->addItem(tr("Play once"), int(ScriptState::Loop::Once));
	loop->addItem(tr("Loop the Action (until StopActionLoop)"), int(ScriptState::Loop::Action));
	loop->addItem(tr("Loop the Post Action Idle after it"), int(ScriptState::Loop::PostIdle));
	loop->setCurrentIndex(loop->findData(int(state.loop)));
	loop->setToolTip(tr("Loop the Action: Loop Action Animation - it repeats until the StopActionLoop input.\n"
	                    "Loop the Post Action Idle: Loop in Post Idle - the Action plays once, then the Post Action Idle "
	                    "repeats (the Action itself if none is set)."));
	connect(loop, &QComboBox::activated, this, [this, id, loop](int index) {
		m_editor->commit(edit::setSequenceLoop(m_editor->doc(), id, ScriptState::Loop(loop->itemData(index).toInt())));
	});
	form->addRow(tr("Loop"), loop);

	auto* moveTo = new QComboBox;
	moveTo->setObjectName("scriptMoveTo");
	moveTo->addItem(tr("No - plays where the NPC stands"), QStringLiteral("0"));
	moveTo->addItem(tr("Walk there (needs a way: info_nodes or a clear line)"), QStringLiteral("1"));
	moveTo->addItem(tr("Run there (needs a way: info_nodes or a clear line)"), QStringLiteral("2"));
	moveTo->addItem(tr("Instantly - put it on this spot, facing this way"), QStringLiteral("4"));
	const QString moveValue = qs(script.get("m_fMoveTo", "1"));
	moveTo->setCurrentIndex(std::max(0, moveTo->findData(moveValue)));
	moveTo->setToolTip(tr("Move to Position (m_fMoveTo). Walk is Hammer's default: without a way to the script the NPC never "
	                      "arrives and the script never plays."));
	connect(moveTo, &QComboBox::activated, this, [this, id, moveTo](int index) { setKey({id}, "m_fMoveTo", moveTo->itemData(index).toString()); });
	form->addRow(tr("Move to position"), moveTo);
	for (auto* combo : {loop, moveTo}) {
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		combo->setMinimumContentsLength(8);
	}
	v->addLayout(form);

	const long long flags = flagsOf(script);
	const auto flagBox = [this, id, v](const QString& text, const QString& tip, long long bits, bool on, const char* name) {
		auto* check = new QCheckBox(text);
		check->setObjectName(name);
		check->setToolTip(tip);
		check->setChecked(on);
		connect(check, &QCheckBox::toggled, this, [this, id, bits](bool checked) {
			const Entity* e = m_editor->doc().findEntity(id);
			if (!e) return;
			const long long value = checked ? (flagsOf(*e) | bits) : (flagsOf(*e) & ~bits);
			setKey({id}, "spawnflags", QString::number(value));
		});
		v->addWidget(check);
	};
	flagBox(tr("Not interrupted"), tr("No Interruptions and Override AI: enemies, damage and the NPC's own AI do not stop the scene"), script_flags::NO_INTERRUPTIONS | script_flags::OVERRIDE_AI,
	        (flags & (script_flags::NO_INTERRUPTIONS | script_flags::OVERRIDE_AI)) == (script_flags::NO_INTERRUPTIONS | script_flags::OVERRIDE_AI),
	        "scriptNoInterrupt");
	flagBox(tr("Repeatable"), tr("Can play again; without it the script is removed once it has played"), script_flags::REPEATABLE,
	        (flags & script_flags::REPEATABLE) != 0, "scriptRepeatable");
	v->addWidget(noteLabel(tr("Pre Action Idle: while the NPC waits for the start. Action: plays on BeginSequence. Post Action Idle: "
	                     "after the Action. Search Radius only matters when Target NPC is a class name.")));
	layout->addWidget(box);
}

void PropertiesPanel::buildCitizenLook(QVBoxLayout* layout, const Entity& citizen) {
	const int id = citizen.id;
	auto* box = new QGroupBox(tr("Look"));
	box->setObjectName("citizenLook");
	auto* v = new QVBoxLayout(box);
	const bool fixed = citizen.get("citizentype") == "4";
	const QString model = qs(normalizePath(citizen.get("model")));
	auto* state = new QLabel(fixed ? tr("Fixed look: %1").arg(model.section('/', -1))
	                               : tr("Random head each time the map starts - the model only picks male or female."));
	state->setObjectName("citizenLookState");
	state->setWordWrap(true);
	v->addWidget(state);
	auto* row = new QHBoxLayout;
	auto* heads = new QComboBox;
	heads->setObjectName("citizenHead");
	for (const auto& head : citizenModels()) {
		const QString path = qs(head);
		heads->addItem(path.mid(QStringLiteral("models/humans/").size()), path);
	}
	if (const int at = heads->findData(model); at >= 0) heads->setCurrentIndex(at);
	heads->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	heads->setMinimumContentsLength(8);
	auto* keep = new QPushButton(fixed ? tr("Use this head") : tr("Keep this head"));
	keep->setObjectName("citizenKeepLook");
	keep->setToolTip(tr("Citizen type Unique with this model: the same head every time (group03 = rebels, group03m = medics)"));
	row->addWidget(heads, 1);
	row->addWidget(keep);
	v->addLayout(row);
	connect(keep, &QPushButton::clicked, this, [this, id, heads] {
		m_editor->commit(edit::keepCitizenLook(m_editor->doc(), id, heads->currentData().toString().toStdString()));
	});
	layout->addWidget(box);
}

} // namespace sme::app
