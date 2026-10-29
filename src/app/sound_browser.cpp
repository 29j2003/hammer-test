// sound_browser.cpp - Sounds page and dock (see sound_browser.h).

#include "sound_browser.h"

#include "copy_paste.h"
#include "editor.h"
#include "sound_player.h"
#include "soundscape_editor.h"
#include "ui_helpers.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <sme/editing.h>
#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/sound_categories.h>
#include <sme/text.h>

#include <random>

namespace sme::app {

namespace {

constexpr int KIND_ROLE = Qt::UserRole;
constexpr int NAME_ROLE = Qt::UserRole + 1;
constexpr int INDEX_ROLE = Qt::UserRole + 2;
constexpr int CATEGORY_ROLE = Qt::UserRole + 3;   ///< On the tree's top items and the category list

// Edit tools as small flat buttons under capital headers (local; the app style stays as it is).
const char* STYLE = R"(
QLabel[role="caps"] { color: #9399a3; font-size: 8pt; font-weight: 700; letter-spacing: 1px; }
QLabel[role="selection"] { color: #f5b541; }
QToolButton[role="soundTool"] { background: #2e2e2e; border: 1px solid #3a3a3a; border-radius: 4px; padding: 3px 8px; color: #dcdcdc; }
QToolButton[role="soundTool"]:hover { background: #3a3a3a; border-color: #4a4a4a; }
QToolButton[role="soundTool"]:disabled { color: #666666; background: #282828; }
QFrame#soundTools { background: #262626; border: 1px solid #333333; border-radius: 6px; }
QFrame#soundTools QLabel { background: transparent; }
QListWidget#soundCategories { background: #232323; border: none; }
QListWidget#soundCategories::item { padding: 5px 6px; border-radius: 4px; }
QListWidget#soundCategories::item:selected { background: #3a3a3a; color: #ffffff; }
)";

/// Color of a category's dot in the list.
QColor categoryColor(int category) {
	static const QColor COLORS[] = {{235, 110, 80},  {120, 200, 110}, {90, 170, 255}, {200, 160, 110}, {245, 181, 65}, {170, 140, 255},
	                                {150, 190, 200}, {255, 120, 200}, {140, 150, 165}, {255, 200, 120}, {120, 125, 135}};
	if (category == SoundBrowser::SoundscapesCategory) return {255, 140, 40};
	if (category < 0 || category >= int(std::size(COLORS))) return {200, 200, 200};
	return COLORS[category];
}

QIcon categoryIcon(int category) {
	QPixmap pixmap(14, 14);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(categoryColor(category));
	if (category == SoundBrowser::AllCategories) {
		for (int i = 0; i < 3; ++i) p.drawRoundedRect(QRectF(1 + i * 4.3, 3, 3, 8), 1, 1);
	} else {
		p.drawEllipse(QRectF(3, 3, 8, 8));
	}
	return QIcon(pixmap);
}

/// Display name of a category (translated).
QString categoryName(int category) {
	if (category == SoundBrowser::AllCategories) return QObject::tr("All sounds");
	if (category == SoundBrowser::SoundscapesCategory) return QObject::tr("Soundscapes");
	return QObject::tr(std::string(soundCategoryName(SOUND_CATEGORIES[std::size_t(category)])).c_str());
}

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
		item->setForeground(0, QColor(200, 204, 212));
		it = groups.insert(name, item);
	}
	return *it;
}

QString seconds(double s) {
	return QObject::tr("%1 s").arg(s, 0, 'f', 2);
}

} // namespace

// --- Waveform ------------------------------------------------------------------------

SoundWaveform::SoundWaveform(QWidget* parent) : QWidget(parent) {
	setMinimumHeight(90);
	setCursor(Qt::IBeamCursor);
	setFocusPolicy(Qt::ClickFocus);
	setToolTip(tr("Click: play from there · drag: select a part to edit or play (Shift extends it)"));
}

void SoundWaveform::setAudio(std::shared_ptr<const DecodedAudio> audio, bool keepSelection) {
	m_audio = std::move(audio);
	m_playhead = -1;
	m_peaks = m_audio ? waveformPeaks(*m_audio, std::max(1, width())) : decltype(m_peaks){};
	const FrameRange before = m_selection;
	if (keepSelection && m_audio) {
		m_selection = {std::min(before.begin, m_audio->frames()), std::min(before.end, m_audio->frames())};
		if (m_selection.empty()) m_selection = {};
	} else {
		m_selection = {};
	}
	update();
	if (before.begin != m_selection.begin || before.end != m_selection.end) emit selectionChanged();
}

void SoundWaveform::setPlayhead(double seconds) {
	m_playhead = seconds;
	update();
}

void SoundWaveform::setSelection(FrameRange range) {
	if (m_audio) {
		range = {std::min(range.begin, m_audio->frames()), std::min(range.end, m_audio->frames())};
	}
	if (range.empty()) range = {};
	if (range.begin == m_selection.begin && range.end == m_selection.end) return;
	m_selection = range;
	update();
	emit selectionChanged();
}

std::size_t SoundWaveform::frameAt(double x) const {
	if (!m_audio || width() <= 0) return 0;
	return static_cast<std::size_t>(std::llround(std::clamp(x / width(), 0.0, 1.0) * double(m_audio->frames())));
}

double SoundWaveform::xOf(std::size_t frame) const {
	if (!m_audio || m_audio->frames() == 0) return 0;
	return double(frame) / double(m_audio->frames()) * width();
}

void SoundWaveform::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
	if (m_audio) {
		m_peaks = waveformPeaks(*m_audio, std::max(1, width()));
	}
}

void SoundWaveform::mousePressEvent(QMouseEvent* event) {
	if (!m_audio || m_audio->frames() == 0 || event->button() != Qt::LeftButton) {
		return;
	}
	const std::size_t frame = frameAt(event->position().x());
	if ((event->modifiers() & Qt::ShiftModifier) && !m_selection.empty()) {
		// Extend the side that is nearer.
		const bool nearStart = frame < (m_selection.begin + m_selection.end) / 2;
		m_anchor = nearStart ? m_selection.end : m_selection.begin;
		setSelection({std::min(m_anchor, frame), std::max(m_anchor, frame)});
		m_dragging = true;
	} else {
		m_anchor = frame;
		m_dragging = false;
	}
	m_pressX = event->position().x();
}

void SoundWaveform::mouseMoveEvent(QMouseEvent* event) {
	if (m_pressX < 0 || !m_audio) return;
	if (!m_dragging && std::abs(event->position().x() - m_pressX) < 4) return;
	m_dragging = true;
	const std::size_t frame = frameAt(event->position().x());
	setSelection({std::min(m_anchor, frame), std::max(m_anchor, frame)});
}

void SoundWaveform::mouseReleaseEvent(QMouseEvent* event) {
	if (m_pressX < 0 || !m_audio) return;
	if (!m_dragging) {
		// A click: no selection any more, play from there.
		setSelection({});
		emit seekRequested(double(frameAt(event->position().x())) / std::max(1, m_audio->sampleRate));
	}
	m_pressX = -1;
	m_dragging = false;
}

void SoundWaveform::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.fillRect(rect(), QColor(24, 26, 31));
	const int mid = height() / 2;
	if (!m_audio) {
		p.setPen(QColor(60, 64, 74));
		p.drawLine(0, mid, width(), mid);
		p.setPen(QColor(130, 136, 150));
		p.drawText(rect(), Qt::AlignCenter, tr("Click a sound in the list to hear and edit it"));
		return;
	}
	// Seconds grid.
	const double length = std::max(1e-6, m_audio->seconds());
	const double step = length > 20 ? 5 : length > 6 ? 1 : length > 2 ? 0.5 : 0.1;
	p.setPen(QColor(38, 41, 48));
	for (double t = step; t < length; t += step) {
		const int x = int(t / length * width());
		p.drawLine(x, 0, x, height());
	}
	if (!m_selection.empty()) {
		p.fillRect(QRectF(QPointF(xOf(m_selection.begin), 0), QPointF(xOf(m_selection.end), height())), QColor(245, 165, 36, 45));
	}
	p.setPen(QColor(60, 64, 74));
	p.drawLine(0, mid, width(), mid);
	// Loop part in a different color: what repeats in the game.
	const int loopX = m_audio->loopStart ? int(xOf(*m_audio->loopStart)) : width();
	for (int x = 0; x < static_cast<int>(m_peaks.size()); ++x) {
		const auto [low, high] = m_peaks[static_cast<std::size_t>(x)];
		p.setPen(x >= loopX ? QColor(90, 170, 255) : QColor(255, 160, 40));
		p.drawLine(x, mid - int(high * (mid - 2)), x, mid - int(low * (mid - 2)));
	}
	if (!m_selection.empty()) {
		p.setPen(QPen(QColor(245, 181, 65), 1));
		p.drawLine(QPointF(xOf(m_selection.begin), 0), QPointF(xOf(m_selection.begin), height()));
		p.drawLine(QPointF(xOf(m_selection.end), 0), QPointF(xOf(m_selection.end), height()));
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
	setStyleSheet(STYLE);
	auto* outer = new QHBoxLayout(this);
	outer->setContentsMargins(4, 4, 4, 4);
	// The parts are built inside the splitters they end up in: moving a finished part there would
	// polish and lay out all of its widgets once more (start-up time).
	auto* split = new QSplitter(Qt::Horizontal);
	split->setChildrenCollapsible(false);
	outer->addWidget(split);

	// Categories.
	m_categories = new QListWidget;
	m_categories->setObjectName("soundCategories");
	m_categories->setIconSize(QSize(14, 14));
	m_categories->setToolTip(tr("Sounds sorted by what they are; searching looks through all of them"));
	split->addWidget(m_categories);

	// Search and the list.
	auto* left = new QWidget(split);
	auto* v = new QVBoxLayout(left);
	v->setContentsMargins(0, 0, 0, 0);
	auto* row = new QHBoxLayout;
	m_search = new QLineEdit;
	m_search->setObjectName("soundSearch");
	m_search->setPlaceholderText(tr("Search all sounds and soundscapes (words: door metal) ..."));
	m_search->setClearButtonEnabled(true);
	m_kind = new QComboBox;
	m_kind->addItem(tr("Scripts and files"), 0);
	m_kind->addItem(tr("Sound scripts"), int(ScriptSound));
	m_kind->addItem(tr("Sound files"), int(FileSound));
	m_kind->setToolTip(tr("Sound script entries (names the game and entities use) or the sound files themselves"));
	auto* newScape = new QPushButton(tr("+ Soundscape"));
	newScape->setObjectName("newSoundscape");
	newScape->setToolTip(tr("Make a new soundscape for the mod"));
	row->addWidget(m_search, 1);
	row->addWidget(m_kind);
	row->addWidget(newScape);
	auto* importSounds = new QPushButton(tr("Import ..."));
	importSounds->setObjectName("importSounds");
	importSounds->setToolTip(tr("Bring your own sound (WAV, MP3, OGG, FLAC) into the mod as a game-ready WAV"));
	row->addWidget(importSounds);
	connect(importSounds, &QPushButton::clicked, this, &SoundBrowser::importRequested);
	v->addLayout(row);
	m_tree = new SoundTree;
	m_tree->setColumnCount(2);
	m_tree->setHeaderLabels({tr("Name"), tr("Details")});
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_tree->header()->setStretchLastSection(false);
	m_tree->header()->resizeSection(1, 130);
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

	// Right: the player with the edit tools, and the sound script entry / soundscape editor.
	m_rightSplit = new QSplitter(Qt::Horizontal, split);
	m_rightSplit->setChildrenCollapsible(false);
	auto* player = new QWidget(m_rightSplit);
	auto* r = new QVBoxLayout(player);
	r->setContentsMargins(4, 0, 0, 0);
	r->setSpacing(6);
	m_title = new QLabel(tr("<b>No sound</b>"));
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	r->addWidget(m_title);
	m_details = new QLabel;
	m_details->setStyleSheet("color: #8a90a0;");
	m_details->setWordWrap(true);
	r->addWidget(m_details);
	m_waveform = new SoundWaveform;
	r->addWidget(m_waveform, 1);
	auto* controls = new QHBoxLayout;
	m_play = new QPushButton(tr("▶ Play"));
	m_play->setToolTip(SoundPlayer::available() ? tr("Play / stop (Space)") : tr("This build cannot play sounds"));
	m_play->setEnabled(SoundPlayer::available());
	m_playSelection = new QPushButton(tr("▶ Selection"));
	m_playSelection->setObjectName("soundPlaySelection");
	m_playSelection->setToolTip(tr("Play only the selected part"));
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
	controls->addWidget(m_playSelection);
	controls->addWidget(m_loop);
	controls->addWidget(m_playOnClick);
	controls->addStretch(1);
	controls->addWidget(new QLabel(tr("Volume")));
	controls->addWidget(m_volume);
	r->addLayout(controls);

	// Edit tools: work on the selection, or on the whole sound without one.
	auto* tools = new QFrame;
	tools->setObjectName("soundTools");
	auto* grid = new QGridLayout(tools);
	grid->setContentsMargins(8, 6, 8, 8);
	grid->setHorizontalSpacing(6);
	grid->setVerticalSpacing(5);
	const auto tool = [](const QString& text, const char* name, const QString& tip) {
		auto* button = new QToolButton;
		button->setText(text);
		button->setObjectName(name);
		button->setProperty("role", "soundTool");
		button->setToolTip(tip);
		return button;
	};
	auto* headRow = new QHBoxLayout;
	headRow->addWidget(capsLabel(tr("Edit")));
	m_selectionLabel = new QLabel;
	m_selectionLabel->setProperty("role", "selection");
	headRow->addWidget(m_selectionLabel, 1);
	m_undoButton = tool(tr("↶ Undo"), "soundUndo", tr("Undo the last edit of this sound"));
	m_redoButton = tool(tr("↷ Redo"), "soundRedo", tr("Redo the edit"));
	auto* revert = tool(tr("Revert"), "soundRevert", tr("Back to the sound as it was read"));
	headRow->addWidget(m_undoButton);
	headRow->addWidget(m_redoButton);
	headRow->addWidget(revert);
	grid->addLayout(headRow, 0, 0, 1, 2);

	auto* cutRow = new QHBoxLayout;
	auto* trim = tool(tr("Trim to selection"), "soundTrim", tr("Keep only the selected part"));
	auto* cutButton = tool(tr("Cut"), "soundCut", tr("Remove the selected part"));
	auto* silenceButton = tool(tr("Silence"), "soundSilence", tr("Make the selected part silent"));
	auto* fadeInButton = tool(tr("Fade in"), "soundFadeIn", tr("From silent to full volume over the selection (without one: the first 10 %)"));
	auto* fadeOutButton = tool(tr("Fade out"), "soundFadeOut", tr("From full volume to silent over the selection (without one: the last 10 %)"));
	auto* normalizeButton = tool(tr("Normalize"), "soundNormalize", tr("As loud as possible without clipping (peak at -1 dB)"));
	auto* reverseButton = tool(tr("Reverse"), "soundReverse", tr("Play the selection (or everything) backwards"));
	for (auto* b : {trim, cutButton, silenceButton, fadeInButton, fadeOutButton, normalizeButton, reverseButton}) cutRow->addWidget(b);
	cutRow->addStretch(1);
	grid->addLayout(cutRow, 1, 0, 1, 2);

	auto* levelRow = new QHBoxLayout;
	m_gain = new QDoubleSpinBox;
	m_gain->setObjectName("soundGainDb");
	m_gain->setRange(-30, 30);
	m_gain->setSingleStep(1);
	m_gain->setDecimals(1);
	m_gain->setValue(3);
	m_gain->setSuffix(tr(" dB"));
	auto* gainButton = tool(tr("Gain"), "soundGain", tr("Louder or quieter by these decibels (6 dB = twice as loud)"));
	m_speed = new QSpinBox;
	m_speed->setObjectName("soundSpeedPercent");
	m_speed->setRange(25, 400);
	m_speed->setValue(110);
	m_speed->setSuffix(tr(" %"));
	auto* speedButton = tool(tr("Speed"), "soundSpeed", tr("Faster and higher (over 100 %) or slower and lower, like a tape"));
	m_rate = new QComboBox;
	m_rate->setObjectName("soundRate");
	m_rate->addItem(tr("Sample rate ..."), 0);
	for (const int rate : {44100, 22050, 11025}) m_rate->addItem(tr("%1 Hz").arg(rate), rate);
	m_rate->setToolTip(tr("Source plays 44100, 22050 and 11025 Hz; other rates can sound wrong in the game"));
	auto* monoButton = tool(tr("Mono"), "soundMono", tr("Mix the channels into one (sounds placed in the world should be mono)"));
	auto* loopButton = tool(tr("Loop from here"), "soundLoopHere", tr("The game repeats the sound from the selection's start (or the playhead) to the end"));
	auto* noLoopButton = tool(tr("No loop"), "soundNoLoop", tr("Plays once"));
	levelRow->addWidget(m_gain);
	levelRow->addWidget(gainButton);
	levelRow->addSpacing(8);
	levelRow->addWidget(m_speed);
	levelRow->addWidget(speedButton);
	levelRow->addSpacing(8);
	levelRow->addWidget(m_rate);
	levelRow->addWidget(monoButton);
	levelRow->addSpacing(8);
	levelRow->addWidget(loopButton);
	levelRow->addWidget(noLoopButton);
	levelRow->addStretch(1);
	grid->addLayout(levelRow, 2, 0, 1, 2);

	auto* saveRow = new QHBoxLayout;
	saveRow->addWidget(new QLabel(tr("Save as  sound/")));
	m_saveName = new QLineEdit;
	m_saveName->setObjectName("soundSaveName");
	m_saveName->setPlaceholderText(tr("mymod/door_open.wav"));
	m_saveName->setToolTip(tr("Path below the mod's sound folder. The game's own path makes the mod play this one instead"));
	m_saveScript = new QCheckBox(tr("+ sound script entry"));
	m_saveScript->setObjectName("soundSaveScript");
	m_saveScript->setToolTip(tr("Also put the file into the sound script entry on the right and save that"));
	m_saveWav = new QPushButton(tr("Save WAV to the mod"));
	m_saveWav->setObjectName("soundSaveWav");
	m_saveWav->setProperty("accent", true);
	saveRow->addWidget(m_saveName, 1);
	saveRow->addWidget(m_saveScript);
	saveRow->addWidget(m_saveWav);
	grid->addLayout(saveRow, 3, 0, 1, 2);
	r->addWidget(tools);
	m_tools = tools;
	m_editHint = new QPushButton(tr("✎ Edit this sound ..."));
	m_editHint->setObjectName("soundEditOnPage");
	m_editHint->setToolTip(tr("The edit tools need more room: open the Sounds page"));
	controls->insertWidget(2, m_editHint);
	m_editHint->hide();   // until the browser is low (resizeEvent)

	m_editors = new QStackedWidget(m_rightSplit);
	auto* entryPage = new QWidget(m_editors);
	auto* box = new QGroupBox(tr("Sound script entry"), entryPage);
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
	addWave->setToolTip(tr("Add the sound file shown in the player"));
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
	m_waves->setMaximumHeight(170);
	// The form keeps its height; the rest of a tall column stays empty below it.
	auto* entryLayout = new QVBoxLayout(entryPage);
	entryLayout->setContentsMargins(0, 0, 0, 0);
	entryLayout->addWidget(box);
	entryLayout->addStretch(1);
	m_entryBox = entryPage;
	m_scapeEditor = new SoundscapeEditor(m_editors);
	m_scapeEditor->setShownFile([this] { return m_file; });
	m_editors->addWidget(entryPage);
	m_editors->addWidget(m_scapeEditor);
	// Player above the editor on a tall page, beside it in the low dock (see resizeEvent).
	// Explicit minimum widths: the parts may get narrow in the dock instead of making the whole window
	// wider than the screen (a layout's own minimum would add up all buttons of a row).
	m_categories->setMinimumWidth(130);
	left->setMinimumWidth(200);
	player->setMinimumWidth(220);
	m_editors->setMinimumWidth(200);
	m_rightSplit->addWidget(player);
	m_rightSplit->addWidget(m_editors);
	m_rightSplit->setStretchFactor(0, 3);
	m_rightSplit->setStretchFactor(1, 2);
	split->addWidget(m_rightSplit);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 3);
	split->setStretchFactor(2, 5);
	split->setSizes({170, 420, 900});

