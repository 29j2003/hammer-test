// sound_browser.cpp - Sounds tab (see sound_browser.h).

#include "sound_browser.h"

#include "copy_paste.h"
#include "editor.h"
#include "sound_player.h"
#include "soundscape_editor.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <sme/editing.h>
#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/text.h>

#include <random>

namespace sme::app {

namespace {

constexpr int KIND_ROLE = Qt::UserRole;
constexpr int NAME_ROLE = Qt::UserRole + 1;
constexpr int INDEX_ROLE = Qt::UserRole + 2;

/// Tree whose sound items can be dragged into the 3D view.
class SoundTree : public QTreeWidget {
public:
	using QTreeWidget::QTreeWidget;

protected:
	QStringList mimeTypes() const override { return {"application/x-sme-sound"}; }
	QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
		if (items.isEmpty() || items.first()->data(0, KIND_ROLE).toInt() == 0) {
			return nullptr;
		}
		auto* mime = new QMimeData;
		// "<kind>:<name>" - the view places an ambient_generic or an env_soundscape.
		const QString payload = QStringLiteral("%1:%2").arg(items.first()->data(0, KIND_ROLE).toInt()).arg(items.first()->data(0, NAME_ROLE).toString());
		mime->setData("application/x-sme-sound", payload.toUtf8());
		mime->setText(items.first()->data(0, NAME_ROLE).toString());
		return mime;
	}
};

QTreeWidgetItem* groupItem(QTreeWidgetItem* parent, QMap<QString, QTreeWidgetItem*>& groups, const QString& name) {
	auto it = groups.find(name);
	if (it == groups.end()) {
		auto* item = new QTreeWidgetItem(parent, QStringList(name));
		item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
		it = groups.insert(name, item);
	}
	return *it;
}

} // namespace

// --- Waveform ------------------------------------------------------------------------

SoundWaveform::SoundWaveform(QWidget* parent) : QWidget(parent) {
	setMinimumHeight(70);
	setCursor(Qt::PointingHandCursor);
	setToolTip(tr("Click to play from there"));
}

void SoundWaveform::setAudio(std::shared_ptr<const DecodedAudio> audio) {
	m_audio = std::move(audio);
	m_playhead = -1;
	m_peaks = m_audio ? waveformPeaks(*m_audio, std::max(1, width())) : decltype(m_peaks){};
	update();
}

void SoundWaveform::setPlayhead(double seconds) {
	m_playhead = seconds;
	update();
}

void SoundWaveform::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
	if (m_audio) {
		m_peaks = waveformPeaks(*m_audio, std::max(1, width()));
	}
}

void SoundWaveform::mousePressEvent(QMouseEvent* event) {
	if (m_audio && m_audio->seconds() > 0) {
		emit seekRequested(std::clamp(event->position().x() / std::max(1, width()), 0.0, 1.0) * m_audio->seconds());
	}
}

void SoundWaveform::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.fillRect(rect(), QColor(24, 26, 31));
	const int mid = height() / 2;
	p.setPen(QColor(60, 64, 74));
	p.drawLine(0, mid, width(), mid);
	if (!m_audio) {
		p.setPen(QColor(130, 136, 150));
		p.drawText(rect(), Qt::AlignCenter, tr("Click a sound to hear it"));
		return;
	}
	const double length = std::max(1e-6, m_audio->seconds());
	// Loop part in a different color: what repeats in the game.
	const int loopX = m_audio->loopStart ? int(double(*m_audio->loopStart) / std::max<std::size_t>(1, m_audio->frames()) * width()) : width();
	for (int x = 0; x < static_cast<int>(m_peaks.size()); ++x) {
		const auto [low, high] = m_peaks[static_cast<std::size_t>(x)];
		p.setPen(x >= loopX ? QColor(90, 170, 255) : QColor(255, 160, 40));
		p.drawLine(x, mid - int(high * (mid - 2)), x, mid - int(low * (mid - 2)));
	}
	if (m_audio->loopStart) {
		p.setPen(QPen(QColor(120, 190, 255), 1, Qt::DashLine));
		p.drawLine(loopX, 0, loopX, height());
		p.drawText(loopX + 4, 12, tr("loop"));
	}
	if (m_playhead >= 0) {
		const int x = int(m_playhead / length * width());
		p.setPen(QPen(Qt::white, 1.5));
		p.drawLine(x, 0, x, height());
	}
	p.setPen(QColor(200, 205, 215));
	const QString time = m_playhead >= 0 ? tr("%1 / %2 s").arg(m_playhead, 0, 'f', 2).arg(length, 0, 'f', 2) : tr("%1 s").arg(length, 0, 'f', 2);
	p.drawText(rect().adjusted(4, 2, -4, -2), Qt::AlignRight | Qt::AlignBottom, time);
}

