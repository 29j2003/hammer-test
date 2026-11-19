// npcs_page.cpp - The NPCs page (see npcs_page.h).

#include "npcs_page.h"

#include "editor.h"
#include "glow_dialog.h"
#include "import_dialog.h"
#include "material_cache.h"
#include "model_cache.h"
#include "model_viewer.h"
#include "process_guard.h"
#include "sound_player.h"
#include "ui_helpers.h"

#include <sme/audio.h>
#include <sme/entity_catalog.h>
#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/model_export.h>
#include <sme/text.h>
#include <sme/weapons.h>

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <cmath>
#include <cstring>
#include <set>

namespace sme::app {

namespace {

constexpr int ROLE_KIND = Qt::UserRole + 1;
constexpr int ROLE_VALUE = Qt::UserRole + 2;
enum SoundRow { EntryRow = 1, WaveRow };

std::optional<std::vector<std::byte>> readFileBytes(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
	const QByteArray data = file.readAll();
	std::vector<std::byte> out(std::size_t(data.size()));
	std::memcpy(out.data(), data.constData(), out.size());
	return out;
}

QPushButton* button(const QString& text, const char* name, const QString& tip) {
	auto* b = new QPushButton(text);
	b->setObjectName(name);
	b->setToolTip(tip);
	return b;
}

} // namespace

NpcsPage::NpcsPage(Editor* editor, MaterialCache* materials, QWidget* parent)
	: QWidget(parent), m_editor(editor), m_materials(materials), m_player(new SoundPlayer(this)) {
	setObjectName("npcsPage");
	m_loader.setParent(this);
	m_scriptLoader.setParent(this);
	buildUi();
	m_timer = new QTimer(this);
	m_timer->setInterval(16);
	connect(m_timer, &QTimer::timeout, this, &NpcsPage::tick);

	connect(m_editor, &Editor::gameAboutToChange, this, [this] {
		waitForOwnTasks(this);
		m_loader.waitForFinished();
		m_scriptLoader.waitForFinished();
	});
	connect(m_editor, &Editor::gameChanged, this, [this] {
		m_dirty = true;
		m_scriptsLoaded = false;
		m_scripts.clear();
		if (isVisible()) refresh();
	});
	if (m_editor->models()) {
		connect(m_editor->models(), &ModelCache::modelReady, this, [this](const QString& path) {
			if (m_model.isEmpty()) return;
			const QString wanted = m_skin->currentIndex() > 0 ? qs(modelVariantKey({m_model.toStdString(), m_skin->currentIndex(), 0, {}})) : m_model;
			if (path.compare(wanted, Qt::CaseInsensitive) != 0) return;
			m_geometry = m_editor->models()->get(wanted);
			m_viewer->setModel(m_geometry, true);
			if (!m_geometry) m_viewer->setMessage(tr("Cannot load: %1").arg(m_editor->models()->error(wanted)));
			applyPose();
			showLook();
		});
	}
	if (m_materials) {
		connect(m_materials, &MaterialCache::materialReady, this, [this](const QString& name) {
			for (int i = 0; i < m_materialList->count(); ++i) {
				auto* item = m_materialList->item(i);
				if (item->data(Qt::UserRole).toString().compare(name, Qt::CaseInsensitive) != 0) continue;
				if (const auto info = m_materials->get(name); info && !info->image.isNull()) {
					item->setIcon(QIcon(QPixmap::fromImage(info->image.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
				}
			}
		});
	}
	connect(&m_loader, &QFutureWatcherBase::finished, this, [this] {
		if (m_loader.property("generation").toInt() != m_generation) return;
		m_animated = m_loader.result();
		showSequences();
		applyPose();
		updateTransport();
	});
	connect(&m_scriptLoader, &QFutureWatcherBase::finished, this, [this] {
		m_scripts = m_scriptLoader.result();
		m_scriptsLoaded = true;
		showSounds();
	});
}

NpcsPage::~NpcsPage() {
	stopOwnProcesses(this);
	waitForOwnTasks(this);
}

void NpcsPage::buildUi() {
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	auto* splitter = new QSplitter(Qt::Horizontal);
	layout->addWidget(splitter);

	// Left: the NPCs, grouped.
	auto* left = new QWidget;
	auto* leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->addWidget(capsLabel(tr("NPCs")));
	m_list = new QListWidget;
	m_list->setObjectName("npcList");
	leftLayout->addWidget(m_list, 1);
	leftLayout->addWidget(noteLabel(tr("Only NPCs the game has a model for. Changes go into your mod; the game's files stay as they are.")));
	left->setMinimumWidth(150);
	left->setMaximumWidth(240);
	splitter->addWidget(left);

	// Middle: the model, free around it, and playback.
	auto* middle = new QWidget;
	auto* middleLayout = new QVBoxLayout(middle);
	middleLayout->setContentsMargins(0, 0, 0, 0);
	m_title = new QLabel;
	m_title->setObjectName("npcTitle");
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	middleLayout->addWidget(m_title);
	m_viewer = new ModelViewer(m_materials);
	m_viewer->setObjectName("npcViewer");
	m_viewer->setMinimumSize(240, 200);
	m_viewer->setToolTip(tr("Drag to turn around the model, right-drag to move, wheel to zoom"));
	middleLayout->addWidget(m_viewer, 1);
	auto* transport = new QHBoxLayout;
	m_play = new QToolButton;
	m_play->setObjectName("npcPlay");
	m_play->setText(QStringLiteral("▶"));
	m_play->setToolTip(tr("Play / pause the animation"));
	transport->addWidget(m_play);
	m_scrub = new QSlider(Qt::Horizontal);
	m_scrub->setObjectName("npcScrub");
	m_scrub->setRange(0, 1000);
	transport->addWidget(m_scrub, 1);
	m_time = new QLabel;
	m_time->setMinimumWidth(110);
	transport->addWidget(m_time);
	middleLayout->addLayout(transport);
	splitter->addWidget(middle);

	m_tabs = new QTabWidget;
	m_tabs->setObjectName("npcTabs");
	m_tabs->addTab(buildStatsTab(), tr("Stats"));
	m_tabs->addTab(buildWeaponsTab(), tr("Weapons"));
	m_tabs->addTab(buildLookTab(), tr("Look"));
	m_tabs->addTab(buildSoundsTab(), tr("Sounds"));
	m_tabs->addTab(buildAnimationsTab(), tr("Animations"));
	m_tabs->setMinimumWidth(320);
	splitter->addWidget(m_tabs);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setStretchFactor(2, 0);
	splitter->setSizes({190, 520, 400});

	connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
		const auto* item = m_list->item(row);
		const QString cls = item ? item->data(Qt::UserRole).toString() : QString();
		m_current = -1;
		for (std::size_t i = 0; i < m_npcs.size(); ++i) {
			if (qs(m_npcs[i].className).compare(cls, Qt::CaseInsensitive) == 0) m_current = int(i);
		}
		showNpc();
	});
	connect(m_play, &QToolButton::clicked, this, [this] { setPlaying(!m_playing); });
	connect(m_scrub, &QSlider::sliderMoved, this, [this](int value) {
		m_cycle = float(value) / 1000.f;
		applyPose();
		updateTransport();
	});
}

QWidget* NpcsPage::buildStatsTab() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* box = new QWidget;
	m_statsForm = new QFormLayout(box);
	m_statsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	scroll->setWidget(box);
	layout->addWidget(scroll, 1);
	m_statsNote = noteLabel(tr("Empty fields keep the game's value. The game reads skill.cfg at every map start."));
	layout->addWidget(m_statsNote);
	auto* buttons = new QHBoxLayout;
	m_saveStats = button(tr("Save to mod"), "npcSaveStats", tr("Writes the changed values into the mod's cfg/skill.cfg"));
	m_restoreStats = button(tr("Restore original"), "npcRestoreStats", tr("Sets this NPC's values back to the game's"));
	buttons->addWidget(m_saveStats);
	buttons->addWidget(m_restoreStats);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	connect(m_saveStats, &QPushButton::clicked, this, [this] { saveStats(); });
	connect(m_restoreStats, &QPushButton::clicked, this, [this] {
		if (QMessageBox::question(this, tr("Restore original"), tr("Set the values of %1 back to the game's?").arg(currentNpc())) == QMessageBox::Yes) {
			restoreStats();
		}
	});
	return page;
}

QWidget* NpcsPage::buildWeaponsTab() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	layout->addWidget(noteLabel(tr("The weapon it holds when you place it (additionalequipment).")));
	m_weaponList = new QListWidget;
	m_weaponList->setObjectName("npcWeapons");
	layout->addWidget(m_weaponList, 1);
	auto* buttons = new QHBoxLayout;
	m_place = button(tr("Place in map"), "npcPlace", tr("Click in the map to place this NPC with the chosen weapon and model"));
	buttons->addWidget(m_place);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	layout->addWidget(noteLabel(tr("How hard NPCs hit with a weapon is on the Stats tab (sk_npc_dmg_*).")));
	connect(m_place, &QPushButton::clicked, this, &NpcsPage::placeInMap);
	connect(m_weaponList, &QListWidget::itemDoubleClicked, this, &NpcsPage::placeInMap);
	return page;
}