	m_filterTimer.setSingleShot(true);
	m_filterTimer.setInterval(180);   // thousands of entries: filter once typing pauses
	connect(&m_filterTimer, &QTimer::timeout, this, &SoundBrowser::applyFilter);
	connect(m_search, &QLineEdit::textChanged, &m_filterTimer, qOverload<>(&QTimer::start));
	connect(m_kind, &QComboBox::currentIndexChanged, this, &SoundBrowser::applyFilter);
	connect(m_categories, &QListWidget::currentRowChanged, this, &SoundBrowser::applyFilter);
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
	connect(m_playSelection, &QPushButton::clicked, this, &SoundBrowser::playSelection);
	for (QWidget* target : {static_cast<QWidget*>(m_tree), static_cast<QWidget*>(m_waveform)}) {
		auto* space = new QShortcut(QKeySequence(Qt::Key_Space), target, nullptr, nullptr, Qt::WidgetShortcut);
		connect(space, &QShortcut::activated, m_play, &QPushButton::click);
	}
	connect(m_loop, &QCheckBox::toggled, m_player, &SoundPlayer::setLoop);
	connect(m_playOnClick, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("sounds/playOnClick", on); });
	connect(m_volume, &QSlider::valueChanged, this, [this](int value) {
		m_player->setVolume(value / 100.f);
		QSettings().setValue("sounds/volume2", value);
	});
	m_player->setVolume(m_volume->value() / 100.f);
	connect(m_waveform, &SoundWaveform::seekRequested, this, [this](double seconds) {
		m_playOffset = 0;
		if (m_audio) m_player->play(m_audio, seconds);
	});
	connect(m_waveform, &SoundWaveform::selectionChanged, this, &SoundBrowser::updateEditState);
	connect(m_player, &SoundPlayer::stateChanged, this, [this](bool playing) { m_play->setText(playing ? tr("■ Stop") : tr("▶ Play")); });
	connect(m_player, &SoundPlayer::positionChanged, this, [this](double s) { m_waveform->setPlayhead(s + m_playOffset); });
	connect(m_player, &SoundPlayer::error, this, [this](const QString& text) { m_details->setText(tr("Cannot play: %1").arg(text)); });