// --- Browser ---------------------------------------------------------------------------

SoundBrowser::SoundBrowser(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor), m_player(new SoundPlayer(this)) {
	auto* outer = new QHBoxLayout(this);
	outer->setContentsMargins(4, 4, 4, 4);
	auto* split = new QSplitter(Qt::Horizontal);
	outer->addWidget(split);

	// Left: search and the list.
	auto* left = new QWidget;
	auto* v = new QVBoxLayout(left);
	v->setContentsMargins(0, 0, 0, 0);
	auto* row = new QHBoxLayout;
	m_search = new QLineEdit;
	m_search->setPlaceholderText(tr("Search sounds, files and soundscapes (words: door metal) ..."));
	m_search->setClearButtonEnabled(true);
	m_kind = new QComboBox;
	m_kind->addItem(tr("Everything"), 0);
	m_kind->addItem(tr("Sound scripts"), int(ScriptSound));
	m_kind->addItem(tr("Sound files"), int(FileSound));
	m_kind->addItem(tr("Soundscapes"), int(SoundscapeEntry));
	auto* newScape = new QPushButton(tr("+ Soundscape"));
	newScape->setObjectName("newSoundscape");
	newScape->setToolTip(tr("Make a new soundscape for the mod"));
	row->addWidget(m_search, 1);
	row->addWidget(m_kind);
	row->addWidget(newScape);
	v->addLayout(row);
	m_tree = new SoundTree;
	m_tree->setColumnCount(2);
	m_tree->setHeaderLabels({tr("Name"), tr("Details")});
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_tree->header()->setStretchLastSection(false);
	m_tree->header()->resizeSection(1, 160);
	m_tree->setUniformRowHeights(true);
	m_tree->setDragEnabled(true);
	m_tree->setDragDropMode(QAbstractItemView::DragOnly);
	addCopyShortcut(m_tree, NAME_ROLE);   // Ctrl+C: sound name for an ambient_generic or a soundscape
	v->addWidget(m_tree, 1);
	m_info = new QLabel(tr("No game loaded"));
	m_info->setWordWrap(true);
	m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(m_info);
	split->addWidget(left);

	// Right: player and sound script editor.
	auto* right = new QWidget;
	auto* r = new QVBoxLayout(right);
	r->setContentsMargins(4, 0, 0, 0);
	m_title = new QLabel(tr("<b>No sound</b>"));
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	r->addWidget(m_title);
	m_waveform = new SoundWaveform;
	r->addWidget(m_waveform, 1);
	auto* controls = new QHBoxLayout;
	m_play = new QPushButton(tr("▶ Play"));
	m_play->setToolTip(SoundPlayer::available() ? tr("Play / stop (Space)") : tr("This build cannot play sounds"));
	m_play->setEnabled(SoundPlayer::available());
	m_loop = new QCheckBox(tr("Loop"));
	m_loop->setToolTip(tr("Sounds with a loop point (cue) repeat from there, like in the game"));
	m_loop->setChecked(true);
	m_playOnClick = new QCheckBox(tr("Play on click"));
	m_playOnClick->setChecked(QSettings().value("sounds/playOnClick", true).toBool());
	m_volume = new QSlider(Qt::Horizontal);
	m_volume->setRange(0, 100);
	m_volume->setValue(QSettings().value("sounds/volume2", 60).toInt());   // "2": the scale is in decibels now
	m_volume->setMaximumWidth(120);
	m_volume->setToolTip(tr("Volume of the preview (from silent up to as loud as the file)"));
	controls->addWidget(m_play);
	controls->addWidget(m_loop);
	controls->addWidget(m_playOnClick);
	controls->addStretch(1);
	controls->addWidget(new QLabel(tr("Volume")));
	controls->addWidget(m_volume);
	r->addLayout(controls);
	m_details = new QLabel;
	m_details->setStyleSheet("color: #8a90a0;");
	m_details->setWordWrap(true);
	r->addWidget(m_details);

	auto* box = new QGroupBox(tr("Sound script entry"));
	auto* form = new QFormLayout(box);
	m_entryName = new QLineEdit;
	m_entryName->setObjectName("soundEntryName");
	m_entryName->setPlaceholderText(tr("MyMod.DoorSqueak"));
	m_channel = new QComboBox;
	m_channel->setEditable(true);
	m_channel->addItems({"CHAN_AUTO", "CHAN_WEAPON", "CHAN_VOICE", "CHAN_ITEM", "CHAN_BODY", "CHAN_STREAM", "CHAN_STATIC", "CHAN_VOICE2"});
	m_entryVolume = new QLineEdit;
	m_entryVolume->setPlaceholderText(tr("1  (or a range: 0.7,1)"));
	m_pitch = new QLineEdit;
	m_pitch->setPlaceholderText(tr("PITCH_NORM  (100; a range: 95,105)"));
	m_soundlevel = new QComboBox;
	m_soundlevel->setEditable(true);
	m_soundlevel->addItems({"SNDLVL_NORM", "SNDLVL_NONE", "SNDLVL_20dB", "SNDLVL_30dB", "SNDLVL_45dB", "SNDLVL_55dB", "SNDLVL_60dB", "SNDLVL_65dB",
	                        "SNDLVL_70dB", "SNDLVL_75dB", "SNDLVL_80dB", "SNDLVL_85dB", "SNDLVL_90dB", "SNDLVL_95dB", "SNDLVL_100dB", "SNDLVL_120dB",
	                        "SNDLVL_140dB", "SNDLVL_GUNFIRE"});
	m_soundlevel->setToolTip(tr("How far it carries: 75 dB is normal, 60 dB quiet, 90 dB and more loud (gunfire)"));
	m_waves = new QListWidget;
	m_waves->setMinimumHeight(40);
	m_waves->setToolTip(tr("Files below sound/; with several the game picks one at random"));
	auto* waveButtons = new QHBoxLayout;
	auto* addWave = new QPushButton(tr("+ Shown file"));
	addWave->setToolTip(tr("Add the sound file shown above"));
	auto* removeWave = new QPushButton(tr("− Remove"));
	m_save = new QPushButton(tr("Save to the mod"));
	waveButtons->addWidget(addWave);
	waveButtons->addWidget(removeWave);
	waveButtons->addStretch(1);
	waveButtons->addWidget(m_save);
	form->addRow(tr("Name"), m_entryName);
	form->addRow(tr("Channel"), m_channel);
	form->addRow(tr("Volume"), m_entryVolume);
	form->addRow(tr("Pitch"), m_pitch);
	form->addRow(tr("Sound level"), m_soundlevel);
	form->addRow(tr("Files"), m_waves);
	form->addRow(waveButtons);
	m_entryBox = box;
	m_scapeEditor = new SoundscapeEditor;
	m_scapeEditor->setShownFile([this] { return m_file; });
	m_editors = new QStackedWidget;
	m_editors->addWidget(box);
	m_editors->addWidget(m_scapeEditor);
	split->addWidget(right);
	split->addWidget(m_editors);   // own column: the dock is low, the waveform needs the height
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 3);
	split->setStretchFactor(2, 2);

	m_filterTimer.setSingleShot(true);
	m_filterTimer.setInterval(180);   // thousands of entries: filter once typing pauses
	connect(&m_filterTimer, &QTimer::timeout, this, &SoundBrowser::applyFilter);
	connect(m_search, &QLineEdit::textChanged, &m_filterTimer, qOverload<>(&QTimer::start));
	connect(m_kind, &QComboBox::currentIndexChanged, this, &SoundBrowser::applyFilter);
	connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) { showItem(item); });
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { useItem(item); });
	connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
		// Files of a sound script entry are added when it is opened.
		if (item->data(0, KIND_ROLE).toInt() != ScriptSound || item->childCount() != 1 || !item->child(0)->text(0).isEmpty()) {
			return;
		}
		delete item->takeChild(0);
		const auto& entry = m_index.scripts[static_cast<std::size_t>(item->data(0, INDEX_ROLE).toInt())];
		for (const auto& wave : entry.waves) {
			auto* child = new QTreeWidgetItem(item, {QString::fromStdString(wave), tr("file")});
			child->setData(0, KIND_ROLE, int(FileSound));
			child->setData(0, NAME_ROLE, QString::fromStdString(wave));
		}
	});
	connect(m_play, &QPushButton::clicked, this, [this] {
		if (m_player->playing()) {
			m_player->stop();
		} else {
			playCurrent();
		}
	});
	auto* space = new QShortcut(QKeySequence(Qt::Key_Space), m_tree, nullptr, nullptr, Qt::WidgetShortcut);
	connect(space, &QShortcut::activated, m_play, &QPushButton::click);
	connect(m_loop, &QCheckBox::toggled, m_player, &SoundPlayer::setLoop);
	connect(m_playOnClick, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("sounds/playOnClick", on); });
	connect(m_volume, &QSlider::valueChanged, this, [this](int value) {
		m_player->setVolume(value / 100.f);
		QSettings().setValue("sounds/volume2", value);
	});
	m_player->setVolume(m_volume->value() / 100.f);
	connect(m_waveform, &SoundWaveform::seekRequested, this, [this](double seconds) {
		if (m_audio) m_player->play(m_audio, seconds);
	});
	connect(m_player, &SoundPlayer::stateChanged, this, [this](bool playing) { m_play->setText(playing ? tr("■ Stop") : tr("▶ Play")); });
	connect(m_player, &SoundPlayer::positionChanged, m_waveform, &SoundWaveform::setPlayhead);
	connect(m_player, &SoundPlayer::error, this, [this](const QString& text) { m_details->setText(tr("Cannot play: %1").arg(text)); });
	connect(addWave, &QPushButton::clicked, this, [this] {
		if (!m_file.isEmpty() && m_waves->findItems(m_file, Qt::MatchFixedString).isEmpty()) {
			m_waves->addItem(m_file);
		}
	});
	connect(removeWave, &QPushButton::clicked, this, [this] { delete m_waves->currentItem(); });
	connect(m_waves, &QListWidget::currentTextChanged, this, [this](const QString& file) {
		if (!file.isEmpty()) load(file, m_playOnClick->isChecked());
	});
	connect(m_save, &QPushButton::clicked, this, &SoundBrowser::saveEntry);
	connect(newScape, &QPushButton::clicked, this, &SoundBrowser::newSoundscape);
	connect(m_scapeEditor, &SoundscapeEditor::previewRequested, this, [this](const QString& file) { load(file, true); });
	connect(m_scapeEditor, &SoundscapeEditor::saveRequested, this, &SoundBrowser::saveSoundscape);
	connect(m_scapeEditor, &SoundscapeEditor::deleteRequested, this, &SoundBrowser::deleteSoundscape);

	connect(editor, &Editor::gameAboutToChange, this, [this] {
		++m_generation;
		m_player->stop();
		m_watcher.waitForFinished();
	});
	connect(editor, &Editor::gameChanged, this, &SoundBrowser::reload);
	connect(&m_watcher, &QFutureWatcherBase::finished, this, [this] {
		if (m_watcher.future().resultCount() == 0 || m_watcher.isCanceled()) {
			return;
		}
		m_index = m_watcher.result();
		fill();
		emit indexed();
	});
	reload();
}