QWidget* NpcsPage::buildLookTab() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	auto* form = new QFormLayout;
	m_modelBox = new QComboBox;
	m_modelBox->setObjectName("npcModel");
	m_modelBox->setToolTip(tr("The NPC's models the game has (citizens: their heads)"));
	form->addRow(tr("Model"), m_modelBox);
	m_skin = new QComboBox;
	m_skin->setObjectName("npcSkin");
	form->addRow(tr("Skin"), m_skin);
	layout->addLayout(form);
	m_materialList = new QListWidget;
	m_materialList->setObjectName("npcMaterials");
	m_materialList->setIconSize(QSize(48, 48));
	layout->addWidget(m_materialList, 1);
	auto* buttons = new QHBoxLayout;
	m_editMaterial = button(tr("Edit material"), "npcEditMaterial", tr("Open it in the Material Editor"));
	m_enhance = button(tr("Enhance textures ..."), "npcEnhance", tr("Sharper, larger textures and bump maps for this model's materials"));
	m_glow = button(tr("Glow ..."), "npcGlow", tr("Make parts of the model's materials glow (eyes, lamps, screens)"));
	for (auto* b : {m_editMaterial, m_enhance, m_glow}) buttons->addWidget(b);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	connect(m_modelBox, &QComboBox::currentIndexChanged, this, [this] {
		if (m_modelBox->currentData().toString().compare(m_model, Qt::CaseInsensitive) != 0) loadModel();
	});
	connect(m_skin, &QComboBox::currentIndexChanged, this, [this] {
		if (m_model.isEmpty() || !m_editor->models()) return;
		const QString key = m_skin->currentIndex() > 0 ? qs(modelVariantKey({m_model.toStdString(), m_skin->currentIndex(), 0, {}})) : m_model;
		if (auto geometry = m_editor->models()->get(key)) {
			m_geometry = geometry;
			m_viewer->setModel(m_geometry, false);
			applyPose();
			showLook();
		}
	});
	connect(m_materialList, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
	connect(m_materialList, &QListWidget::itemDoubleClicked, this, [this] { m_editMaterial->click(); });
	connect(m_editMaterial, &QPushButton::clicked, this, [this] {
		if (auto* item = m_materialList->currentItem()) emit openMaterialRequested(item->data(Qt::UserRole).toString());
	});
	connect(m_enhance, &QPushButton::clicked, this, [this] {
		QStringList materials;
		for (int i = 0; i < m_materialList->count(); ++i) materials << m_materialList->item(i)->data(Qt::UserRole).toString();
		if (!materials.isEmpty()) emit enhanceTexturesRequested(materials);
	});
	connect(m_glow, &QPushButton::clicked, this, [this] { openGlow(); });
	return page;
}