	// Edits. Without a selection: fades take the first / last tenth, everything else the whole sound.
	const auto tenth = [](const DecodedAudio& a, FrameRange range, bool start) {
		if (!range.empty()) return range;
		const std::size_t n = std::max<std::size_t>(1, a.frames() / 10);
		return start ? FrameRange{0, n} : FrameRange{a.frames() - std::min(n, a.frames()), a.frames()};
	};
	connect(trim, &QToolButton::clicked, this, [this] {
		applyEdit(tr("Trimmed"), [](const DecodedAudio& a, FrameRange range) { return trimmed(a, range); });
	});
	connect(cutButton, &QToolButton::clicked, this, [this] {
		applyEdit(tr("Cut"), [](const DecodedAudio& a, FrameRange range) { return cut(a, range); });
	});
	connect(silenceButton, &QToolButton::clicked, this, [this] {
		applyEdit(tr("Silenced"), [](const DecodedAudio& a, FrameRange range) {
			auto out = a;
			silence(out, range);
			return out;
		});
	});
	connect(fadeInButton, &QToolButton::clicked, this, [this, tenth] {
		applyEdit(tr("Faded in"), [tenth](const DecodedAudio& a, FrameRange range) {
			auto out = a;
			fadeIn(out, tenth(a, range, true));
			return out;
		});
	});
	connect(fadeOutButton, &QToolButton::clicked, this, [this, tenth] {
		applyEdit(tr("Faded out"), [tenth](const DecodedAudio& a, FrameRange range) {
			auto out = a;
			fadeOut(out, tenth(a, range, false));
			return out;
		});
	});
	connect(normalizeButton, &QToolButton::clicked, this, [this] {
		applyEdit(tr("Normalized"), [](const DecodedAudio& a, FrameRange range) {
			auto out = a;
			normalize(out, -1.0, range);
			return out;
		});
	});
	connect(reverseButton, &QToolButton::clicked, this, [this] {
		applyEdit(tr("Reversed"), [](const DecodedAudio& a, FrameRange range) {
			auto out = a;
			reverse(out, range);
			return out;
		});
	});
	connect(gainButton, &QToolButton::clicked, this, [this] {
		const double db = m_gain->value();
		applyEdit(tr("Gain %1 dB").arg(db), [db](const DecodedAudio& a, FrameRange range) {
			auto out = a;
			applyGain(out, db, range);
			return out;
		});
	});
	connect(speedButton, &QToolButton::clicked, this, [this] {
		const double factor = m_speed->value() / 100.0;
		applyEdit(tr("Speed %1 %").arg(m_speed->value()), [factor](const DecodedAudio& a, FrameRange) { return speedChanged(a, factor); });
	});
	connect(m_rate, &QComboBox::activated, this, [this](int index) {
		const int rate = m_rate->itemData(index).toInt();
		m_rate->setCurrentIndex(0);
		if (rate > 0) {
			applyEdit(tr("%1 Hz").arg(rate), [rate](const DecodedAudio& a, FrameRange) { return resampled(a, rate); });
		}
	});
	connect(monoButton, &QToolButton::clicked, this, [this] {
		applyEdit(tr("Mono"), [](const DecodedAudio& a, FrameRange) { return toMono(a); });
	});
	connect(loopButton, &QToolButton::clicked, this, [this] {
		std::size_t at = m_waveform->selection().begin;
		if (m_waveform->selection().empty() && m_audio) {
			at = static_cast<std::size_t>(std::max(0.0, m_player->position()) * m_audio->sampleRate);
			if (!m_player->playing()) at = 0;
		}
		applyEdit(tr("Loop point"), [at](const DecodedAudio& a, FrameRange) {
			auto out = a;
			out.loopStart = std::min(at, a.frames() > 0 ? a.frames() - 1 : 0);
			return out;
		});
	});
	connect(noLoopButton, &QToolButton::clicked, this, [this] {
		applyEdit(tr("No loop"), [](const DecodedAudio& a, FrameRange) {
			auto out = a;
			out.loopStart.reset();
			return out;
		});
	});
	connect(m_undoButton, &QToolButton::clicked, this, &SoundBrowser::undoEdit);
	connect(m_redoButton, &QToolButton::clicked, this, &SoundBrowser::redoEdit);
	connect(revert, &QToolButton::clicked, this, [this] {
		if (m_undo.empty()) return;
		m_redo.clear();
		m_redo.push_back(m_audio);
		const auto original = m_undo.front();
		m_undo.clear();
		showEdited(original);
		m_details->setText(tr("Back to the sound as it was read"));
	});
	connect(m_saveWav, &QPushButton::clicked, this, [this] { saveEdited(m_saveName->text(), m_saveScript->isChecked()); });
	connect(m_editHint, &QPushButton::clicked, this, &SoundBrowser::pageRequested);

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
	connect(editor, &Editor::gameChanged, this, [this] {
		m_firstFill = true;
		reload();
	});
	connect(&m_watcher, &QFutureWatcherBase::finished, this, [this] {
		if (m_watcher.future().resultCount() == 0 || m_watcher.isCanceled()) {
			return;
		}
		m_index = m_watcher.result();
		fill();
		emit indexed();
	});
	updateEditState();
	reload();
}