SoundBrowser::~SoundBrowser() {
	++m_generation;
	m_watcher.waitForFinished();
}

void SoundBrowser::reload() {
	m_tree->clear();
	m_index = {};
	auto* game = m_editor->game();
	if (!game) {
		m_info->setText(tr("No game loaded"));
		return;
	}
	m_info->setText(tr("Reading sound scripts, sound files and soundscapes ..."));
	const GameFileSystem* fs = &game->files;
	const int generation = ++m_generation;
	m_watcher.setFuture(QtConcurrent::run([this, fs, generation] {
		SoundIndex index;
		if (generation != m_generation) return index;
		index.scripts = soundScripts(*fs);
		if (generation != m_generation) return index;
		index.soundscapes = soundscapes(*fs);
		for (const char* extension : {".wav", ".mp3", ".ogg"}) {
			if (generation != m_generation) return index;
			for (auto& file : fs->list("sound", extension)) {
				index.files.push_back(file.substr(6));   // without "sound/"
			}
		}
		std::sort(index.files.begin(), index.files.end());
		return index;
	}));
}

void SoundBrowser::fill() {
	m_tree->setUpdatesEnabled(false);
	m_tree->clear();
	auto* scripts = new QTreeWidgetItem(m_tree, {tr("Sound scripts (%1)").arg(m_index.scripts.size())});
	auto* files = new QTreeWidgetItem(m_tree, {tr("Sound files (%1)").arg(m_index.files.size())});
	auto* scapes = new QTreeWidgetItem(m_tree, {tr("Soundscapes (%1)").arg(m_index.soundscapes.size())});
	for (auto* top : {scripts, files, scapes}) {
		top->setFlags(top->flags() & ~Qt::ItemIsDragEnabled);
		QFont font = top->font(0);
		font.setBold(true);
		top->setFont(0, font);
	}
	// Sound scripts grouped by the part before the dot ("Weapon_Pistol"), files by folder,
	// soundscapes by file.
	QMap<QString, QTreeWidgetItem*> groups;
	for (std::size_t i = 0; i < m_index.scripts.size(); ++i) {
		const auto& entry = m_index.scripts[i];
		const QString name = QString::fromStdString(entry.name);
		const QString prefix = name.contains('.') ? name.section('.', 0, 0) : tr("(no group)");
		auto* item = new QTreeWidgetItem(groupItem(scripts, groups, prefix), {name, QString::fromStdString(entry.channel)});
		item->setData(0, KIND_ROLE, int(ScriptSound));
		item->setData(0, NAME_ROLE, name);
		item->setData(0, INDEX_ROLE, int(i));
		if (!entry.waves.empty()) {
			new QTreeWidgetItem(item);   // placeholder, filled when opened
		}
	}
	groups.clear();
	for (const auto& file : m_index.files) {
		const QString path = QString::fromStdString(file);
		const QString folder = path.contains('/') ? path.section('/', 0, -2) : tr("(sound folder)");
		auto* item = new QTreeWidgetItem(groupItem(files, groups, folder), {path.section('/', -1), path.section('.', -1).toUpper()});
		item->setData(0, KIND_ROLE, int(FileSound));
		item->setData(0, NAME_ROLE, path);
		item->setToolTip(0, path);
	}
	groups.clear();
	for (std::size_t i = 0; i < m_index.soundscapes.size(); ++i) {
		const auto& scape = m_index.soundscapes[i];
		const QString file = QString::fromStdString(scape.file).section('/', -1);
		auto* item = new QTreeWidgetItem(groupItem(scapes, groups, file),
		                                 {QString::fromStdString(scape.name), tr("%1 sounds").arg(scape.sounds.size())});
		item->setData(0, KIND_ROLE, int(SoundscapeEntry));
		item->setData(0, NAME_ROLE, QString::fromStdString(scape.name));
		item->setData(0, INDEX_ROLE, int(i));
	}
	m_tree->setUpdatesEnabled(true);
	QStringList scapeNames, fileNames;
	for (const auto& scape : m_index.soundscapes) scapeNames << QString::fromStdString(scape.name);
	for (const auto& file : m_index.files) fileNames << QString::fromStdString(file);
	m_scapeEditor->setSoundscapeNames(scapeNames);
	m_scapeEditor->setSoundFiles(fileNames);
	m_info->setText(tr("%1 sound script entries, %2 files, %3 soundscapes - drag into the map, double-click sets it on the selected entity")
	                    .arg(m_index.scripts.size())
	                    .arg(m_index.files.size())
	                    .arg(m_index.soundscapes.size()));
	applyFilter();
	if (!m_reselect.isEmpty()) {
		// After saving: the soundscape again, as it is now in the mod (the save message stays).
		const auto found = m_tree->findItems(m_reselect, Qt::MatchFixedString | Qt::MatchRecursive);
		m_reselect.clear();
		for (auto* item : found) {
			if (item->data(0, KIND_ROLE).toInt() == SoundscapeEntry) {
				const QString status = m_scapeEditor->status();
				m_tree->setCurrentItem(item);
				m_scapeEditor->setStatus(status);
				break;
			}
		}
	}
}