QWidget* NpcsPage::buildSoundsTab() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	m_sounds = new QTreeWidget;
	m_sounds->setObjectName("npcSounds");
	m_sounds->setHeaderLabels({tr("Sound"), tr("Status")});
	m_sounds->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	layout->addWidget(m_sounds, 1);
	auto* buttons = new QHBoxLayout;
	m_playSound = button(tr("Play"), "npcPlaySound", tr("Play the selected sound"));
	m_replaceSound = button(tr("Replace ..."), "npcReplaceSound", tr("Your own sound file at the same place in the mod: the game plays it instead"));
	m_editSound = button(tr("Edit ..."), "npcEditSound", tr("Cut, fade, louder/quieter on the Sounds page"));
	m_restoreSound = button(tr("Restore"), "npcRestoreSound", tr("Take your replacement out: the game's sound plays again"));
	for (auto* b : {m_playSound, m_replaceSound, m_editSound, m_restoreSound}) buttons->addWidget(b);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	layout->addWidget(noteLabel(tr("Sounds named after the NPC (footsteps, pain, attacks). Spoken lines of citizens and soldiers come from sentences and are not listed.")));
	connect(m_sounds, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });
	connect(m_sounds, &QTreeWidget::itemDoubleClicked, this, [this] { m_playSound->click(); });
	connect(m_playSound, &QPushButton::clicked, this, [this] {
		if (auto* item = m_sounds->currentItem()) playSound(item->data(0, ROLE_VALUE).toString());
	});
	connect(m_replaceSound, &QPushButton::clicked, this, [this] {
		const QString wave = selectedWave();
		if (wave.isEmpty()) return;
		const QString file = QFileDialog::getOpenFileName(this, tr("Replace %1").arg(wave), {}, ImportDialog::fileFilter(ImportDialog::Kind::Sound));
		if (!file.isEmpty()) replaceWave(wave, file);
	});
	connect(m_editSound, &QPushButton::clicked, this, [this] {
		if (const QString wave = selectedWave(); !wave.isEmpty()) emit editSoundRequested(wave);
	});
	connect(m_restoreSound, &QPushButton::clicked, this, [this] { restoreWave(selectedWave()); });
	return page;
}