SoundBrowser::~SoundBrowser() {
	++m_generation;
	m_watcher.waitForFinished();
}

void SoundBrowser::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
	// In the low dock the edit tools would push the map views together: there a button leads to the page.
	const bool roomy = height() >= 420;
	if (m_tools->isVisibleTo(this) != roomy) {
		m_tools->setVisible(roomy);
		m_editHint->setVisible(!roomy);
		// The low dock under the map leaves out the category list (search and the tree find
		// everything); the page has it. Keeps the dock narrow enough for small screens.
		m_categories->setVisible(roomy);
	}
	// The page is tall: the editor goes below the player. The dock is low: beside it.
	const auto orientation = height() >= 560 ? Qt::Vertical : Qt::Horizontal;
	if (m_rightSplit->orientation() != orientation) {
		m_rightSplit->setOrientation(orientation);
		if (orientation == Qt::Vertical) {
			m_rightSplit->setSizes({height() * 11 / 20, height() * 9 / 20});
		} else {
			m_rightSplit->setSizes({m_rightSplit->width() * 3 / 5, m_rightSplit->width() * 2 / 5});
		}
	}
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
	const int previousCategory = m_categories->currentItem() ? selectedCategory() : AllCategories - 1;
	m_tree->setUpdatesEnabled(false);
	m_tree->blockSignals(true);
	m_tree->clear();
	// One branch per category; inside, folders hold the script sounds (by the folder of their first
	// file, so "Weapon_Pistol.Single" sits with weapons/pistol/*.wav) and then the files.
	std::vector<QTreeWidgetItem*> tops;
	std::vector<int> counts(SOUND_CATEGORIES.size() + 1, 0);
	const auto top = [&](int category) {
		auto* item = new QTreeWidgetItem(m_tree, {categoryName(category)});
		item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
		item->setData(0, CATEGORY_ROLE, category);
		item->setIcon(0, categoryIcon(category));
		QFont font = item->font(0);
		font.setBold(true);
		item->setFont(0, font);
		return item;
	};
	for (std::size_t c = 0; c < SOUND_CATEGORIES.size(); ++c) tops.push_back(top(int(c)));
	auto* scapes = top(SoundscapesCategory);
	std::vector<QMap<QString, QTreeWidgetItem*>> groups(SOUND_CATEGORIES.size());
	const auto folderName = [this](const std::string& folder) { return folder.empty() ? tr("(sound folder)") : QString::fromStdString(folder); };
	for (std::size_t i = 0; i < m_index.scripts.size(); ++i) {
		const auto& entry = m_index.scripts[i];
		const std::string firstWave = entry.waves.empty() ? std::string() : entry.waves.front();
		const auto category = std::size_t(categorizeSoundScript(entry.name, firstWave));
		const QString name = QString::fromStdString(entry.name);
		const QString group = entry.waves.empty() ? tr("%1 (script only)").arg(name.contains('.') ? name.section('.', 0, 0) : name)
		                                          : folderName(soundFolder(firstWave));
		auto* item = new QTreeWidgetItem(groupItem(tops[category], groups[category], group),
		                                 {name, entry.channel.empty() ? tr("script") : tr("script · %1").arg(QString::fromStdString(entry.channel))});
		item->setData(0, KIND_ROLE, int(ScriptSound));
		item->setData(0, NAME_ROLE, name);
		item->setData(0, INDEX_ROLE, int(i));
		item->setForeground(0, QColor(245, 190, 110));
		item->setToolTip(0, tr("Sound script entry from %1").arg(QString::fromStdString(entry.script)));
		if (!entry.waves.empty()) {
			new QTreeWidgetItem(item);   // placeholder, filled when opened
		}
		++counts[category];
	}
	for (const auto& file : m_index.files) {
		const auto category = std::size_t(categorizeSoundFile(file));
		const QString path = QString::fromStdString(file);
		auto* item = new QTreeWidgetItem(groupItem(tops[category], groups[category], folderName(soundFolder(file))),
		                                 {path.section('/', -1), path.section('.', -1).toUpper()});
		item->setData(0, KIND_ROLE, int(FileSound));
		item->setData(0, NAME_ROLE, path);
		item->setToolTip(0, path);
		++counts[category];
	}
	QMap<QString, QTreeWidgetItem*> scapeGroups;
	for (std::size_t i = 0; i < m_index.soundscapes.size(); ++i) {
		const auto& scape = m_index.soundscapes[i];
		const QString file = QString::fromStdString(scape.file).section('/', -1);
		auto* item = new QTreeWidgetItem(groupItem(scapes, scapeGroups, file), {QString::fromStdString(scape.name), tr("%1 sounds").arg(scape.sounds.size())});
		item->setData(0, KIND_ROLE, int(SoundscapeEntry));
		item->setData(0, NAME_ROLE, QString::fromStdString(scape.name));
		item->setData(0, INDEX_ROLE, int(i));
	}
	counts.back() = int(m_index.soundscapes.size());
	for (std::size_t c = 0; c < tops.size(); ++c) {
		tops[c]->setText(1, tr("%1").arg(counts[c]));
		tops[c]->sortChildren(0, Qt::AscendingOrder);
	}
	scapes->setText(1, tr("%1").arg(counts.back()));

	// The category list with the counts.
	m_categories->blockSignals(true);
	m_categories->clear();
	const auto addCategory = [this](int category, int count) {
		auto* item = new QListWidgetItem(categoryIcon(category), tr("%1  %2").arg(categoryName(category)).arg(count), m_categories);
		item->setData(CATEGORY_ROLE, category);
		if (count == 0 && category != AllCategories) item->setForeground(QColor(110, 110, 110));
	};
	addCategory(AllCategories, int(m_index.scripts.size() + m_index.files.size()));
	for (std::size_t c = 0; c < SOUND_CATEGORIES.size(); ++c) addCategory(int(c), counts[c]);
	addCategory(SoundscapesCategory, counts.back());
	// First time: the first category with sounds (the page shows something at once); later the same as before.
	int pick = 0;
	for (int row = 0; row < m_categories->count(); ++row) {
		const int category = m_categories->item(row)->data(CATEGORY_ROLE).toInt();
		const bool wanted = m_firstFill || previousCategory < AllCategories ? category >= 0 && category != SoundscapesCategory &&
		                                                                       counts[std::size_t(category)] > 0
		                                                                 : category == previousCategory;
		if (wanted) {
			pick = row;
			break;
		}
	}
	m_categories->setCurrentRow(pick);
	m_categories->blockSignals(false);
	m_tree->blockSignals(false);
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
		const QString status = m_scapeEditor->status();
		const QString name = m_reselect;
		m_reselect.clear();
		if (reveal(name, SoundscapeEntry)) m_scapeEditor->setStatus(status);
	} else if (m_firstFill && m_file.isEmpty()) {
		// Something to look at right away: the first sound file of the category, shown without playing
		// (a file, not a script entry: the entry form stays free for a new entry).
		auto* category = m_tree->topLevelItem(std::max(0, selectedCategory()));
		QTreeWidgetItem* firstFile = nullptr;
		for (int g = 0; category && g < category->childCount() && !firstFile; ++g) {
			for (int c = 0; c < category->child(g)->childCount() && !firstFile; ++c) {
				if (category->child(g)->child(c)->data(0, KIND_ROLE).toInt() == FileSound) firstFile = category->child(g)->child(c);
			}
		}
		if (firstFile) {
			firstFile->parent()->setExpanded(true);
			const bool before = m_playOnClick->isChecked();
			m_playOnClick->blockSignals(true);
			m_playOnClick->setChecked(false);
			m_tree->setCurrentItem(firstFile);
			m_playOnClick->setChecked(before);
			m_playOnClick->blockSignals(false);
		}
	}
	m_firstFill = false;
}