void SoundBrowser::applyFilter() {
	const QStringList words = m_search->text().toLower().split(' ', Qt::SkipEmptyParts);
	const int kind = m_kind->currentData().toInt();
	m_tree->setUpdatesEnabled(false);
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		auto* top = m_tree->topLevelItem(i);
		const int topKind = i + 1;   // same order as Kind
		const bool kindVisible = kind == 0 || kind == topKind;
		int topCount = 0;
		for (int g = 0; g < top->childCount(); ++g) {
			auto* group = top->child(g);
			int visible = 0;
			for (int c = 0; c < group->childCount(); ++c) {
				auto* item = group->child(c);
				const QString name = item->data(0, NAME_ROLE).toString().toLower();
				bool show = kindVisible;
				for (const auto& word : words) {
					show = show && name.contains(word);
				}
				item->setHidden(!show);
				visible += show ? 1 : 0;
			}
			group->setHidden(visible == 0);
			group->setExpanded(!words.isEmpty() && visible <= 50);
			topCount += visible;
		}
		top->setHidden(!kindVisible || topCount == 0);
		top->setExpanded(!words.isEmpty() || kind == topKind);
	}
	m_tree->setUpdatesEnabled(true);
}

void SoundBrowser::showItem(QTreeWidgetItem* item) {
	if (!item) {
		return;
	}
	const int kind = item->data(0, KIND_ROLE).toInt();
	const int index = item->data(0, INDEX_ROLE).toInt();
	const bool autoPlay = m_playOnClick->isChecked();
	if (kind == ScriptSound) {
		const auto& entry = m_index.scripts[static_cast<std::size_t>(index)];
		m_info->setText(tr("<b>%1</b> from %2 - double-click sets it on the selected ambient_generic")
		                    .arg(QString::fromStdString(entry.name), QString::fromStdString(entry.script)));
		editEntry(entry);
		m_editors->setCurrentWidget(m_entryBox);
		if (!entry.waves.empty()) {
			// Like the game: one of the files at random.
			static std::mt19937 random{std::random_device{}()};
			const auto pick = std::uniform_int_distribution<std::size_t>(0, entry.waves.size() - 1)(random);
			load(QString::fromStdString(entry.waves[pick]), autoPlay);
		}
	} else if (kind == SoundscapeEntry) {
		const auto& scape = m_index.soundscapes[static_cast<std::size_t>(index)];
		QStringList sounds, includes;
		for (const auto& s : scape.sounds) sounds << QString::fromStdString(s);
		for (const auto& s : scape.includes) includes << QString::fromStdString(s);
		m_info->setText(tr("<b>%1</b> (%2, dsp %3)<br>Plays: %4%5")
		                    .arg(QString::fromStdString(scape.name), QString::fromStdString(scape.file))
		                    .arg(scape.dsp)
		                    .arg(sounds.join(", "))
		                    .arg(includes.isEmpty() ? QString() : tr("<br>Includes: %1").arg(includes.join(", "))));
		editSoundscape(scape);
		for (const auto& sound : scape.sounds) {
			if (sound.find('.') != std::string::npos && sound.find('/') != std::string::npos) {
				load(QString::fromStdString(sound), autoPlay);
				break;
			}
		}
	} else if (kind == FileSound) {
		m_info->setText(tr("<b>sound/%1</b> - double-click sets it on a selected ambient_generic").arg(item->data(0, NAME_ROLE).toString()));
		load(item->data(0, NAME_ROLE).toString(), autoPlay);
	}
}