QWidget* NpcsPage::buildAnimationsTab() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	m_sequences = new QTreeWidget;
	m_sequences->setObjectName("npcSequences");
	m_sequences->setHeaderLabels({tr("Animation"), tr("Activity"), tr("Length")});
	m_sequences->setRootIsDecorated(false);
	m_sequences->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	layout->addWidget(m_sequences, 1);
	auto* buttons = new QHBoxLayout;
	m_editAnimation = button(tr("Edit animation ..."), "npcEditAnimation", tr("Change the poses of this animation with keyframes"));
	buttons->addWidget(m_editAnimation);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	layout->addWidget(noteLabel(tr("Double-click an animation to play it.")));
	connect(m_sequences, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
		if (!item) return;
		m_cycle = 0;
		applyPose();
		updateTransport();
		updateButtons();
	});
	connect(m_sequences, &QTreeWidget::itemDoubleClicked, this, [this] {
		m_cycle = 0;
		setPlaying(true);
	});
	connect(m_editAnimation, &QPushButton::clicked, this, [this] {
		if (!m_model.isEmpty()) emit editAnimationRequested(m_model, currentSequence());
	});
	return page;
}

void NpcsPage::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	if (m_dirty) refresh();
	if (m_playing) {
		m_clock.restart();
		m_timer->start();
	}
}

void NpcsPage::hideEvent(QHideEvent* event) {
	QWidget::hideEvent(event);
	m_timer->stop();
	if (m_player) m_player->stop();
}

std::filesystem::path NpcsPage::modDir() const {
	const auto* entry = m_editor->gameEntry();
	return entry ? entry->gameDir : std::filesystem::path();
}

void NpcsPage::refresh() {
	m_dirty = false;
	const QString kept = currentNpc();
	m_npcs.clear();
	if (auto* game = m_editor->game()) {
		const GameFileSystem* fs = &game->files;
		m_npcs = findNpcs(m_editor->entities(), [fs](std::string_view model) { return fs->exists(model); });
	}
	{
		QSignalBlocker block(m_list);
		m_list->clear();
		QString group;
		for (const auto& npc : m_npcs) {
			if (qs(npc.group) != group) {
				group = qs(npc.group);
				auto* header = new QListWidgetItem(group.toUpper());
				header->setFlags(Qt::NoItemFlags);
				QFont font = header->font();
				font.setBold(true);
				header->setFont(font);
				m_list->addItem(header);
			}
			auto* item = new QListWidgetItem(qs(npc.name));
			item->setToolTip(qs(npc.className));
			item->setData(Qt::UserRole, qs(npc.className));
			m_list->addItem(item);
		}
	}
	if (m_npcs.empty()) {
		m_current = -1;
		m_viewer->setModel(nullptr);
		m_viewer->setMessage(m_editor->game() ? tr("This game has no NPC models") : tr("Open a game first"));
		showNpc();
		return;
	}
	if (!selectNpc(kept)) selectNpc(qs(m_npcs.front().className));
	if (!m_scriptsLoaded && !m_scriptLoader.isRunning() && m_editor->game()) {
		const GameFileSystem* fs = &m_editor->game()->files;
		m_scriptLoader.setFuture(QtConcurrent::run([fs] { return soundScripts(*fs); }));
	}
}

bool NpcsPage::selectNpc(const QString& className) {
	if (className.isEmpty()) return false;
	for (int i = 0; i < m_list->count(); ++i) {
		if (m_list->item(i)->data(Qt::UserRole).toString().compare(className, Qt::CaseInsensitive) == 0) {
			if (m_list->currentRow() == i) {
				showNpc();
			} else {
				m_list->setCurrentRow(i);
			}
			return true;
		}
	}
	return false;
}

QString NpcsPage::currentNpc() const {
	return m_current >= 0 && m_current < int(m_npcs.size()) ? qs(m_npcs[std::size_t(m_current)].className) : QString();
}

const NpcInfo* NpcsPage::currentInfo() const {
	return m_current >= 0 && m_current < int(m_npcs.size()) ? &m_npcs[std::size_t(m_current)] : nullptr;
}

void NpcsPage::showNpc() {
	setPlaying(false);
	const auto* npc = currentInfo();
	m_title->setText(npc ? QStringLiteral("<b>%1</b>  %2").arg(qs(npc->name).toHtmlEscaped(), qs(npc->className).toHtmlEscaped()) : QString());
	{
		QSignalBlocker block(m_modelBox);
		m_modelBox->clear();
		if (npc) {
			for (const auto& model : npc->models) m_modelBox->addItem(qs(model).section('/', -1), qs(model));
		}
	}
	loadModel();
	showStats();
	showWeapons();
	showSounds();
}

bool NpcsPage::selectModel(const QString& model) {
	const int index = m_modelBox->findData(model, Qt::UserRole, Qt::MatchFixedString);
	if (index < 0) return false;
	m_modelBox->setCurrentIndex(index);
	if (m_model.compare(model, Qt::CaseInsensitive) != 0) loadModel();
	return true;
}

