// properties_door.cpp - The "Door" box of the properties panel for func_door,
// func_door_rotating and prop_door_rotating: how the door opens (swing with the hinge on the
// left or right, which way it turns, or slide up/down/left/right), how far and fast, when it
// closes, locked, how it is opened, its sounds with play buttons, "Test open" (the views play
// the motion) and the func_areaportal in its doorway. Every change is one undo step
// (edit::setDoor); the view draws the motion of the selected door, so the result is visible
// at once. Part of PropertiesPanel, kept in its own file so the door feature stays in one place.

#include "properties_panel.h"

#include "asset_browser.h"
#include "editor.h"
#include "sound_player.h"
#include "toggle_switch.h"

#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <cmath>

#include <sme/assets.h>
#include <sme/audio.h>
#include <sme/doors.h>
#include <sme/game.h>
#include <sme/text.h>

namespace sme::app {

namespace {

QLabel* note(const QString& text) {
	auto* label = new QLabel(text);
	label->setWordWrap(true);
	label->setProperty("role", "description");
	return label;
}

QString qs(const std::string& s) {
	return QString::fromStdString(s);
}

/// "Opens" choices of a brush door: (type, hinge or slide) packed into one item value.
struct OpenChoice {
	DoorType type;
	DoorHinge hinge;
	DoorSlide slide;
	const char* text;
};
const OpenChoice OPEN_CHOICES[] = {
	{DoorType::Swinging, DoorHinge::Left, DoorSlide::Up, QT_TRANSLATE_NOOP("PropertiesPanel", "Swings - hinge on the left")},
	{DoorType::Swinging, DoorHinge::Right, DoorSlide::Up, QT_TRANSLATE_NOOP("PropertiesPanel", "Swings - hinge on the right")},
	{DoorType::Sliding, DoorHinge::Left, DoorSlide::Up, QT_TRANSLATE_NOOP("PropertiesPanel", "Slides up")},
	{DoorType::Sliding, DoorHinge::Left, DoorSlide::Down, QT_TRANSLATE_NOOP("PropertiesPanel", "Slides down")},
	{DoorType::Sliding, DoorHinge::Left, DoorSlide::Left, QT_TRANSLATE_NOOP("PropertiesPanel", "Slides left")},
	{DoorType::Sliding, DoorHinge::Left, DoorSlide::Right, QT_TRANSLATE_NOOP("PropertiesPanel", "Slides right")},
};

/// One sentence saying what the door does (top of the box).
QString summary(const DoorSettings& s) {
	QString how;
	if (s.type == DoorType::Sliding) {
		const char* where[] = {"up", "down", "to the left", "to the right", "along its movedir"};
		how = PropertiesPanel::tr("Slides %1").arg(PropertiesPanel::tr(where[int(s.slide)]));
	} else {
		const QString way = s.swing == DoorSwing::AwayFromPlayer ? PropertiesPanel::tr("away from the player")
		                    : s.swing == DoorSwing::Front        ? PropertiesPanel::tr("always to the front")
		                                                         : PropertiesPanel::tr("always to the back");
		how = PropertiesPanel::tr("Swings %1° %2").arg(s.angle).arg(way);
	}
	QStringList opens;
	if (s.type == DoorType::Prop || s.useOpens) opens << PropertiesPanel::tr("Use");
	if (s.type != DoorType::Prop && s.touchOpens) opens << PropertiesPanel::tr("touch");
	QString text = how;
	text += opens.isEmpty() ? PropertiesPanel::tr(" · opens only by outputs") : PropertiesPanel::tr(" · opens on %1").arg(opens.join(PropertiesPanel::tr(" or ")));
	if (s.toggle) {
		text += PropertiesPanel::tr(" · stays until used again");
	} else {
		text += s.closeAfter < 0 ? PropertiesPanel::tr(" · stays open") : PropertiesPanel::tr(" · closes after %1 s").arg(s.closeAfter);
	}
	if (s.locked) text += PropertiesPanel::tr(" · locked");
	return text;
}

} // namespace

void PropertiesPanel::buildDoorBox(QVBoxLayout* layout, const Entity& door) {
	const int id = door.id;
	const DoorSettings current = readDoor(door);
	const bool prop = current.type == DoorType::Prop;
	auto* box = new QGroupBox(tr("Door"));
	box->setObjectName("doorBox");
	auto* v = new QVBoxLayout(box);
	auto* what = new QLabel(QStringLiteral("<b>%1</b>").arg(summary(current).toHtmlEscaped()));
	what->setObjectName("doorSummary");
	what->setWordWrap(true);
	v->addWidget(what);

	// Every editor changes one field of the door's current settings and writes them back.
	const auto apply = [this, id](const std::function<void(DoorSettings&)>& change) {
		const Entity* entity = m_editor->doc().findEntity(id);
		if (!entity) return;
		DoorSettings s = readDoor(*entity);
		change(s);
		m_editor->commit(edit::setDoor(m_editor->doc(), id, s));
	};
	auto* form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	if (!prop) {
		auto* opens = new QComboBox;
		opens->setObjectName("doorOpens");
		int currentIndex = -1;
		for (int i = 0; i < int(std::size(OPEN_CHOICES)); ++i) {
			const auto& choice = OPEN_CHOICES[i];
			opens->addItem(tr(choice.text), i);
			const bool match = choice.type == current.type &&
			                   (choice.type == DoorType::Swinging ? choice.hinge == current.hinge : choice.slide == current.slide);
			if (match) currentIndex = i;
		}
		if (currentIndex < 0) {
			opens->addItem(tr("Slides along its own movedir"), -1);
			currentIndex = opens->count() - 1;
		}
		opens->setCurrentIndex(currentIndex);
		opens->setToolTip(tr("Left and right as seen from the door's front; the view shows the motion of the selected door"));
		connect(opens, &QComboBox::activated, this, [apply, opens](int index) {
			const int choice = opens->itemData(index).toInt();
			if (choice < 0) return;
			apply([choice](DoorSettings& s) {
				s.type = OPEN_CHOICES[choice].type;
				if (s.type == DoorType::Swinging) {
					s.hinge = OPEN_CHOICES[choice].hinge;
				} else {
					s.slide = OPEN_CHOICES[choice].slide;
				}
			});
		});
		form->addRow(tr("Opens"), opens);
	}

	if (current.type != DoorType::Sliding) {
		auto* swing = new QComboBox;
		swing->setObjectName("doorSwing");
		swing->addItem(tr("Away from the player"), int(DoorSwing::AwayFromPlayer));
		swing->addItem(tr("Always to the front"), int(DoorSwing::Front));
		swing->addItem(tr("Always to the back"), int(DoorSwing::Back));
		swing->setCurrentIndex(swing->findData(int(current.swing)));
		swing->setToolTip(tr("Away from the player: it never hits whoever opens it. The front is the side the arrow points to."));
		connect(swing, &QComboBox::activated, this, [apply, swing](int index) {
			const auto way = DoorSwing(swing->itemData(index).toInt());
			apply([way](DoorSettings& s) { s.swing = way; });
		});
		form->addRow(tr("Turns"), swing);
		auto* angle = new QSpinBox;
		angle->setObjectName("doorAngle");
		angle->setRange(1, 360);
		angle->setSuffix(QStringLiteral("°"));
		angle->setKeyboardTracking(false);
		angle->setValue(int(std::lround(current.angle)));
		connect(angle, &QSpinBox::valueChanged, this, [apply](int value) { apply([value](DoorSettings& s) { s.angle = value; }); });
		form->addRow(tr("Angle"), angle);
	}

	auto* speed = new QSpinBox;
	speed->setObjectName("doorSpeed");
	speed->setRange(1, 10000);
	speed->setSingleStep(10);
	speed->setKeyboardTracking(false);
	speed->setSuffix(current.type == DoorType::Sliding ? tr(" units/s") : tr(" °/s"));
	speed->setValue(int(std::lround(current.speed)));
	connect(speed, &QSpinBox::valueChanged, this, [apply](int value) { apply([value](DoorSettings& s) { s.speed = value; }); });
	form->addRow(tr("Speed"), speed);

	// Closing: stays open, or closes after some seconds.
	auto* closeRow = new QHBoxLayout;
	auto* staysOpen = new ToggleSwitch;
	staysOpen->setObjectName("doorStaysOpen");
	staysOpen->setChecked(current.closeAfter < 0 || current.toggle);
	closeRow->addWidget(staysOpen);
	closeRow->addWidget(new QLabel(tr("Stays open")));
	auto* closeAfter = new QDoubleSpinBox;
	closeAfter->setObjectName("doorCloseAfter");
	closeAfter->setRange(0, 600);
	closeAfter->setDecimals(1);
	closeAfter->setPrefix(tr("else closes after "));
	closeAfter->setSuffix(tr(" s"));
	closeAfter->setKeyboardTracking(false);
	closeAfter->setValue(current.closeAfter < 0 ? 4 : current.closeAfter);
	closeAfter->setEnabled(!staysOpen->isChecked());
	closeRow->addWidget(closeAfter, 1);
	form->addRow(tr("Closing"), closeRow);
	connect(staysOpen, &QCheckBox::toggled, this, [apply, closeAfter](bool on) {
		const double seconds = closeAfter->value();
		apply([on, seconds](DoorSettings& s) {
			s.toggle = false;
			s.closeAfter = on ? -1 : seconds;
		});
	});
	connect(closeAfter, &QDoubleSpinBox::valueChanged, this, [apply](double value) { apply([value](DoorSettings& s) { s.closeAfter = value; }); });

	// Switches: locked, and for brush doors how the player opens it.
	const auto toggleRow = [&](const QString& label, const char* name, bool on, const QString& tip, std::function<void(DoorSettings&, bool)> set) {
		auto* row = new QHBoxLayout;
		auto* toggle = new ToggleSwitch;
		toggle->setObjectName(QString::fromLatin1(name));
		toggle->setChecked(on);
		toggle->setToolTip(tip);
		row->addWidget(toggle);
		row->addWidget(new QLabel(label), 1);
		connect(toggle, &QCheckBox::toggled, this, [apply, set](bool value) { apply([set, value](DoorSettings& s) { set(s, value); }); });
		v->addLayout(row);
	};
	v->addLayout(form);
	if (!prop) {
		toggleRow(tr("Opens when the player presses Use"), "doorUseOpens", current.useOpens, tr("Use Opens (spawnflag 256)"),
		          [](DoorSettings& s, bool on) { s.useOpens = on; });
		toggleRow(tr("Opens when the player walks into it"), "doorTouchOpens", current.touchOpens, tr("Touch Opens (spawnflag 1024)"),
		          [](DoorSettings& s, bool on) { s.touchOpens = on; });
	}
	toggleRow(tr("Starts locked (an Unlock input opens it up)"), "doorLocked", current.locked, tr("Starts locked (spawnflag 2048); plays the Locked sound when used"),
	          [](DoorSettings& s, bool on) { s.locked = on; });

	if (prop) {
		// The door model and its handle.
		auto* modelForm = new QFormLayout;
		auto* model = new QComboBox;
		model->setObjectName("doorModel");
		model->setEditable(true);
		model->setInsertPolicy(QComboBox::NoInsert);
		std::vector<std::string> names;
		for (const auto& name : m_assets->modelNames()) names.push_back(name.toStdString());
		QStringList doorModelNames;
		for (const auto& name : doorModels(names)) doorModelNames << qs(name);
		model->addItems(doorModelNames);
		model->setEditText(qs(current.model));
		connect(model, &QComboBox::activated, this, [apply, model](int) {
			const std::string chosen = model->currentText().toStdString();
			apply([chosen](DoorSettings& s) { s.model = chosen; });
		});
		connect(model->lineEdit(), &QLineEdit::editingFinished, this, [apply, model, current] {
			const std::string chosen = model->currentText().toStdString();
			if (chosen != current.model) apply([chosen](DoorSettings& s) { s.model = chosen; });
		});
		modelForm->addRow(tr("Model"), model);
		auto* hardware = new QComboBox;
		hardware->setObjectName("doorHardware");
		hardware->addItems({tr("None"), tr("Lever"), tr("Push bar"), tr("Keypad")});
		hardware->setCurrentIndex(std::clamp(current.hardware, 0, 3));
		connect(hardware, &QComboBox::activated, this, [apply](int index) { apply([index](DoorSettings& s) { s.hardware = index; }); });
		hardware->setItemData(0, tr("No handle: opens with +use on the door itself"), Qt::ToolTipRole);
		hardware->setItemData(1, tr("A lever on both faces, at the free edge (away from the hinge)"), Qt::ToolTipRole);
		hardware->setItemData(2, tr("A push bar across the door, at the free edge"), Qt::ToolTipRole);
		hardware->setItemData(3, tr("A keypad beside the free edge"), Qt::ToolTipRole);
		modelForm->addRow(tr("Handle"), hardware);
		v->addLayout(modelForm);
		auto* handleHint = new QLabel(tr("The 3D view shows the door with this handle, so you see where it sits."));
		handleHint->setWordWrap(true);
		handleHint->setStyleSheet(QStringLiteral("color: #9aa3ad;"));
		v->addWidget(handleHint);
	}

	// Sounds: door sound scripts first in the lists, each with a play button.
	QStringList doorSounds;
	for (const auto& name : m_assets->soundNames()) {
		if (isDoorSound(name.toStdString())) doorSounds << name;
	}
	auto* soundModel = sharedNames(QStringLiteral("doorsound"), doorSounds);
	auto* sounds = new QFormLayout;
	const auto soundSlots = doorSoundSlots(current.type);
	for (std::size_t i = 0; i < soundSlots.size(); ++i) {
		auto* row = new QHBoxLayout;
		auto* combo = new QComboBox;
		combo->setObjectName(QStringLiteral("doorSound:%1").arg(i));
		combo->setEditable(true);
		combo->setInsertPolicy(QComboBox::NoInsert);
		combo->setModel(soundModel);
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		combo->setMinimumContentsLength(10);
		if (auto* list = qobject_cast<QListView*>(combo->view())) list->setUniformItemSizes(true);
		auto* completer = new QCompleter(soundModel, combo);
		completer->setCaseSensitivity(Qt::CaseInsensitive);
		completer->setFilterMode(Qt::MatchContains);
		combo->setCompleter(completer);
		const QString value = i < current.sounds.size() ? qs(current.sounds[i]) : QString();
		combo->setEditText(value);
		combo->lineEdit()->setPlaceholderText(prop ? tr("(the model's own)") : tr("(none)"));
		auto* play = new QToolButton;
		play->setObjectName(QStringLiteral("doorPlay:%1").arg(i));
		play->setText(QStringLiteral("▶"));
		play->setToolTip(tr("Play this sound"));
		connect(play, &QToolButton::clicked, this, [this, combo] {
			if (!combo->currentText().isEmpty()) playSound(combo->currentText());
		});
		const auto store = [apply, combo, i, value] {
			const std::string chosen = combo->currentText().trimmed().toStdString();
			if (qs(chosen) == value) return;
			apply([i, chosen](DoorSettings& s) {
				if (i < s.sounds.size()) s.sounds[i] = chosen;
			});
		};
		connect(combo, &QComboBox::activated, this, store);
		connect(combo->lineEdit(), &QLineEdit::editingFinished, this, store);
		row->addWidget(combo, 1);
		row->addWidget(play);
		sounds->addRow(tr(soundSlots[i].label.c_str()), row);
	}
	auto* soundTitle = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Sounds")));
	v->addWidget(soundTitle);
	v->addLayout(sounds);

	// Test open: the views play the opening once.
	auto* test = new QPushButton(tr("Test open"));
	test->setObjectName("doorTest");
	test->setToolTip(tr("Plays the opening in the view at the door's speed"));
	connect(test, &QPushButton::clicked, this, [this, id] { m_editor->testDoor(id); });
	v->addWidget(test);

	// Areaportal in the doorway: linked, or one click to link it.
	if (const Entity* portal = doorAreaportal(m_editor->doc(), door)) {
		if (areaportalLinked(*portal, door)) {
			auto* linked = note(tr("Linked to the func_areaportal in its doorway: what is behind the closed door is not drawn."));
			linked->setObjectName("doorPortalLinked");
			v->addWidget(linked);
		} else {
			auto* warn = new QLabel(tr("A func_areaportal sits in this doorway but is not linked to the door - it would never open."));
			warn->setProperty("role", "warning");
			warn->setWordWrap(true);
			v->addWidget(warn);
			auto* link = new QPushButton(tr("Link areaportal"));
			link->setObjectName("doorLinkPortal");
			const int portalId = portal->id;
			connect(link, &QPushButton::clicked, this, [this, id, portalId] { m_editor->commit(edit::linkAreaportal(m_editor->doc(), id, portalId)); });
			v->addWidget(link);
		}
	} else {
		v->addWidget(note(tr("Between two rooms? Put a func_areaportal brush (tools/toolsareaportal) in the doorway: the door links it here and "
		                     "the game then skips what is behind the closed door.")));
	}
	layout->addWidget(box);
}

void PropertiesPanel::playSound(const QString& name) {
	LoadedGame* game = m_editor->game();
	if (!game) {
		return;
	}
	if (!m_soundPlayer) {
		m_soundPlayer = new SoundPlayer(this);
		connect(m_soundPlayer, &SoundPlayer::error, this, [this](const QString& text) { emit m_editor->message(text); });
		connect(&m_soundWatcher, &QFutureWatcherBase::finished, this, [this] {
			m_lastSound = m_soundWatcher.result();
			if (!m_lastSound) {
				emit m_editor->message(tr("This sound has no readable file in the game"));
				return;
			}
			m_soundPlayer->play(m_lastSound);
		});
		// The file system goes away with the game: let a running read finish first.
		connect(m_editor, &Editor::gameAboutToChange, this, [this] { m_soundWatcher.waitForFinished(); });
	}
	m_soundWatcher.waitForFinished();
	m_lastSound.reset();
	const GameFileSystem* fs = &game->files;
	const std::string sound = name.trimmed().toStdString();
	m_soundWatcher.setFuture(QtConcurrent::run([fs, sound]() -> std::shared_ptr<const DecodedAudio> {
		// A sound script name plays its first wave; a file name plays itself.
		std::string wave = sound;
		if (!endsWithI(sound, ".wav") && !endsWithI(sound, ".mp3") && !endsWithI(sound, ".ogg")) {
			wave.clear();
			for (const auto& entry : soundScripts(*fs)) {
				if (iequals(entry.name, sound) && !entry.waves.empty()) {
					wave = entry.waves.front();
					break;
				}
			}
		}
		if (wave.empty()) return nullptr;
		const std::string path = "sound/" + stripSoundChars(wave);
		const auto data = fs->read(path);
		if (!data) return nullptr;
		auto audio = decodeAudio(*data, path);
		if (!audio) return nullptr;
		return std::make_shared<const DecodedAudio>(std::move(*audio));
	}));
}

} // namespace sme::app