void SoundBrowser::load(const QString& file, bool play) {
	auto* game = m_editor->game();
	if (!game || file.isEmpty()) {
		return;
	}
	if (file == m_file && m_audio) {
		if (play) m_player->play(m_audio);
		return;
	}
	m_player->stop();
	m_file = file;
	m_title->setText(tr("<b>sound/%1</b>").arg(file.toHtmlEscaped()));
	m_details->setText(tr("Reading ..."));
	const int generation = ++m_loadGeneration;
	const GameFileSystem* fs = &game->files;
	auto* watcher = new QFutureWatcher<std::pair<std::shared_ptr<const DecodedAudio>, QString>>(this);
	connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, play] {
		watcher->deleteLater();
		if (generation != m_loadGeneration) {
			return;   // another sound was clicked meanwhile
		}
		const auto [audio, error] = watcher->result();
		m_audio = audio;
		m_waveform->setAudio(audio);
		if (!audio) {
			m_details->setText(tr("Cannot read: %1").arg(error));
			return;
		}
		QString text = tr("%1 · %2 Hz · %3 · %4 s").arg(QString::fromStdString(audio->format)).arg(audio->sampleRate)
		                   .arg(audio->channels == 1 ? tr("mono") : audio->channels == 2 ? tr("stereo") : tr("%1 channels").arg(audio->channels))
		                   .arg(audio->seconds(), 0, 'f', 2);
		text += audio->loopStart ? tr(" · loops (cue point) - good for ambient_generic") : tr(" · plays once");
		m_details->setText(text);
		emit audioLoaded();
		if (play) {
			m_player->play(audio);
		}
	});
	const std::string path = "sound/" + file.toStdString();
	watcher->setFuture(QtConcurrent::run([fs, path]() -> std::pair<std::shared_ptr<const DecodedAudio>, QString> {
		const auto data = fs->read(path);
		if (!data) {
			return {nullptr, QObject::tr("the file is missing")};
		}
		std::string error;
		auto audio = decodeAudio(*data, path, &error);
		if (!audio) {
			return {nullptr, QString::fromStdString(error)};
		}
		return {std::make_shared<const DecodedAudio>(std::move(*audio)), {}};
	}));
}