void NpcsPage::loadModel() {
	const QString model = m_modelBox->currentData().toString();
	m_model = model;
	m_animated.reset();
	m_cycle = 0;
	++m_generation;
	{
		QSignalBlocker block(m_skin);
		m_skin->clear();
	}
	if (model.isEmpty()) {
		m_geometry.reset();
		m_viewer->setModel(nullptr);
		showSequences();
		showLook();
		return;
	}
	const std::string path = model.toStdString();
	m_geometry = m_editor->models() ? m_editor->models()->get(model) : nullptr;
	m_viewer->setModel(m_geometry, true);   // framed: the whole NPC, free around it
	if (!m_geometry) m_viewer->setMessage(tr("Loading %1 ...").arg(model.section('/', -1)));
	if (auto* game = m_editor->game()) {
		if (const auto mdl = game->files.read(path)) {
			const auto skins = readSkinFamilies(*mdl);
			QSignalBlocker block(m_skin);
			for (int i = 0; i < std::max<int>(1, int(skins.size())); ++i) m_skin->addItem(tr("Skin %1").arg(i), i);
		}
		const GameFileSystem* fs = &game->files;
		m_loader.setProperty("generation", m_generation);
		m_loader.setFuture(QtConcurrent::run([fs, path] { return AnimatedModel::load(*fs, path); }));
	}
	showSequences();
	showLook();
}

void NpcsPage::showSequences() {
	const QString kept = currentSequence();
	QSignalBlocker block(m_sequences);
	m_sequences->clear();
	if (m_animated) {
		QTreeWidgetItem* select = nullptr;
		for (const auto& seq : m_animated->sequences()) {
			auto* item = new QTreeWidgetItem(m_sequences);
			item->setText(0, qs(seq.name));
			item->setText(1, qs(seq.activity));
			item->setText(2, tr("%1 s").arg(double(seq.duration()), 0, 'f', 2));
			item->setData(0, Qt::UserRole, qs(seq.name));
			if (seq.layer) item->setForeground(0, palette().color(QPalette::PlaceholderText));
			if (qs(seq.name).compare(kept, Qt::CaseInsensitive) == 0) select = item;
		}
		// The idle first, as the NPC stands in the map.
		if (!select && m_animated->idleSequence() >= 0) select = m_sequences->topLevelItem(m_animated->idleSequence());
		if (!select && m_sequences->topLevelItemCount() > 0) select = m_sequences->topLevelItem(0);
		if (select) m_sequences->setCurrentItem(select);
	}
	updateTransport();
	updateButtons();
}

int NpcsPage::sequenceIndex() const {
	return m_animated ? m_animated->findSequence(currentSequence().toStdString()) : -1;
}

QString NpcsPage::currentSequence() const {
	auto* item = m_sequences->currentItem();
	return item ? item->data(0, Qt::UserRole).toString() : QString();
}

bool NpcsPage::selectSequence(const QString& name) {
	for (int i = 0; i < m_sequences->topLevelItemCount(); ++i) {
		auto* item = m_sequences->topLevelItem(i);
		if (item->data(0, Qt::UserRole).toString().compare(name, Qt::CaseInsensitive) == 0) {
			m_sequences->setCurrentItem(item);
			return true;
		}
	}
	return false;
}

void NpcsPage::setPlaying(bool on) {
	m_playing = on && m_animated && sequenceIndex() >= 0;
	m_play->setText(m_playing ? QStringLiteral("❚❚") : QStringLiteral("▶"));
	if (m_playing) {
		if (m_cycle >= 1.f) m_cycle = 0;
		m_clock.restart();
		if (isVisible()) m_timer->start();
	} else {
		m_timer->stop();
	}
}

void NpcsPage::tick() {
	const int index = sequenceIndex();
	if (index < 0) {
		setPlaying(false);
		return;
	}
	const auto& seq = m_animated->sequences()[std::size_t(index)];
	m_cycle += float(m_clock.restart()) / 1000.f / std::max(seq.duration(), 1.f / 30.f);
	if (m_cycle >= 1.f) {
		if (seq.looping) {
			m_cycle = std::fmod(m_cycle, 1.f);
		} else {
			m_cycle = 1.f;
			setPlaying(false);
		}
	}
	applyPose();
	updateTransport();
}

void NpcsPage::applyPose() {
	if (!m_geometry || !m_animated) return;
	if (m_animated->bones().empty() || m_geometry->weights.size() != m_geometry->vertices.size()) return;
	std::vector<ModelVertex> vertices;
	skinVertices(*m_geometry, m_animated->skinning(sequenceIndex(), m_cycle), vertices);
	m_viewer->setPose(std::move(vertices));
}

void NpcsPage::updateTransport() {
	const int index = sequenceIndex();
	m_play->setEnabled(index >= 0);
	m_scrub->setEnabled(index >= 0);
	if (!m_scrub->isSliderDown()) {
		QSignalBlocker block(m_scrub);
		m_scrub->setValue(int(std::lround(m_cycle * 1000)));
	}
	if (index < 0) {
		m_time->clear();
		return;
	}
	const auto& seq = m_animated->sequences()[std::size_t(index)];
	const int frame = seq.frames > 1 ? int(std::lround(m_cycle * float(seq.frames - 1))) : 0;
	m_time->setText(tr("frame %1 / %2").arg(frame).arg(std::max(0, seq.frames - 1)));
}

