// soundscape_editor.cpp - Soundscape editor of the Sounds page (see soundscape_editor.h).

#include "soundscape_editor.h"

#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QStringListModel>
#include <QToolButton>
#include <QVBoxLayout>

#include <random>

#include <sme/assets.h>

namespace sme::app {

namespace {

// Rules as cards under small capital headers (local, the app style stays as it is).
const char* STYLE = R"(
QFrame#soundscapeRule { background: #2a2c31; border: 1px solid #3a3d44; border-radius: 6px; }
QFrame#soundscapeRule QLabel { background: transparent; }
QLabel[role="caps"] { color: #9399a3; font-size: 8pt; font-weight: 700; letter-spacing: 1px; }
QLabel[role="ruleKind"] { color: #f0a050; font-size: 8pt; font-weight: 700; letter-spacing: 1px; }
QLabel[role="hint"] { color: #8a90a0; }
QToolButton[role="cardButton"] { border: none; padding: 2px 6px; border-radius: 3px; }
QToolButton[role="cardButton"]:hover { background: #3c4047; }
)";

const QStringList SOUND_LEVELS = {"",            "SNDLVL_NORM", "SNDLVL_NONE", "SNDLVL_20dB", "SNDLVL_30dB", "SNDLVL_45dB", "SNDLVL_55dB",
                                  "SNDLVL_60dB", "SNDLVL_65dB", "SNDLVL_70dB", "SNDLVL_75dB", "SNDLVL_80dB", "SNDLVL_85dB", "SNDLVL_90dB",
                                  "SNDLVL_95dB", "SNDLVL_100dB", "SNDLVL_120dB", "SNDLVL_140dB"};

QLabel* caps(const QString& text, const char* role = "caps") {
	auto* label = new QLabel(text.toUpper());
	label->setProperty("role", role);
	return label;
}

QString qs(const std::string& s) {
	return QString::fromStdString(s);
}

/// A text field bound to one string of the soundscape.
QLineEdit* boundEdit(std::string& target, const QString& placeholder, const QString& tip = {}) {
	auto* edit = new QLineEdit(qs(target));
	edit->setPlaceholderText(placeholder);
	if (!tip.isEmpty()) edit->setToolTip(tip);
	QObject::connect(edit, &QLineEdit::textChanged, edit, [&target](const QString& text) { target = text.trimmed().toStdString(); });
	return edit;
}

QComboBox* boundCombo(std::string& target, const QStringList& items, const QString& tip) {
	auto* combo = new QComboBox;
	combo->setEditable(true);
	combo->addItems(items);
	combo->setCurrentText(qs(target));
	combo->setToolTip(tip);
	QObject::connect(combo, &QComboBox::currentTextChanged, combo, [&target](const QString& text) { target = text.trimmed().toStdString(); });
	return combo;
}

/// File to play for a wave as written ("*ambient/hum.wav" -> "ambient/hum.wav").
QString playable(const std::string& wave) {
	return qs(stripSoundChars(wave));
}

} // namespace

SoundscapeEditor::SoundscapeEditor(QWidget* parent) : QWidget(parent), m_fileCompleter(new QCompleter(this)) {
	setObjectName("soundscapeEditor");
	setStyleSheet(STYLE);
	m_fileCompleter->setModel(new QStringListModel(m_fileCompleter));
	m_fileCompleter->setCaseSensitivity(Qt::CaseInsensitive);
	m_fileCompleter->setFilterMode(Qt::MatchContains);

	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(8, 4, 4, 4);
	v->setSpacing(8);
	auto* head = new QHBoxLayout;
	head->addWidget(caps(tr("Soundscape")));
	head->addStretch(1);
	v->addLayout(head);

	auto* form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignLeft);
	m_name = new QLineEdit;
	m_name->setObjectName("soundscapeName");
	m_name->setPlaceholderText(tr("mymod.cave_drips"));
	m_name->setToolTip(tr("The name env_soundscape uses (its \"soundscape\" key)"));
	m_dsp = new QComboBox;
	m_dsp->setObjectName("soundscapeDsp");
	m_dsp->setEditable(true);
	m_dsp->addItem(tr("(not set)"), QString());
	for (const auto& preset : dspPresets()) {
		m_dsp->addItem(QStringLiteral("%1 · %2").arg(preset.id).arg(QString::fromUtf8(preset.name)), QString::number(preset.id));
	}
	m_dsp->setToolTip(tr("Room effect (echo) while this soundscape plays: the dsp number of the engine"));
	m_fade = new QLineEdit;
	m_fade->setObjectName("soundscapeFade");
	m_fade->setPlaceholderText(tr("seconds (empty: the engine's default)"));
	form->addRow(tr("Name"), m_name);
	form->addRow(tr("Room effect"), m_dsp);
	form->addRow(tr("Fade in"), m_fade);
	v->addLayout(form);
	m_source = new QLabel;
	m_source->setProperty("role", "hint");
	m_source->setWordWrap(true);
	v->addWidget(m_source);

	auto* rulesHead = new QHBoxLayout;
	rulesHead->addWidget(caps(tr("Sounds")));
	rulesHead->addStretch(1);
	const auto addButton = [&](const QString& text, const QString& name, const QString& tip, SoundscapeRule::Kind kind) {
		auto* b = new QPushButton(text);
		b->setObjectName(name);
		b->setToolTip(tip);
		connect(b, &QPushButton::clicked, this, [this, kind] { addRule(kind); });
		rulesHead->addWidget(b);
	};
	addButton(tr("+ Looping"), "addLooping", tr("A sound that plays all the time (wind, hum)"), SoundscapeRule::Kind::Looping);
	addButton(tr("+ Random"), "addRandom", tr("One of several sounds every few seconds (birds, creaks)"), SoundscapeRule::Kind::Random);
	addButton(tr("+ Soundscape"), "addSubSoundscape", tr("Plays another soundscape inside this one"), SoundscapeRule::Kind::Soundscape);
	v->addLayout(rulesHead);

	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* holder = new QWidget;
	m_rules = new QVBoxLayout(holder);
	m_rules->setContentsMargins(0, 0, 4, 0);
	m_rules->setSpacing(8);
	m_rules->addStretch(1);
	scroll->setWidget(holder);
	v->addWidget(scroll, 1);

	auto* foot = new QHBoxLayout;
	m_status = new QLabel;
	m_status->setObjectName("soundscapeStatus");
	m_status->setProperty("role", "hint");
	m_status->setWordWrap(true);
	m_delete = new QPushButton(tr("Delete"));
	m_delete->setObjectName("deleteSoundscape");
	m_delete->setToolTip(tr("Removes this soundscape from the mod's own soundscape file"));
	m_save = new QPushButton(tr("Save to the mod"));
	m_save->setObjectName("saveSoundscape");
	m_save->setProperty("accent", true);
	foot->addWidget(m_status, 1);
	foot->addWidget(m_delete);
	foot->addWidget(m_save);
	v->addLayout(foot);

	connect(m_name, &QLineEdit::textChanged, this, [this](const QString& text) { m_scape.name = text.trimmed().toStdString(); });
	connect(m_dsp, &QComboBox::currentTextChanged, this, [this](const QString& text) {
		const int index = m_dsp->findText(text);
		if (index >= 0) {
			m_scape.dsp = m_dsp->itemData(index).toString().toStdString();
		} else {
			// Typed: the number in front ("31", "31 · own preset").
			m_scape.dsp = text.section(QLatin1Char(' '), 0, 0).trimmed().toStdString();
		}
	});
	connect(m_fade, &QLineEdit::textChanged, this, [this](const QString& text) { m_scape.fadetime = text.trimmed().toStdString(); });
	connect(m_save, &QPushButton::clicked, this, [this] { emit saveRequested(m_scape); });
	connect(m_delete, &QPushButton::clicked, this, [this] { emit deleteRequested(qs(m_scape.name)); });
	newSoundscape(QStringLiteral("mymod.new_soundscape"));
}

void SoundscapeEditor::setSoundscape(const SoundscapeScript& scape, const QString& file, bool inMod) {
	m_scape = scape;
	const QSignalBlocker a(m_name), b(m_dsp), c(m_fade);
	m_name->setText(qs(scape.name));
	m_fade->setText(qs(scape.fadetime));
	const int dsp = scape.dsp.empty() ? 0 : m_dsp->findData(qs(scape.dsp));
	if (dsp >= 0) {
		m_dsp->setCurrentIndex(dsp);
	} else {
		m_dsp->setEditText(qs(scape.dsp));
	}
	m_source->setText(inMod ? tr("In the mod: %1").arg(file)
	                        : tr("From the game: %1 - saving puts a changed copy into the mod's own file").arg(file));
	m_delete->setEnabled(inMod);
	m_status->clear();
	rebuildRules();
}

void SoundscapeEditor::newSoundscape(const QString& suggestedName) {
	SoundscapeScript scape;
	scape.name = suggestedName.toStdString();
	setSoundscape(scape, QString(), false);
	m_source->setText(tr("New soundscape: give it a name, add sounds, then save it into the mod. Put an env_soundscape "
	                     "with this name into the map."));
	m_name->setFocus();
	m_name->selectAll();
}

void SoundscapeEditor::setSoundscapeNames(const QStringList& names) {
	m_names = names;
}

void SoundscapeEditor::setSoundFiles(const QStringList& files) {
	static_cast<QStringListModel*>(m_fileCompleter->model())->setStringList(files);
}

void SoundscapeEditor::setStatus(const QString& text) {
	m_status->setText(text);
}

QString SoundscapeEditor::status() const {
	return m_status->text();
}

void SoundscapeEditor::addRule(SoundscapeRule::Kind kind) {
	SoundscapeRule rule;
	rule.kind = kind;
	switch (kind) {
		case SoundscapeRule::Kind::Looping:
			rule.volume = "1";
			if (m_shownFile) rule.wave = m_shownFile().toStdString();
			break;
		case SoundscapeRule::Kind::Random:
			rule.volume = "0.5,0.8";
			rule.time = "5,10";
			if (m_shownFile && !m_shownFile().isEmpty()) rule.waves.push_back(m_shownFile().toStdString());
			break;
		case SoundscapeRule::Kind::Soundscape: rule.volume = "1"; break;
		case SoundscapeRule::Kind::Other: break;
	}
	m_scape.rules.push_back(std::move(rule));
	rebuildRules();
}

void SoundscapeEditor::rebuildRules() {
	while (m_rules->count() > 1) {
		auto* item = m_rules->takeAt(0);
		delete item->widget();
		delete item;
	}
	for (int i = 0; i < int(m_scape.rules.size()); ++i) {
		m_rules->insertWidget(i, makeRuleCard(i));
	}
	if (m_scape.rules.empty()) {
		auto* empty = new QLabel(tr("No sounds yet - add a looping sound, random sounds or another soundscape above."));
		empty->setProperty("role", "hint");
		empty->setWordWrap(true);
		m_rules->insertWidget(0, empty);
	}
}

QWidget* SoundscapeEditor::makeRuleCard(int index) {
	using Kind = SoundscapeRule::Kind;
	auto& rule = m_scape.rules[static_cast<std::size_t>(index)];
	auto* card = new QFrame;
	card->setObjectName("soundscapeRule");
	auto* v = new QVBoxLayout(card);
	v->setContentsMargins(10, 8, 10, 10);
	v->setSpacing(6);

	auto* head = new QHBoxLayout;
	const QString kind = rule.kind == Kind::Looping ? tr("Looping sound")
	                     : rule.kind == Kind::Random ? tr("Random sounds")
	                     : rule.kind == Kind::Soundscape ? tr("Soundscape inside")
	                                                     : qs(rule.key);
	head->addWidget(caps(kind, "ruleKind"));
	head->addStretch(1);
	const auto cardButton = [&](const QString& text, const QString& tip, const QString& name) {
		auto* b = new QToolButton;
		b->setText(text);
		b->setToolTip(tip);
		b->setObjectName(name);
		b->setProperty("role", "cardButton");
		head->addWidget(b);
		return b;
	};
	auto* listen = cardButton(QStringLiteral("▶"), tr("Listen"), "ruleListen");
	auto* up = cardButton(QStringLiteral("↑"), tr("Move up"), "ruleUp");
	auto* remove = cardButton(QStringLiteral("✕"), tr("Remove this rule"), "ruleRemove");
	up->setEnabled(index > 0);
	connect(up, &QToolButton::clicked, this, [this, index] {
		std::swap(m_scape.rules[static_cast<std::size_t>(index)], m_scape.rules[static_cast<std::size_t>(index - 1)]);
		QMetaObject::invokeMethod(this, &SoundscapeEditor::rebuildRules, Qt::QueuedConnection);   // not while the button is busy
	});
	connect(remove, &QToolButton::clicked, this, [this, index] {
		m_scape.rules.erase(m_scape.rules.begin() + index);
		QMetaObject::invokeMethod(this, &SoundscapeEditor::rebuildRules, Qt::QueuedConnection);
	});
	v->addLayout(head);

	auto* form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignLeft);
	form->setVerticalSpacing(4);
	const QString volumeTip = tr("0 to 1; a range (0.5,0.8) picks a value each time");
	const QString positionTip = tr("Empty: everywhere around the player. 0-7: from the position target of that number "
	                               "set on env_soundscape (position0 ... position7)");
	switch (rule.kind) {
		case Kind::Looping: {
			auto* wave = boundEdit(rule.wave, tr("ambient/wind1.wav"), tr("File below sound/ (or a sound script name)"));
			wave->setObjectName("ruleWave");
			wave->setCompleter(m_fileCompleter);
			auto* row = new QHBoxLayout;
			row->addWidget(wave, 1);
			auto* shown = new QPushButton(tr("Use shown"));
			shown->setToolTip(tr("The sound shown in the player"));
			connect(shown, &QPushButton::clicked, wave, [this, wave] {
				if (m_shownFile && !m_shownFile().isEmpty()) wave->setText(m_shownFile());
			});
			row->addWidget(shown);
			form->addRow(tr("Sound"), row);
			connect(listen, &QToolButton::clicked, this, [this, &rule] {
				if (!rule.wave.empty()) emit previewRequested(playable(rule.wave));
			});
			break;
		}
		case Kind::Random: {
			auto* list = new QListWidget;
			list->setObjectName("ruleWaves");
			list->setMaximumHeight(96);
			for (const auto& w : rule.waves) list->addItem(qs(w));
			auto* add = new QLineEdit;
			add->setPlaceholderText(tr("Type a sound file and press Enter ..."));
			add->setCompleter(m_fileCompleter);
			auto* buttons = new QHBoxLayout;
			auto* shown = new QPushButton(tr("+ Shown"));
			shown->setToolTip(tr("Add the sound shown in the player"));
			auto* drop = new QPushButton(tr("− Remove"));
			buttons->addWidget(add, 1);
			buttons->addWidget(shown);
			buttons->addWidget(drop);
			const auto sync = [list, &rule] {
				rule.waves.clear();
				for (int i = 0; i < list->count(); ++i) rule.waves.push_back(list->item(i)->text().toStdString());
			};
			const auto append = [list, sync](const QString& file) {
				if (file.trimmed().isEmpty() || !list->findItems(file.trimmed(), Qt::MatchFixedString).isEmpty()) return;
				list->addItem(file.trimmed());
				sync();
			};
			connect(add, &QLineEdit::returnPressed, list, [add, append] {
				append(add->text());
				add->clear();
			});
			connect(shown, &QPushButton::clicked, list, [this, append] {
				if (m_shownFile) append(m_shownFile());
			});
			connect(drop, &QPushButton::clicked, list, [list, sync] {
				delete list->currentItem();
				sync();
			});
			connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
				emit previewRequested(playable(item->text().toStdString()));
			});
			auto* files = new QVBoxLayout;
			files->setSpacing(4);
			files->addWidget(list);
			files->addLayout(buttons);
			form->addRow(tr("Sounds"), files);
			form->addRow(tr("Every"), boundEdit(rule.time, tr("5,10  (seconds, from-to)"), tr("Seconds between two sounds; a range picks each time")));
			connect(listen, &QToolButton::clicked, this, [this, list, &rule] {
				if (rule.waves.empty()) return;
				// The selected one, else one at random like the game.
				static std::mt19937 random{std::random_device{}()};
				const auto pick = list->currentRow() >= 0 ? std::size_t(list->currentRow())
				                                          : std::uniform_int_distribution<std::size_t>(0, rule.waves.size() - 1)(random);
				emit previewRequested(playable(rule.waves[std::min(pick, rule.waves.size() - 1)]));
			});
			break;
		}
		case Kind::Soundscape: {
			auto* name = boundCombo(rule.name, m_names, tr("The soundscape to play inside this one"));
			name->setObjectName("ruleSoundscape");
			name->setCurrentText(qs(rule.name));
			form->addRow(tr("Soundscape"), name);
			listen->setEnabled(false);
			break;
		}
		case Kind::Other: {
			auto* note = new QLabel(tr("Kept as it is (this editor does not change \"%1\" blocks).").arg(qs(rule.key)));
			note->setProperty("role", "hint");
			note->setWordWrap(true);
			v->addWidget(note);
			listen->setEnabled(false);
			return card;
		}
	}
	form->addRow(tr("Volume"), boundEdit(rule.volume, tr("1"), volumeTip));
	if (rule.kind == Kind::Soundscape) {
		form->addRow(tr("Position"), boundEdit(rule.positionOverride, tr("(as the soundscape says)"),
		                                       tr("All sounds of that soundscape come from this position number (0-7)")));
	} else {
		form->addRow(tr("Pitch"), boundEdit(rule.pitch, tr("100  (a range: 95,105)"), tr("100 is normal speed")));
		form->addRow(tr("Sound level"), boundCombo(rule.soundlevel, SOUND_LEVELS, tr("How far it carries (only for sounds with a position)")));
		form->addRow(tr("Position"), boundEdit(rule.position, tr("(everywhere)"), positionTip));
	}
	v->addLayout(form);
	return card;
}

} // namespace sme::app