void SoundBrowser::playCurrent() {
	if (m_audio) {
		m_player->play(m_audio);
	} else if (auto* item = m_tree->currentItem()) {
		const bool before = m_playOnClick->isChecked();
		m_playOnClick->setChecked(true);
		showItem(item);
		m_playOnClick->setChecked(before);
	}
}

void SoundBrowser::editEntry(const SoundScriptEntry& entry) {
	m_entryName->setText(QString::fromStdString(entry.name));
	m_channel->setCurrentText(entry.channel.empty() ? QStringLiteral("CHAN_AUTO") : QString::fromStdString(entry.channel));
	m_entryVolume->setText(QString::fromStdString(entry.volume));
	m_pitch->setText(QString::fromStdString(entry.pitch));
	m_soundlevel->setCurrentText(entry.soundlevel.empty() ? QStringLiteral("SNDLVL_NORM") : QString::fromStdString(entry.soundlevel));
	m_waves->blockSignals(true);
	m_waves->clear();
	for (const auto& wave : entry.waves) {
		m_waves->addItem(QString::fromStdString(wave));
	}
	m_waves->blockSignals(false);
}

QString SoundBrowser::modScriptPath() const {
	const auto* entry = m_editor->gameEntry();
	if (!entry) {
		return {};
	}
	return QStringLiteral("scripts/game_sounds_%1.txt").arg(QString::fromStdString(toLower(entry->folder())));
}