std::map<std::string, std::string> NpcsPage::skillValues(bool modFirst) const {
	if (modFirst) {
		if (const auto own = readFileText(modDir() / "cfg" / "skill.cfg")) return readSkillValues(*own);
	}
	if (auto* game = m_editor->game()) {
		if (const auto text = game->files.readOriginal("cfg/skill.cfg", modDir())) return readSkillValues(*text);
		if (const auto text = game->files.readText("cfg/skill.cfg")) return readSkillValues(*text);
	}
	return {};
}

void NpcsPage::showStats() {
	while (m_statsForm->rowCount() > 0) m_statsForm->removeRow(0);
	m_statEditors.clear();
	m_statShown.clear();
	const auto* npc = currentInfo();
	if (!npc) return;
	const auto game = skillValues(false);
	const auto shown = skillValues(true);
	const auto addRow = [&](const std::string& name, const QString& label, const QString& help) {
		const QString key = qs(name);
		const auto it = shown.find(name);
		auto* line = new QLineEdit(it != shown.end() ? qs(it->second) : QString());
		const auto def = game.find(name);
		line->setPlaceholderText(def != game.end() ? tr("game: %1").arg(qs(def->second)) : tr("game default"));
		line->setObjectName("stat:" + key);
		line->setToolTip(help);
		auto* caption = new QLabel(label);
		caption->setToolTip(help);
		m_statsForm->addRow(caption, line);
		m_statEditors.insert(key, line);
		m_statShown.insert(key, line->text());
	};
	const auto values = npcSkillValues(*npc, game);
	if (!values.empty()) m_statsForm->addRow(capsLabel(tr("Health and damage")));
	for (const auto& value : values) addRow(value.name, qs(value.label), qs(value.help));
	// The damage of the weapons it carries when NPCs shoot them (shared by every NPC with that weapon).
	bool header = false;
	std::set<std::string> done;
	for (const auto& weapon : npc->weapons) {
		const std::string name = npcWeaponDamageValue(weapon);
		if (name.empty() || !done.insert(name).second) continue;
		if (!header) {
			m_statsForm->addRow(capsLabel(tr("Weapon damage when NPCs shoot")));
			header = true;
		}
		addRow(name, qs(prettyClassName(weapon)), tr("%1 - damage per hit of this weapon for every NPC (the ammo's value)").arg(qs(name)));
	}
	if (m_statEditors.isEmpty()) m_statsForm->addRow(noteLabel(tr("This NPC has no skill values (its health is set in the game's code).")));
}

bool NpcsPage::saveStats() {
	const auto dir = modDir();
	if (!currentInfo() || dir.empty()) return false;
	std::map<std::string, std::string> values;
	for (auto it = m_statEditors.cbegin(); it != m_statEditors.cend(); ++it) {
		auto* line = qobject_cast<QLineEdit*>(it.value());
		const QString value = line ? line->text().trimmed().remove('"') : QString();
		if (value == m_statShown.value(it.key()) || value.isEmpty()) continue;
		values[it.key().toStdString()] = value.toStdString();
	}
	if (values.empty()) {
		message(tr("Nothing changed"));
		return true;
	}
	std::string gameSkill;
	if (auto* game = m_editor->game()) gameSkill = game->files.readOriginal("cfg/skill.cfg", dir).value_or(game->files.readText("cfg/skill.cfg").value_or(""));
	std::string error;
	if (!saveSkillValues(dir, values, gameSkill, &error)) {
		message(tr("Could not save skill.cfg: %1").arg(qs(error)));
		return false;
	}
	noteModFile(dir / "cfg" / "skill.cfg");
	showStats();
	message(tr("Saved %1 into the mod's skill.cfg - start a new map (or reload it) in the game to see it").arg(currentNpc()));
	return true;
}

bool NpcsPage::restoreStats() {
	const auto dir = modDir();
	auto* game = m_editor->game();
	if (!currentInfo() || dir.empty() || !game) return false;
	if (!std::filesystem::exists(dir / "cfg" / "skill.cfg")) return true;
	const auto original = game->files.readOriginal("cfg/skill.cfg", dir);
	if (!original) {
		message(tr("The game has no skill.cfg to restore from"));
		return false;
	}
	std::vector<std::string> names;
	for (auto it = m_statEditors.cbegin(); it != m_statEditors.cend(); ++it) names.push_back(it.key().toStdString());
	const auto back = skillValuesToRestore(names, *original);
	std::string error;
	if (!back.empty() && !saveSkillValues(dir, back, *original, &error)) {
		message(tr("Could not restore: %1").arg(qs(error)));
		return false;
	}
	noteModFile(dir / "cfg" / "skill.cfg");
	showStats();
	message(tr("%1 has the game's values again").arg(currentNpc()));
	return true;
}