int SoundBrowser::selectedCategory() const {
	const auto* item = m_categories->currentItem();
	return item ? item->data(CATEGORY_ROLE).toInt() : AllCategories;
}

void SoundBrowser::applyFilter() {
	const QStringList words = m_search->text().toLower().split(' ', Qt::SkipEmptyParts);
	const int kind = m_kind->currentData().toInt();
	const int category = selectedCategory();
	const bool searching = !words.isEmpty();
	m_tree->setUpdatesEnabled(false);
	int total = 0;
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		auto* top = m_tree->topLevelItem(i);
		const int topCategory = top->data(0, CATEGORY_ROLE).toInt();
		// Searching looks through everything; else only the chosen category.
		const bool categoryShown = searching || category == AllCategories || category == topCategory;
		const bool isScapes = topCategory == SoundscapesCategory;
		int topCount = 0;
		for (int g = 0; g < top->childCount(); ++g) {
			auto* group = top->child(g);
			const QString groupName = group->text(0).toLower();
			int visible = 0;
			for (int c = 0; c < group->childCount(); ++c) {
				auto* item = group->child(c);
				const int itemKind = item->data(0, KIND_ROLE).toInt();
				const QString name = item->data(0, NAME_ROLE).toString().toLower();
				bool show = categoryShown && (isScapes || kind == 0 || kind == itemKind);
				for (const auto& word : words) {
					show = show && (name.contains(word) || groupName.contains(word));
				}
				item->setHidden(!show);
				visible += show ? 1 : 0;
			}
			group->setHidden(visible == 0);
			group->setText(1, visible > 0 ? QString::number(visible) : QString());
			topCount += visible;
		}
		top->setHidden(topCount == 0);
		total += topCount;
	}
	// Open what is short enough to read: the chosen category, matches of a search.
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		auto* top = m_tree->topLevelItem(i);
		if (top->isHidden()) continue;
		const bool single = !searching && category != AllCategories;
		top->setExpanded(single || (searching && total <= 400));
		int groupsShown = 0;
		for (int g = 0; g < top->childCount(); ++g) {
			if (!top->child(g)->isHidden()) ++groupsShown;
		}
		for (int g = 0; g < top->childCount(); ++g) {
			auto* group = top->child(g);
			if (group->isHidden()) continue;
			group->setExpanded((searching && total <= 150) || (single && groupsShown <= 2));
		}
	}
	m_tree->setUpdatesEnabled(true);
	if (searching) {
		m_info->setText(total == 0 ? tr("Nothing found for \"%1\"").arg(m_search->text()) : tr("%1 found in all categories").arg(total));
	}
}