void SoundBrowser::saveEntry() {
	auto* game = m_editor->game();
	const auto* gameEntry = m_editor->gameEntry();
	if (!game || !gameEntry) {
		return;
	}
	SoundScriptEntry entry;
	entry.name = m_entryName->text().trimmed().toStdString();
	if (entry.name.empty() || entry.name.find_first_of("\"{} \t") != std::string::npos) {
		m_details->setText(tr("Give the entry a name without spaces, e.g. MyMod.DoorSqueak"));
		return;
	}
	entry.channel = m_channel->currentText().trimmed().toStdString();
	entry.volume = m_entryVolume->text().trimmed().toStdString();
	entry.pitch = m_pitch->text().trimmed().toStdString();
	entry.soundlevel = m_soundlevel->currentText().trimmed().toStdString();
	for (int i = 0; i < m_waves->count(); ++i) {
		entry.waves.push_back(m_waves->item(i)->text().toStdString());
	}
	if (entry.waves.empty()) {
		m_details->setText(tr("Add at least one file (click a sound file, then \"+ Shown file\")"));
		return;
	}
	// The mod's own script (made if needed) and the manifest that loads it.
	const QString gameDir = fromPath(gameEntry->gameDir);
	const QString script = modScriptPath();
	const auto readMod = [this](const QString& relative) { return readModFile(relative); };
	const auto write = [this](const QString& relative, const std::string& text) { return writeModFile(relative, text); };
	try {
		const auto scriptText = upsertSoundScriptEntry(readMod(script), entry);
		// The manifest: the mod's own, else a copy of the base game's (it lists the game's scripts).
		std::string manifest = readMod(QStringLiteral("scripts/game_sounds_manifest.txt"));
		if (manifest.empty()) {
			manifest = game->files.readText("scripts/game_sounds_manifest.txt").value_or(std::string());
		}
		const auto manifestText = addToSoundManifest(manifest, script.toStdString());
		if (!write(script, scriptText) || !write(QStringLiteral("scripts/game_sounds_manifest.txt"), manifestText)) {
			m_details->setText(tr("Could not write into %1").arg(gameDir));
			return;
		}
	} catch (const std::exception& e) {
		m_details->setText(tr("%1 could not be read: %2").arg(script, QString::fromUtf8(e.what())));
		return;
	}
	m_details->setText(tr("Saved %1 into %2 (loaded by scripts/game_sounds_manifest.txt)").arg(QString::fromStdString(entry.name), script));
	emit m_editor->message(tr("Sound script entry %1 saved").arg(QString::fromStdString(entry.name)));
	reload();
}

std::string SoundBrowser::readModFile(const QString& relative) const {
	const auto* entry = m_editor->gameEntry();
	if (!entry) return {};
	QFile file(fromPath(entry->gameDir) + "/" + relative);
	return file.open(QIODevice::ReadOnly) ? file.readAll().toStdString() : std::string();
}

bool SoundBrowser::writeModFile(const QString& relative, const std::string& text) {
	auto* game = m_editor->game();
	const auto* entry = m_editor->gameEntry();
	if (!game || !entry) return false;
	const QString path = fromPath(entry->gameDir) + "/" + relative;
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text.data(), qint64(text.size())) != qint64(text.size())) {
		return false;
	}
	file.close();
	game->files.noteWrittenFile(toPath(path));   // the game's file list knows a new file at once
	return true;
}

QString SoundBrowser::modSoundscapePath() const {
	const auto* entry = m_editor->gameEntry();
	if (!entry) {
		return {};
	}
	return QStringLiteral("scripts/soundscapes_%1.txt").arg(QString::fromStdString(toLower(entry->folder())));
}

void SoundBrowser::newSoundscape() {
	const auto* entry = m_editor->gameEntry();
	const QString prefix = entry ? QString::fromStdString(toLower(entry->folder())) : QStringLiteral("mymod");
	m_scapeEditor->newSoundscape(prefix + ".new_soundscape");
	m_editors->setCurrentWidget(m_scapeEditor);
}