void NpcsPage::showWeapons() {
	QSignalBlocker block(m_weaponList);
	m_weaponList->clear();
	const auto* npc = currentInfo();
	if (!npc) return;
	auto* none = new QListWidgetItem(tr("No weapon"));
	none->setData(Qt::UserRole, QString());
	m_weaponList->addItem(none);
	for (const auto& weapon : npc->weapons) {
		auto* item = new QListWidgetItem(qs(prettyClassName(weapon)) + QStringLiteral("  (%1)").arg(qs(weapon)));
		item->setData(Qt::UserRole, qs(weapon));
		m_weaponList->addItem(item);
	}
	// The first weapon is the one the game gives by default.
	m_weaponList->setCurrentRow(npc->weapons.empty() ? 0 : 1);
	updateButtons();
}

QString NpcsPage::currentWeapon() const {
	auto* item = m_weaponList->currentItem();
	return item ? item->data(Qt::UserRole).toString() : QString();
}

bool NpcsPage::selectWeapon(const QString& weaponClass) {
	for (int i = 0; i < m_weaponList->count(); ++i) {
		if (m_weaponList->item(i)->data(Qt::UserRole).toString().compare(weaponClass, Qt::CaseInsensitive) == 0) {
			m_weaponList->setCurrentRow(i);
			return true;
		}
	}
	return false;
}

void NpcsPage::placeInMap() {
	const auto* npc = currentInfo();
	if (!npc) return;
	emit placeRequested(qs(npc->className), npcPlacementKeys(*npc, currentWeapon().toStdString(), m_model.toStdString()));
}