QTreeWidgetItem* SoundBrowser::reveal(const QString& name, int kind) {
	QTreeWidgetItem* found = nullptr;
	for (auto* item : m_tree->findItems(name, Qt::MatchFixedString | Qt::MatchRecursive)) {
		const int itemKind = item->data(0, KIND_ROLE).toInt();
		if (itemKind != 0 && (kind == 0 || itemKind == kind) && item->parent() && item->parent()->parent()) {
			found = item;
			break;
		}
	}
	if (!found) return nullptr;
	// Its category in the list (the search would hide it otherwise).
	auto* top = found;
	while (top->parent()) top = top->parent();
	if (!m_search->text().isEmpty()) {
		m_search->blockSignals(true);
		m_search->clear();
		m_search->blockSignals(false);
	}
	m_kind->setCurrentIndex(0);
	const int category = top->data(0, CATEGORY_ROLE).toInt();
	for (int row = 0; row < m_categories->count(); ++row) {
		if (m_categories->item(row)->data(CATEGORY_ROLE).toInt() == category) m_categories->setCurrentRow(row);
	}
	applyFilter();
	for (auto* parent = found->parent(); parent; parent = parent->parent()) parent->setExpanded(true);
	m_tree->setCurrentItem(found);
	m_tree->scrollToItem(found);
	return found;
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
		m_editors->setCurrentWidget(m_entryBox);
		load(item->data(0, NAME_ROLE).toString(), autoPlay);
	}
}