void SoundBrowser::editSoundscape(const Soundscape& scape) {
	auto* game = m_editor->game();
	if (!game) return;
	const QString file = QString::fromStdString(scape.file);
	SoundscapeScript found;
	found.name = scape.name;
	QString problem;
	try {
		for (auto& candidate : parseSoundscapes(game->files.readText(scape.file).value_or(std::string()))) {
			if (iequals(candidate.name, scape.name)) {
				found = std::move(candidate);
				break;
			}
		}
	} catch (const std::exception& e) {
		problem = tr("%1 could not be read: %2").arg(file, QString::fromUtf8(e.what()));
	}
	const auto* entry = m_editor->gameEntry();
	const bool inMod = entry && QString::compare(file, modSoundscapePath(), Qt::CaseInsensitive) == 0 &&
	                   QFileInfo::exists(fromPath(entry->gameDir) + "/" + file);
	m_scapeEditor->setSoundscape(found, file, inMod);
	m_scapeEditor->setStatus(problem);
	m_editors->setCurrentWidget(m_scapeEditor);
}

void SoundBrowser::saveSoundscape(const SoundscapeScript& scape) {
	auto* game = m_editor->game();
	if (!game || !m_editor->gameEntry()) {
		return;
	}
	const QString name = QString::fromStdString(scape.name);
	if (name.isEmpty() || name.contains(QRegularExpression(QStringLiteral("[\\s\"{}]")))) {
		m_scapeEditor->setStatus(tr("Give the soundscape a name without spaces, e.g. mymod.cave_drips"));
		return;
	}
	const QString file = modSoundscapePath();
	try {
		const auto text = upsertSoundscape(readModFile(file), scape);
		// The manifest: the mod's own, else a copy of the game's (it lists the game's soundscapes).
		std::string manifest = readModFile(QStringLiteral("scripts/soundscapes_manifest.txt"));
		if (manifest.empty()) {
			manifest = game->files.readText("scripts/soundscapes_manifest.txt").value_or(std::string());
		}
		const auto manifestText = addToSoundscapeManifest(manifest, file.toStdString());
		if (!writeModFile(file, text) || !writeModFile(QStringLiteral("scripts/soundscapes_manifest.txt"), manifestText)) {
			m_scapeEditor->setStatus(tr("Could not write into the mod's folder"));
			return;
		}
	} catch (const std::exception& e) {
		m_scapeEditor->setStatus(tr("%1 could not be read: %2").arg(file, QString::fromUtf8(e.what())));
		return;
	}
	m_scapeEditor->setStatus(tr("Saved %1 into %2").arg(name, file));
	emit m_editor->message(tr("Soundscape %1 saved into %2").arg(name, file));
	m_reselect = name;
	reload();
}

void SoundBrowser::deleteSoundscape(const QString& name) {
	const QString file = modSoundscapePath();
	try {
		const std::string text = readModFile(file);
		if (text.empty() || !writeModFile(file, removeSoundscape(text, name.toStdString()))) {
			m_scapeEditor->setStatus(tr("%1 is not in the mod's own file").arg(name));
			return;
		}
	} catch (const std::exception& e) {
		m_scapeEditor->setStatus(tr("%1 could not be read: %2").arg(file, QString::fromUtf8(e.what())));
		return;
	}
	emit m_editor->message(tr("Soundscape %1 removed from %2").arg(name, file));
	newSoundscape();
	reload();
}

void SoundBrowser::useItem(QTreeWidgetItem* item) {
	const int kind = item ? item->data(0, KIND_ROLE).toInt() : 0;
	if (kind == 0) {
		return;
	}
	const QString name = item->data(0, NAME_ROLE).toString();
	QApplication::clipboard()->setText(name);
	const auto& selection = m_editor->selection();
	std::vector<int> targets;
	std::string key;
	for (const int id : selection.entities) {
		const auto* entity = m_editor->doc().findEntity(id);
		if (!entity) continue;
		const auto cls = entity->classname();
		if (kind == SoundscapeEntry && cls.rfind("env_soundscape", 0) == 0) {
			targets.push_back(id);
			key = "soundscape";
		} else if (kind != SoundscapeEntry && cls == "ambient_generic") {
			targets.push_back(id);
			key = "message";
		}
	}
	if (targets.empty()) {
		emit m_editor->message(tr("%1 copied - select an ambient_generic or env_soundscape to set it, or drag it into the map").arg(name));
		return;
	}
	m_editor->commit(edit::setKeyValue(m_editor->doc(), targets, key, name.toStdString()));
}

} // namespace sme::app