void NpcsPage::showLook() {
	const QString kept = m_materialList->currentItem() ? m_materialList->currentItem()->data(Qt::UserRole).toString() : QString();
	QSignalBlocker block(m_materialList);
	m_materialList->clear();
	if (m_geometry) {
		std::set<std::string> seen;
		for (const auto& mesh : m_geometry->meshes) {
			if (!seen.insert(toLower(mesh.material)).second) continue;
			const QString name = qs(mesh.material);
			auto* item = new QListWidgetItem(name);
			item->setData(Qt::UserRole, name);
			if (m_materials) {
				if (const auto info = m_materials->get(name); info && !info->image.isNull()) {
					item->setIcon(QIcon(QPixmap::fromImage(info->image.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
				}
			}
			m_materialList->addItem(item);
			if (name.compare(kept, Qt::CaseInsensitive) == 0) m_materialList->setCurrentItem(item);
		}
		if (!m_materialList->currentItem() && m_materialList->count() > 0) m_materialList->setCurrentRow(0);
	}
	updateButtons();
}

GlowDialog* NpcsPage::openGlow() {
	if (m_model.isEmpty() || !m_geometry) return nullptr;
	QStringList materials;
	for (const auto& mesh : m_geometry->meshes) materials << qs(mesh.material);
	materials.removeDuplicates();
	std::vector<std::pair<std::string, std::string>> animations;
	if (m_animated) {
		for (const auto& seq : m_animated->sequences()) animations.emplace_back(seq.name, seq.activity);
	}
	auto* dialog = new GlowDialog(m_editor, m_viewer, m_model, materials, std::move(animations), this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	connect(dialog, &GlowDialog::modChanged, this, &NpcsPage::modChanged);
	dialog->show();
	return dialog;
}

void NpcsPage::showSounds() {
	QSignalBlocker block(m_sounds);
	m_sounds->clear();
	const auto* npc = currentInfo();
	if (!npc) return;
	if (!m_scriptsLoaded) {
		new QTreeWidgetItem(m_sounds, {tr("Reading the sound scripts ...")});
		updateButtons();
		return;
	}
	const auto dir = modDir();
	for (const auto* entry : npcSounds(*npc, m_scripts)) {
		auto* item = new QTreeWidgetItem(m_sounds);
		item->setText(0, qs(entry->name));
		item->setData(0, ROLE_KIND, EntryRow);
		item->setData(0, ROLE_VALUE, qs(entry->name));
		for (const auto& raw : entry->waves) {
			const QString wave = qs(normalizePath(stripSoundChars(raw)));
			auto* child = new QTreeWidgetItem(item);
			child->setText(0, wave);
			child->setData(0, ROLE_KIND, WaveRow);
			child->setData(0, ROLE_VALUE, wave);
			if (soundReplacement(dir, wave.toStdString())) {
				child->setText(1, tr("replaced"));
			} else if (m_editor->game() && !m_editor->game()->files.exists("sound/" + wave.toStdString())) {
				child->setText(1, tr("missing"));
			}
		}
	}
	if (m_sounds->topLevelItemCount() == 0) new QTreeWidgetItem(m_sounds, {tr("No sounds named after this NPC")});
	updateButtons();
}

QString NpcsPage::selectedWave() const {
	auto* item = m_sounds->currentItem();
	if (!item) return {};
	if (item->data(0, ROLE_KIND).toInt() == WaveRow) return item->data(0, ROLE_VALUE).toString();
	if (item->data(0, ROLE_KIND).toInt() == EntryRow && item->childCount() > 0) return item->child(0)->data(0, ROLE_VALUE).toString();
	return {};
}

void NpcsPage::playSound(const QString& soundOrWave) {
	if (!m_player || !SoundPlayer::available() || !m_editor->game() || soundOrWave.isEmpty()) return;
	QString wave = soundOrWave;
	if (!endsWithI(wave.toStdString(), ".wav") && !endsWithI(wave.toStdString(), ".mp3")) {
		wave.clear();
		for (const auto& entry : m_scripts) {
			if (iequals(entry.name, soundOrWave.toStdString()) && !entry.waves.empty()) wave = qs(normalizePath(stripSoundChars(entry.waves.front())));
		}
	}
	if (wave.isEmpty()) return;
	const auto bytes = m_editor->game()->files.read("sound/" + wave.toStdString());
	if (!bytes) {
		message(tr("sound/%1 is missing").arg(wave));
		return;
	}
	std::string error;
	auto audio = decodeAudio(*bytes, wave.toStdString(), &error);
	if (!audio) {
		message(tr("Cannot play %1: %2").arg(wave, qs(error)));
		return;
	}
	m_player->play(std::make_shared<const DecodedAudio>(std::move(*audio)));
}

bool NpcsPage::replaceWave(const QString& wave, const QString& sourceFile) {
	const auto dir = modDir();
	auto* game = m_editor->game();
	if (dir.empty() || !game || wave.isEmpty()) return false;
	const auto bytes = readFileBytes(sourceFile);
	std::string error;
	auto audio = bytes ? decodeAudio(*bytes, sourceFile.toStdString(), &error) : std::nullopt;
	if (!audio) {
		message(tr("Cannot read %1: %2").arg(sourceFile, bytes ? qs(error) : tr("no access")));
		return false;
	}
	std::optional<DecodedAudio> original;
	if (const auto text = game->files.readOriginal("sound/" + wave.toStdString(), dir)) {
		const std::vector<std::byte> raw(reinterpret_cast<const std::byte*>(text->data()), reinterpret_cast<const std::byte*>(text->data() + text->size()));
		original = decodeAudio(raw, wave.toStdString());
	}
	if (!replaceSound(dir, wave.toStdString(), *audio, original ? &*original : nullptr, &error)) {
		message(tr("Could not replace %1: %2").arg(wave, qs(error)));
		return false;
	}
	noteModFile(dir / "sound" / toNativePath(normalizePath(stripSoundChars(wave.toStdString()))));
	showSounds();
	message(tr("%1 replaced in the mod - the game plays your sound there").arg(wave));
	return true;
}

bool NpcsPage::restoreWave(const QString& wave) {
	const auto dir = modDir();
	if (wave.isEmpty() || dir.empty()) return false;
	std::string error;
	if (!restoreSound(dir, wave.toStdString(), &error)) {
		message(tr("Could not restore %1: %2").arg(wave, qs(error)));
		return false;
	}
	noteModFile(dir / "sound" / toNativePath(normalizePath(stripSoundChars(wave.toStdString()))));
	showSounds();
	message(tr("%1: the game's sound again").arg(wave));
	return true;
}

void NpcsPage::updateButtons() {
	const auto* npc = currentInfo();
	const auto dir = modDir();
	m_saveStats->setEnabled(npc && !m_statEditors.isEmpty());
	m_restoreStats->setEnabled(npc && !m_statEditors.isEmpty() && std::filesystem::exists(dir / "cfg" / "skill.cfg"));
	m_place->setEnabled(npc != nullptr);
	m_editMaterial->setEnabled(m_materialList->currentItem() != nullptr);
	m_enhance->setEnabled(m_materialList->count() > 0);
	m_glow->setEnabled(m_geometry && !m_geometry->meshes.empty());
	const QString wave = selectedWave();
	auto* item = m_sounds->currentItem();
	const int kind = item ? item->data(0, ROLE_KIND).toInt() : 0;
	m_playSound->setEnabled(SoundPlayer::available() && kind != 0);
	m_replaceSound->setEnabled(!wave.isEmpty() && soundReplaceOptions(wave.toStdString(), nullptr).has_value());
	m_editSound->setEnabled(!wave.isEmpty());
	m_restoreSound->setEnabled(!wave.isEmpty() && soundReplacement(dir, wave.toStdString()).has_value());
	m_editAnimation->setEnabled(!m_model.isEmpty() && !currentSequence().isEmpty());
}

void NpcsPage::noteModFile(const std::filesystem::path& file) {
	auto* game = m_editor->game();
	if (!game) return;
	std::error_code ec;
	if (std::filesystem::exists(file, ec)) {
		game->files.noteWrittenFile(file);
	} else {
		game->files.noteRemovedFile(file);
	}
}

void NpcsPage::message(const QString& text) {
	emit m_editor->message(text);
}

} // namespace sme::app