void SoundBrowser::load(const QString& file, bool play) {
	auto* game = m_editor->game();
	if (!game || file.isEmpty()) {
		return;
	}
	if (file == m_file && m_audio) {
		// Already read (e.g. by the soundscape shown before): ready at once, with its edits.
		m_playOffset = 0;
		if (play) m_player->play(m_audio);
		QTimer::singleShot(0, this, [this] { emit audioLoaded(); });   // like a fresh load: after this call
		return;
	}
	m_player->stop();
	m_file = file;
	m_undo.clear();
	m_redo.clear();
	m_savedAfterEdit = false;
	m_title->setText(tr("<b>sound/%1</b>").arg(file.toHtmlEscaped()));
	m_details->setText(tr("Reading ..."));
	// Saving goes to the same path in the mod (it then plays instead of the game's); always as WAV.
	m_saveName->setText(QFileInfo(file).path() == "." ? QFileInfo(file).completeBaseName() + ".wav"
	                                                   : QFileInfo(file).path() + "/" + QFileInfo(file).completeBaseName() + ".wav");
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
		updateEditState();
		if (!audio) {
			m_details->setText(tr("Cannot read: %1").arg(error));
			return;
		}
		m_details->setText(describe(*audio));
		emit audioLoaded();
		if (play) {
			m_playOffset = 0;
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

QString SoundBrowser::describe(const DecodedAudio& audio) const {
	QString text = tr("%1 · %2 Hz · %3 · %4 s").arg(QString::fromStdString(audio.format)).arg(audio.sampleRate)
	                   .arg(audio.channels == 1 ? tr("mono") : audio.channels == 2 ? tr("stereo") : tr("%1 channels").arg(audio.channels))
	                   .arg(audio.seconds(), 0, 'f', 2);
	text += audio.loopStart ? tr(" · loops (cue point) - good for ambient_generic") : tr(" · plays once");
	if (!isSourceSampleRate(audio.sampleRate)) text += tr(" · %1 Hz may sound wrong in the game: use 44100 or 22050").arg(audio.sampleRate);
	return text;
}

void SoundBrowser::playCurrent() {
	m_playOffset = 0;
	if (m_audio) {
		m_player->play(m_audio);
	} else if (auto* item = m_tree->currentItem()) {
		const bool before = m_playOnClick->isChecked();
		m_playOnClick->setChecked(true);
		showItem(item);
		m_playOnClick->setChecked(before);
	}
}

void SoundBrowser::playSelection() {
	if (!m_audio) return;
	const FrameRange range = m_waveform->selection();
	if (range.empty()) {
		playCurrent();
		return;
	}
	// A copy of just that part; the playhead counts from where it starts.
	auto part = std::make_shared<DecodedAudio>(trimmed(*m_audio, range));
	part->loopStart.reset();
	m_playOffset = double(range.begin) / std::max(1, m_audio->sampleRate);
	m_player->play(part);
}

void SoundBrowser::applyEdit(const QString& what, const std::function<DecodedAudio(const DecodedAudio&, FrameRange)>& edit) {
	if (!m_audio) {
		m_details->setText(tr("Click a sound first, then edit it here"));
		return;
	}
	m_player->stop();
	auto result = std::make_shared<const DecodedAudio>(edit(*m_audio, m_waveform->selection()));
	if (result->frames() == 0) {
		m_details->setText(tr("%1 would leave nothing of the sound").arg(what));
		return;
	}
	m_undo.push_back(m_audio);
	m_redo.clear();
	m_savedAfterEdit = false;
	showEdited(result);
	m_details->setText(tr("%1 - %2").arg(what, describe(*result)));
}

void SoundBrowser::undoEdit() {
	if (m_undo.empty()) return;
	m_player->stop();
	m_redo.push_back(m_audio);
	auto previous = m_undo.back();
	m_undo.pop_back();
	showEdited(previous);
	m_details->setText(describe(*previous));
}

void SoundBrowser::redoEdit() {
	if (m_redo.empty()) return;
	m_player->stop();
	m_undo.push_back(m_audio);
	auto next = m_redo.back();
	m_redo.pop_back();
	showEdited(next);
	m_details->setText(describe(*next));
}

void SoundBrowser::showEdited(std::shared_ptr<const DecodedAudio> audio) {
	const bool sameLength = m_audio && audio && m_audio->frames() == audio->frames();
	m_audio = std::move(audio);
	m_waveform->setAudio(m_audio, sameLength);   // the selection stays where the length did not change
	updateEditState();
}

void SoundBrowser::updateEditState() {
	const bool have = m_audio != nullptr;
	for (auto* button : findChildren<QToolButton*>()) {
		if (button->property("role").toString() == "soundTool") button->setEnabled(have);
	}
	m_undoButton->setEnabled(!m_undo.empty());
	m_redoButton->setEnabled(!m_redo.empty());
	if (auto* revert = findChild<QToolButton*>("soundRevert")) revert->setEnabled(!m_undo.empty());
	const FrameRange range = m_waveform->selection();
	m_playSelection->setEnabled(have && !range.empty() && SoundPlayer::available());
	for (const char* name : {"soundTrim", "soundCut", "soundSilence"}) {
		if (auto* b = findChild<QToolButton*>(name)) b->setEnabled(have && !range.empty());
	}
	m_gain->setEnabled(have);
	m_speed->setEnabled(have);
	m_rate->setEnabled(have);
	m_saveWav->setEnabled(have);
	m_saveName->setEnabled(have);
	if (!have) {
		m_selectionLabel->setText(QString());
	} else if (range.empty()) {
		m_selectionLabel->setText(tr("whole sound - drag on the waveform to select a part"));
	} else {
		const double rate = std::max(1, m_audio->sampleRate);
		m_selectionLabel->setText(tr("selection %1 – %2 (%3)").arg(seconds(range.begin / rate), seconds(range.end / rate), seconds(range.size() / rate)));
	}
	m_title->setText(m_file.isEmpty() ? tr("<b>No sound</b>")
	                                  : tr("<b>sound/%1</b>%2").arg(m_file.toHtmlEscaped(), edited() ? tr("  <span style='color:#f5b541'>● edited</span>") : QString()));
}

bool SoundBrowser::saveEdited(const QString& relativeIn, bool scriptEntry, bool confirmed) {
	const auto* gameEntry = m_editor->gameEntry();
	if (!m_audio || !gameEntry || !m_editor->game()) {
		return false;
	}
	// A clean path below sound/, always .wav.
	QString relative = QDir::cleanPath(relativeIn.trimmed().replace('\\', '/'));
	if (relative.startsWith("sound/", Qt::CaseInsensitive)) relative = relative.mid(6);
	if (relative.isEmpty() || relative == "." || relative.startsWith('/') || relative.startsWith("..") || relative.contains(':') ||
	    relative.contains(QRegularExpression(QStringLiteral("[\"{}]")))) {
		m_details->setText(tr("Give a path below sound/, e.g. mymod/door_open.wav"));
		return false;
	}
	if (!relative.endsWith(".wav", Qt::CaseInsensitive)) relative = QFileInfo(relative).path() == "." ? QFileInfo(relative).completeBaseName() + ".wav"
	                                                                                                : QFileInfo(relative).path() + "/" + QFileInfo(relative).completeBaseName() + ".wav";
	const QString target = fromPath(gameEntry->gameDir) + "/sound/" + relative;
	if (QFileInfo::exists(target) && !confirmed) {
		const auto answer = QMessageBox::question(this, tr("Replace the mod's sound?"),
		                                          tr("sound/%1 already is a file of the mod. Replace it with the edited sound?").arg(relative));
		if (answer != QMessageBox::Yes) return false;
	}
	const auto bytes = encodeWav(*m_audio);
	if (!writeModFile("sound/" + relative, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()))) {
		m_details->setText(tr("Could not write %1").arg(target));
		return false;
	}
	m_savedAfterEdit = true;
	m_saveName->setText(relative);
	const auto saved = m_audio;
	const QString before = m_file;
	m_file = relative;   // the player shows the saved file from now on (with the undo steps kept)
	if (before.compare(relative, Qt::CaseInsensitive) != 0) {
		m_undo.clear();
		m_redo.clear();
	}
	updateEditState();
	m_details->setText(tr("Saved sound/%1 into the mod (%2)").arg(relative, describe(*saved)));
	emit m_editor->message(tr("Sound saved: sound/%1").arg(relative));
	if (scriptEntry) {
		if (m_entryName->text().trimmed().isEmpty()) {
			const auto* entry = m_editor->gameEntry();
			const QString prefix = entry ? QString::fromStdString(entry->folder()) : QStringLiteral("MyMod");
			QString base = QFileInfo(relative).completeBaseName();
			base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
			m_entryName->setText(prefix + "." + base);
		}
		if (m_waves->findItems(relative, Qt::MatchFixedString).isEmpty()) {
			m_waves->blockSignals(true);
			m_waves->addItem(relative);
			m_waves->blockSignals(false);
		}
		m_editors->setCurrentWidget(m_entryBox);
		saveEntry();   // reads everything again
	} else {
		reload();      // the new file shows up in the list
	}
	return true;
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
