// weapons_page.cpp - The Weapons page (see weapons_page.h).

#include "weapons_page.h"
#include "model_check_dialog.h"

#include "editor.h"
#include "effects_panel.h"
#include "glow_dialog.h"
#include "import_dialog.h"
#include "material_cache.h"
#include "model_cache.h"
#include "model_rebuild_job.h"
#include "model_viewer.h"
#include "process_guard.h"
#include "sound_player.h"
#include "ui_helpers.h"

#include <sme/audio.h>
#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/material.h>
#include <sme/mod_menu.h>
#include <sme/model_export.h>
#include <sme/model_rebuild.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <set>

namespace sme::app {

namespace {

constexpr int ROLE_KIND = Qt::UserRole + 1;
constexpr int ROLE_VALUE = Qt::UserRole + 2;   // Slot name, wave path, or sequence name of an event
constexpr int ROLE_INDEX = Qt::UserRole + 3;   // Event index in its sequence

std::optional<std::vector<std::byte>> readFileBytes(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
	const QByteArray data = file.readAll();
	std::vector<std::byte> out(std::size_t(data.size()));
	std::memcpy(out.data(), data.constData(), out.size());
	return out;
}

} // namespace

WeaponsPage::WeaponsPage(Editor* editor, MaterialCache* materials, AssetBrowser* assets, QWidget* parent)
	: QWidget(parent), m_editor(editor), m_materials(materials), m_assets(assets), m_job(new ModelRebuildJob(editor, this)), m_player(new SoundPlayer(this)) {
	setObjectName("weaponsPage");
	m_loader.setParent(this);
	m_scriptLoader.setParent(this);
	buildUi();

	m_timer = new QTimer(this);
	m_timer->setInterval(16);
	connect(m_timer, &QTimer::timeout, this, &WeaponsPage::tick);

	connect(m_editor, &Editor::gameAboutToChange, this, [this] {
		waitForOwnTasks(this);
		m_loader.waitForFinished();
		m_scriptLoader.waitForFinished();
	});
	connect(m_editor, &Editor::gameChanged, this, [this] {
		m_dirty = true;
		m_textsRead = false;
		m_texts.reset();
		m_scriptsLoaded = false;
		m_scripts.clear();
		if (isVisible()) refresh();
	});
	if (m_editor->models()) {
		connect(m_editor->models(), &ModelCache::modelReady, this, [this](const QString& path) {
			if (m_model.isEmpty()) return;
			const QString wanted = m_skin && m_skin->currentIndex() > 0
				? qs(modelVariantKey({m_model.toStdString(), m_skin->currentIndex(), 0, {}}))
				: m_model;
			if (path.compare(wanted, Qt::CaseInsensitive) != 0) return;
			m_geometry = m_editor->models()->get(wanted);
			m_viewer->setModel(m_geometry, m_framePending);
			m_framePending = false;
			if (!m_geometry) m_viewer->setMessage(tr("Cannot load: %1").arg(m_editor->models()->error(wanted)));
			checkBounds();
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
		showSounds();   // the events are listed per animation
		applyPose();
		updateTransport();
	});
	connect(&m_scriptLoader, &QFutureWatcherBase::finished, this, [this] {
		m_scripts = m_scriptLoader.result();
		m_scriptsLoaded = true;
		showSounds();
	});
	connect(m_job, &ModelRebuildJob::output, this, [this](const QString& line) { m_buildLog->appendPlainText(line); });
	connect(m_job, &ModelRebuildJob::finished, this, [this](bool ok, const QString& text) {
		m_buildLog->appendPlainText(text);
		m_buildLog->setVisible(true);
		if (ok) {
			m_pendingEvents.clear();
			loadModel();   // The rebuilt model is what the game shows now
		}
		message(text);
		updateButtons();
	});
}

WeaponsPage::~WeaponsPage() {
	stopOwnProcesses(this);
	waitForOwnTasks(this);
}

void WeaponsPage::buildUi() {
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	auto* splitter = new QSplitter(Qt::Horizontal);
	layout->addWidget(splitter);

	// Left: the weapons.
	auto* left = new QWidget;
	auto* leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->addWidget(capsLabel(tr("Weapons")));
	m_list = new QListWidget;
	m_list->setObjectName("weaponList");
	leftLayout->addWidget(m_list, 1);
	m_showAll = new QCheckBox(tr("Show NPC and developer weapons"));
	m_showAll->setObjectName("weaponShowAll");
	m_showAll->setToolTip(tr("Also list weapons only NPCs carry (Alyx's gun, the citizens' package ...), debug and unfinished ones "
	                         "(cubemap, physgun ...) and weapons without a first-person model"));
	m_showAll->setChecked(QSettings().value("weapons/showAll", false).toBool());
	leftLayout->addWidget(m_showAll);
	leftLayout->addWidget(noteLabel(tr("From scripts/weapon_*.txt. Changes go into your mod as its own copies; the game's files stay as they are.")));
	left->setMinimumWidth(150);
	left->setMaximumWidth(240);
	splitter->addWidget(left);

	// Middle: the model and playback.
	auto* middle = new QWidget;
	auto* middleLayout = new QVBoxLayout(middle);
	middleLayout->setContentsMargins(0, 0, 0, 0);
	auto* header = new QHBoxLayout;
	m_title = new QLabel;
	m_title->setObjectName("weaponTitle");
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	header->addWidget(m_title, 1);
	m_viewMode = new QComboBox;
	m_viewMode->setObjectName("weaponView");
	m_viewMode->addItem(tr("Free view"), int(View::Free));
	m_viewMode->addItem(tr("First person"), int(View::FirstPerson));
	m_viewMode->addItem(tr("World model"), int(View::World));
	m_viewMode->setToolTip(tr("Free view: turn with the left mouse button, move with the right or middle one, zoom with the wheel\n"
	                          "First person: as the player sees it in the game (view model FOV of the script, else 54)\n"
	                          "World model: the model in the world (on the floor, in an NPC's hand)"));
	header->addWidget(m_viewMode);
	middleLayout->addLayout(header);
	m_viewNote = noteLabel(QString());
	m_viewNote->setObjectName("weaponViewNote");
	m_viewNote->hide();
	middleLayout->addWidget(m_viewNote);
	m_viewer = new ModelViewer(m_materials);
	m_viewer->setObjectName("weaponViewer");
	m_viewer->setMinimumSize(240, 200);
	middleLayout->addWidget(m_viewer, 1);
	auto* transport = new QHBoxLayout;
	m_play = new QToolButton;
	m_play->setObjectName("weaponPlay");
	m_play->setText(QStringLiteral("▶"));
	m_play->setToolTip(tr("Play / pause the animation"));
	transport->addWidget(m_play);
	m_scrub = new QSlider(Qt::Horizontal);
	m_scrub->setObjectName("weaponScrub");
	m_scrub->setRange(0, 1000);
	transport->addWidget(m_scrub, 1);
	m_time = new QLabel;
	m_time->setMinimumWidth(110);
	transport->addWidget(m_time);
	middleLayout->addLayout(transport);
	splitter->addWidget(middle);

	// Right: the tabs.
	m_tabs = new QTabWidget;
	m_tabs->setObjectName("weaponTabs");
	m_tabs->addTab(buildAnimationsTab(), tr("Animations"));
	m_tabs->addTab(buildSoundsTab(), tr("Sounds"));
	m_tabs->addTab(buildLookTab(), tr("Look"));
	m_tabs->addTab(buildStatsTab(), tr("Stats"));
	m_tabs->addTab(buildEffectsTab(), tr("Effects"));
	m_tabs->setMinimumWidth(320);
	splitter->addWidget(m_tabs);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setStretchFactor(2, 0);
	splitter->setSizes({190, 520, 400});

	connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
		m_current = row >= 0 && row < int(m_weapons.size()) ? row : -1;
		showWeapon();
	});
	connect(m_viewMode, &QComboBox::currentIndexChanged, this, [this] { setView(View(m_viewMode->currentData().toInt())); });
	connect(m_showAll, &QCheckBox::toggled, this, [this](bool on) {
		QSettings().setValue("weapons/showAll", on);
		refresh();
	});
	connect(m_play, &QToolButton::clicked, this, [this] { setPlaying(!m_playing); });
	connect(m_scrub, &QSlider::sliderMoved, this, [this](int value) {
		m_cycle = float(value) / 1000.f;
		applyPose();
		updateTransport();
	});
}

QWidget* WeaponsPage::buildAnimationsTab() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	m_sequences = new QTreeWidget;
	m_sequences->setObjectName("weaponSequences");
	m_sequences->setHeaderLabels({tr("Animation"), tr("Activity"), tr("Length"), tr("Events")});
	m_sequences->setRootIsDecorated(false);
	m_sequences->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	layout->addWidget(m_sequences, 1);
	auto* buttons = new QHBoxLayout;
	m_editAnimation = new QPushButton(tr("Edit animation ..."));
	m_editAnimation->setObjectName("weaponEditAnimation");
	m_editAnimation->setToolTip(tr("Change the poses of this animation with keyframes"));
	buttons->addWidget(m_editAnimation);
	m_handPose = new QPushButton(tr("Change hand pose ..."));
	m_handPose->setObjectName("weaponHandPose");
	m_handPose->setToolTip(tr("Hold the weapon differently in every animation: move the hands and arms, bend the fingers"));
	buttons->addWidget(m_handPose);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	layout->addWidget(noteLabel(tr("Double-click an animation to play it. Its sounds and effects are on the Sounds tab (Animation events).")));
	connect(m_sequences, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
		if (!item) return;
		m_cycle = 0;
		applyPose();
		updateTransport();
		updateButtons();
		showSounds();
	});
	connect(m_sequences, &QTreeWidget::itemDoubleClicked, this, [this] {
		m_cycle = 0;
		setPlaying(true);
	});
	connect(m_editAnimation, &QPushButton::clicked, this, [this] {
		if (!m_model.isEmpty()) emit editAnimationRequested(m_model, currentSequence());
	});
	connect(m_handPose, &QPushButton::clicked, this, [this] {
		if (!m_model.isEmpty()) emit handPoseRequested(m_model);
	});
	return page;
}

QWidget* WeaponsPage::buildSoundsTab() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	m_sounds = new QTreeWidget;
	m_sounds->setObjectName("weaponSounds");
	m_sounds->setHeaderLabels({tr("Sound"), tr("Plays")});
	m_sounds->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_sounds->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	layout->addWidget(m_sounds, 1);

	const auto button = [](const QString& text, const char* name, const QString& tip) {
		auto* b = new QPushButton(text);
		b->setObjectName(name);
		b->setToolTip(tip);
		return b;
	};
	auto* row1 = new QHBoxLayout;
	m_playSound = button(tr("Play"), "weaponPlaySound", tr("Play the selected sound"));
	m_replaceSound = button(tr("Replace ..."), "weaponReplaceSound", tr("Your own sound file at the same place in the mod: the game plays it instead"));
	m_editSound = button(tr("Edit ..."), "weaponEditSound", tr("Cut, fade, louder/quieter on the Sounds page"));
	m_restoreSound = button(tr("Restore"), "weaponRestoreSound", tr("Take your replacement out: the game's sound plays again"));
	for (auto* b : {m_playSound, m_replaceSound, m_editSound, m_restoreSound}) row1->addWidget(b);
	row1->addStretch(1);
	layout->addLayout(row1);
	auto* row2 = new QHBoxLayout;
	m_slotSound = button(tr("Use another sound ..."), "weaponSlotSound", tr("This slot of the weapon plays another sound of the game (saved in the mod's copy of the script)"));
	m_eventSound = button(tr("Change sound ..."), "weaponEventSound", tr("The animation event plays another sound"));
	m_eventFrame = button(tr("Frame ..."), "weaponEventFrame", tr("Move the event to another frame of the animation"));
	m_eventRemove = button(tr("Remove"), "weaponEventRemove", tr("Remove the animation event"));
	m_eventAdd = button(tr("Add sound ..."), "weaponEventAdd", tr("A new sound in the selected animation, at the frame shown"));
	for (auto* b : {m_slotSound, m_eventSound, m_eventFrame, m_eventRemove, m_eventAdd}) row2->addWidget(b);
	row2->addStretch(1);
	layout->addLayout(row2);
	auto* row3 = new QHBoxLayout;
	m_build = button(tr("Build model"), "weaponBuildModel", tr("Rebuild the view model with the changed animation events into the mod (studiomdl)"));
	m_restoreModel = button(tr("Restore model"), "weaponRestoreModel", tr("Take the rebuilt model out of the mod: the game's model counts again"));
	row3->addWidget(m_build);
	row3->addWidget(m_restoreModel);
	m_buildNote = noteLabel(QString());
	row3->addWidget(m_buildNote, 1);
	layout->addLayout(row3);
	m_buildLog = new QPlainTextEdit;
	m_buildLog->setObjectName("weaponBuildLog");
	m_buildLog->setReadOnly(true);
	m_buildLog->setMaximumHeight(110);
	m_buildLog->setVisible(false);
	layout->addWidget(m_buildLog);

	connect(m_sounds, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });
	connect(m_sounds, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		if (!item) return;
		if (item->data(0, ROLE_KIND).toInt() == EventRow) {
			// Jump to the event in its animation.
			selectSequence(item->data(0, ROLE_VALUE).toString());
			const auto events = eventsOf(item->data(0, ROLE_VALUE).toString());
			const int index = item->data(0, ROLE_INDEX).toInt();
			if (index >= 0 && index < int(events.size())) {
				m_cycle = events[std::size_t(index)].cycle;
				applyPose();
				updateTransport();
			}
		}
		m_playSound->click();
	});
	connect(m_playSound, &QPushButton::clicked, this, [this] {
		if (auto* item = m_sounds->currentItem()) {
			const int kind = item->data(0, ROLE_KIND).toInt();
			if (kind == WaveRow) playSound(item->data(0, ROLE_VALUE).toString());
			else if (kind == SlotRow || kind == EventRow) playSound(item->text(1));
		}
	});
	connect(m_replaceSound, &QPushButton::clicked, this, [this] {
		const QString wave = selectedWave();
		if (wave.isEmpty()) return;
		const QString file = QFileDialog::getOpenFileName(this, tr("Replace %1").arg(wave), {}, ImportDialog::fileFilter(ImportDialog::Kind::Sound));
		if (!file.isEmpty()) replaceWave(wave, file);
	});
	connect(m_editSound, &QPushButton::clicked, this, [this] {
		const QString wave = selectedWave();
		if (!wave.isEmpty()) emit editSoundRequested(wave);
	});
	connect(m_restoreSound, &QPushButton::clicked, this, [this] { restoreWave(selectedWave()); });
	connect(m_slotSound, &QPushButton::clicked, this, [this] {
		auto* item = m_sounds->currentItem();
		while (item && item->data(0, ROLE_KIND).toInt() != SlotRow) item = item->parent();
		if (!item || !m_editor->game()) return;
		QStringList names;
		for (const auto& name : soundScriptNames(m_editor->game()->files)) names << qs(name);
		bool ok = false;
		const int at = std::max(0, int(names.indexOf(item->text(1))));
		const QString chosen = QInputDialog::getItem(this, tr("Sound for %1").arg(item->text(0)), tr("Sound script entry:"), names, at, true, &ok);
		if (ok && !chosen.trimmed().isEmpty()) setSlotSound(item->data(0, ROLE_VALUE).toString(), chosen.trimmed());
	});
	connect(m_eventSound, &QPushButton::clicked, this, [this] { editSelectedEvent(true); });
	connect(m_eventFrame, &QPushButton::clicked, this, [this] { editSelectedEvent(false); });
	connect(m_eventRemove, &QPushButton::clicked, this, [this] { removeSelectedEvent(); });
	connect(m_eventAdd, &QPushButton::clicked, this, [this] { addSoundEvent(); });
	connect(m_build, &QPushButton::clicked, this, [this] { buildModel(); });
	connect(m_restoreModel, &QPushButton::clicked, this, [this] {
		if (QMessageBox::question(this, tr("Restore model"), tr("Take the rebuilt %1 out of the mod? Its event changes are forgotten.").arg(m_model)) ==
		    QMessageBox::Yes) {
			restoreModel();
		}
	});
	return page;
}

QWidget* WeaponsPage::buildLookTab() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	auto* skinRow = new QHBoxLayout;
	skinRow->addWidget(new QLabel(tr("Skin")));
	m_skin = new QComboBox;
	m_skin->setObjectName("weaponSkin");
	skinRow->addWidget(m_skin);
	skinRow->addStretch(1);
	layout->addLayout(skinRow);
	m_materialList = new QListWidget;
	m_materialList->setObjectName("weaponMaterials");
	m_materialList->setIconSize(QSize(48, 48));
	layout->addWidget(m_materialList, 1);
	auto* buttons = new QHBoxLayout;
	m_editMaterial = new QPushButton(tr("Edit material"));
	m_editMaterial->setObjectName("weaponEditMaterial");
	m_editMaterial->setToolTip(tr("Open it in the Material Editor (shine, bump map, colour ...)"));
	m_replaceTexture = new QPushButton(tr("Replace texture ..."));
	m_replaceTexture->setObjectName("weaponReplaceTexture");
	m_replaceTexture->setToolTip(tr("Your own picture (PNG, JPG, TGA, BMP) as the texture, at the same place in the mod"));
	m_restoreTexture = new QPushButton(tr("Restore texture"));
	m_restoreTexture->setObjectName("weaponRestoreTexture");
	m_enhance = new QPushButton(tr("Enhance textures ..."));
	m_enhance->setObjectName("weaponEnhance");
	m_enhance->setToolTip(tr("Sharper, larger textures and bump maps for this weapon's materials"));
	m_check = new QPushButton(tr("Check model"));
	m_check->setObjectName("weaponCheck");
	m_check->setToolTip(tr("Look for what would break the view model in the game: files, checksums, limits, missing materials, sounds and effects"));
	for (auto* b : {m_editMaterial, m_replaceTexture, m_restoreTexture, m_enhance, m_check}) buttons->addWidget(b);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	layout->addWidget(noteLabel(tr("A replaced texture keeps the model's mapping: any size works (it is made a power of two, at most 2048).")));
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
		if (const QString material = selectedMaterial(); !material.isEmpty()) emit openMaterialRequested(material);
	});
	connect(m_replaceTexture, &QPushButton::clicked, this, [this] {
		const QString material = selectedMaterial();
		if (material.isEmpty()) return;
		const QString file = QFileDialog::getOpenFileName(this, tr("Texture for %1").arg(material), {}, ImportDialog::fileFilter(ImportDialog::Kind::Material));
		if (!file.isEmpty()) replaceTexture(material, file);
	});
	connect(m_restoreTexture, &QPushButton::clicked, this, [this] { restoreTexture(selectedMaterial()); });
	connect(m_check, &QPushButton::clicked, this, [this] {
		if (!m_model.isEmpty()) openModelCheckDialog(m_editor, m_model, this);
	});
	connect(m_enhance, &QPushButton::clicked, this, [this] {
		QStringList materials;
		for (int i = 0; i < m_materialList->count(); ++i) materials << m_materialList->item(i)->data(Qt::UserRole).toString();
		if (!materials.isEmpty()) emit enhanceTexturesRequested(materials);
	});
	return page;
}

QWidget* WeaponsPage::buildEffectsTab() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	auto* glowRow = new QHBoxLayout;
	auto* glowText = new QLabel(tr("Glow: parts of the view model's materials light up (sights, screens, a hot barrel) - always or only during animations"));
	glowText->setWordWrap(true);
	glowRow->addWidget(glowText, 1);
	m_glow = new QPushButton(tr("Glow ..."));
	m_glow->setObjectName("weaponGlow");
	m_glow->setToolTip(tr("Make parts of the view model's materials glow, steady or pulsing, always or while an animation plays"));
	glowRow->addWidget(m_glow);
	layout->addLayout(glowRow);
	m_effects = new EffectsPanel(m_editor, m_assets);
	m_effects->setRebuildJob(m_job);
	layout->addWidget(m_effects);
	layout->addStretch(1);
	layout->addWidget(noteLabel(tr("Compile into model builds the view model with these effects, keeping its sounds and your sound changes from the Sounds tab.")));
	connect(m_glow, &QPushButton::clicked, this, [this] { openGlow(); });
	connect(m_effects, &EffectsPanel::compileRequested, this, &WeaponsPage::compileEffects);
	connect(m_effects, &EffectsPanel::modelRestored, this, [this] {
		m_pendingEvents.clear();
		loadModel();
		updateButtons();
	});
	return page;
}

bool WeaponsPage::compileEffects(const QString& model, const ModelRebuildChanges& effects) {
	ModelRebuildChanges changes = effects;
	changes.events = m_pendingEvents;   // Sound edits waiting for Build model go in the same build
	m_buildLog->clear();
	m_buildLog->appendPlainText(tr("Building %1 with its effects ...").arg(model));
	const bool started = m_job->start(model, changes);
	updateButtons();
	return started;
}

GlowDialog* WeaponsPage::openGlow(const QString& material) {
	const auto* script = currentScript();
	if (!script || script->viewModel().empty()) return nullptr;
	if (m_view == View::World) setView(View::FirstPerson);
	if (!m_geometry) return nullptr;
	QStringList materials;
	for (const auto& mesh : m_geometry->meshes) materials << qs(mesh.material);
	materials.removeDuplicates();
	std::vector<std::pair<std::string, std::string>> animations;
	if (m_animated) {
		for (const auto& seq : m_animated->sequences()) animations.emplace_back(seq.name, seq.activity);
	}
	auto* dialog = new GlowDialog(m_editor, m_viewer, qs(normalizePath(script->viewModel())), materials, std::move(animations), this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	connect(dialog, &GlowDialog::modChanged, this, &WeaponsPage::modChanged);
	if (!material.isEmpty()) dialog->selectMaterial(material);
	dialog->show();
	return dialog;
}

QWidget* WeaponsPage::buildStatsTab() {
	auto* page = new QWidget;
	auto* layout = new QVBoxLayout(page);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	m_statsBox = new QWidget;
	m_statsForm = new QFormLayout(m_statsBox);
	m_statsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	scroll->setWidget(m_statsBox);
	layout->addWidget(scroll, 1);
	m_statsNote = noteLabel(QString());
	layout->addWidget(m_statsNote);
	auto* buttons = new QHBoxLayout;
	m_saveStats = new QPushButton(tr("Save to mod"));
	m_saveStats->setObjectName("weaponSaveStats");
	m_saveStats->setToolTip(tr("Writes the changed values: the script as the mod's own copy, damage and ammo into the mod's cfg/skill.cfg"));
	m_restoreStats = new QPushButton(tr("Restore original"));
	m_restoreStats->setObjectName("weaponRestoreStats");
	m_restoreStats->setToolTip(tr("Takes the mod's copy of the script out and sets the skill values back to the game's"));
	buttons->addWidget(m_saveStats);
	buttons->addWidget(m_restoreStats);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	connect(m_saveStats, &QPushButton::clicked, this, [this] { saveStats(); });
	connect(m_restoreStats, &QPushButton::clicked, this, [this] {
		if (QMessageBox::question(this, tr("Restore original"), tr("Take your changes to %1 out of the mod?").arg(currentWeapon())) == QMessageBox::Yes) {
			restoreStats();
		}
	});
	return page;
}

void WeaponsPage::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	if (m_dirty) refresh();
	if (m_playing) {
		m_clock.restart();
		m_timer->start();
	}
}

void WeaponsPage::hideEvent(QHideEvent* event) {
	QWidget::hideEvent(event);
	m_timer->stop();
	if (m_player) m_player->stop();
}

std::filesystem::path WeaponsPage::modDir() const {
	const auto* entry = m_editor->gameEntry();
	return entry ? entry->gameDir : std::filesystem::path();
}

std::string WeaponsPage::lookupToken(std::string_view token) {
	if (!m_textsRead) {
		m_textsRead = true;
		const auto* entry = m_editor->gameEntry();
		const auto* game = m_editor->game();
		if (entry && game) {
			const std::string folder = toLower(displayPath(entry->gameDir.filename()));
			// The mod's own text file, else the game's (the HUD reads the mod's, see mod_base_files.h).
			for (const std::string& file : {"resource/" + folder + "_english.txt", std::string("resource/hl2_english.txt"), std::string("resource/ep2_english.txt")}) {
				if (const auto bytes = game->files.read(file)) {
					m_texts = parseLocalizationFile(decodeTextFile(*bytes));
					if (!m_texts->tokens.empty()) break;
				}
			}
		}
	}
	return m_texts ? m_texts->get(token) : std::string();
}

void WeaponsPage::refresh() {
	m_dirty = false;
	const QString kept = currentWeapon();
	m_weapons.clear();
	m_pendingEvents.clear();
	std::size_t hidden = 0;
	if (auto* game = m_editor->game()) {
		const bool all = m_showAll->isChecked();
		const GameFileSystem& files = game->files;
		for (auto& weapon : findWeapons(files, modDir())) {
			// Player weapons (and the mod's own ones) with a first-person model; the rest on request.
			if (all || listedByDefault(weapon, [&files](std::string_view model) { return files.exists(model); })) {
				m_weapons.push_back(std::move(weapon));
			} else {
				++hidden;
			}
		}
	}
	{
		QSignalBlocker block(m_list);
		m_list->clear();
		for (const auto& weapon : m_weapons) {
			const QString name = qs(weaponDisplayName(weapon, [this](std::string_view token) { return lookupToken(token); }));
			// The class under the name: several weapons share a print name (NPC weapons borrow the SMG's).
			auto* item = new QListWidgetItem(name + (weapon.inMod ? QStringLiteral("  •") : QString()) + QStringLiteral("\n") + qs(weapon.className));
			QString tip = qs(weapon.className) + (weapon.inMod ? tr(" - the mod has its own script") : QString());
			switch (weaponRole(weapon.className)) {
			case WeaponRole::Npc: tip += tr("\nOnly NPCs carry it in the game"); break;
			case WeaponRole::Developer: tip += tr("\nA debug, unfinished or template weapon of the game's code"); break;
			case WeaponRole::Unknown: tip += tr("\nNot a weapon of the game's code: your mod's own (its code decides what it does)"); break;
			case WeaponRole::Player: break;
			}
			item->setToolTip(tip);
			item->setData(Qt::UserRole, qs(weapon.className));
			item->setData(Qt::UserRole + 1, name);
			m_list->addItem(item);
		}
	}
	m_showAll->setText(hidden > 0 ? tr("Show NPC and developer weapons (%1 more)").arg(hidden) : tr("Show NPC and developer weapons"));
	if (m_weapons.empty()) {
		m_current = -1;
		m_viewer->setModel(nullptr);
		m_viewer->setMessage(m_editor->game() ? (hidden > 0 ? tr("No player weapons found - tick \"Show NPC and developer weapons\"")
		                                                    : tr("This game has no weapon scripts (scripts/weapon_*.txt)"))
		                                      : tr("Open a game first"));
		showWeapon();
		return;
	}
	if (!selectWeapon(kept)) m_list->setCurrentRow(0);
	// The sound scripts, read once per game in the background (all of the game's script files).
	if (!m_scriptsLoaded && !m_scriptLoader.isRunning() && m_editor->game()) {
		const GameFileSystem* fs = &m_editor->game()->files;
		m_scriptLoader.setFuture(QtConcurrent::run([fs] { return soundScripts(*fs); }));
	}
}

bool WeaponsPage::selectWeapon(const QString& className) {
	for (int i = 0; i < m_list->count(); ++i) {
		if (m_list->item(i)->data(Qt::UserRole).toString().compare(className, Qt::CaseInsensitive) == 0) {
			if (m_list->currentRow() == i) {
				m_current = i;
				showWeapon();
			} else {
				m_list->setCurrentRow(i);
			}
			return true;
		}
	}
	return false;
}

QString WeaponsPage::currentWeapon() const {
	return m_current >= 0 && m_current < int(m_weapons.size()) ? qs(m_weapons[std::size_t(m_current)].className) : QString();
}

const WeaponScript* WeaponsPage::currentScript() const {
	return m_current >= 0 && m_current < int(m_weapons.size()) ? &m_weapons[std::size_t(m_current)] : nullptr;
}

QString WeaponsPage::currentModel() const {
	return m_model;
}

void WeaponsPage::showWeapon() {
	setPlaying(false);
	m_pendingEvents.clear();
	const auto* script = currentScript();
	m_title->setText(script ? QStringLiteral("<b>%1</b>  %2").arg(m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole + 1).toString().toHtmlEscaped() : QString(),
	                                                              qs(script->className).toHtmlEscaped())
	                        : QString());
	loadModel();
	showStats();
	updateButtons();
}

void WeaponsPage::setView(View view) {
	m_view = view;
	{
		QSignalBlocker block(m_viewMode);
		m_viewMode->setCurrentIndex(m_viewMode->findData(int(view)));
	}
	loadModel();
}

void WeaponsPage::loadModel() {
	const auto* script = currentScript();
	const std::string path = script ? normalizePath(m_view == View::World ? script->worldModel() : script->viewModel()) : std::string();
	const QString model = qs(path);
	float fov = 54;
	if (script) {
		const float own = QString(qs(script->get("viewmodel_fov"))).toFloat();
		if (own > 0) fov = own;
	}
	// First person only for a first-person model: anything else (an NPC weapon's world model, a
	// debug model) sits at the eye and the camera would be inside it - it is shown in free view.
	const bool firstPerson = m_view == View::FirstPerson && isViewModelPath(path);
	if (m_view == View::FirstPerson && !path.empty() && !firstPerson) {
		m_viewNote->setText(tr("%1 is not a first-person model - shown in free view").arg(qs(path).section('/', -1)));
		m_viewNote->show();
	} else {
		m_viewNote->hide();
	}
	m_viewer->setFirstPerson(firstPerson, fov);
	m_model = model;
	m_animated.reset();
	m_modelEvents.clear();
	m_cycle = 0;
	++m_generation;
	{
		QSignalBlocker block(m_skin);
		m_skin->clear();
	}
	if (model.isEmpty()) {
		m_geometry.reset();
		m_viewer->setModel(nullptr);
		m_viewer->setMessage(script ? tr("The script names no model") : QString());
		if (m_effects) m_effects->setModel({}, nullptr);
		showSequences();
		showSounds();
		showLook();
		return;
	}
	if (m_editor->models()) m_editor->models()->forget(model);   // A rebuild may have changed it
	m_geometry = m_editor->models() ? m_editor->models()->get(model) : nullptr;
	m_viewer->setModel(m_geometry, !firstPerson);
	// A model that is still loading is framed when it arrives (modelReady), not around nothing.
	m_framePending = !m_geometry && !firstPerson;
	if (!m_geometry) m_viewer->setMessage(tr("Loading %1 ...").arg(model.section('/', -1)));
	checkBounds();
	if (auto* game = m_editor->game()) {
		if (const auto mdl = game->files.read(path)) {
			m_modelEvents = readSequenceEvents(*mdl);
			// A model rebuilt with effects has their events: they belong to the Effects tab.
			if (const auto effects = m_job->storedChanges(model).effects) removeAddedEvents(m_modelEvents, *effects);
			const auto skins = readSkinFamilies(*mdl);
			QSignalBlocker block(m_skin);
			for (int i = 0; i < std::max<int>(1, int(skins.size())); ++i) m_skin->addItem(tr("Skin %1").arg(i), i);
		}
		const GameFileSystem* fs = &game->files;
		m_loader.setProperty("generation", m_generation);
		m_loader.setFuture(QtConcurrent::run([fs, path] { return AnimatedModel::load(*fs, path); }));
	}
	if (m_effects) {
		const QString viewModel = script ? qs(normalizePath(script->viewModel())) : QString();
		if (viewModel.compare(m_effects->model(), Qt::CaseInsensitive) != 0 || viewModel == model) {
			m_effects->setModel(viewModel, viewModel == model ? m_viewer : nullptr);
		}
	}
	showSequences();
	showSounds();
	showLook();
}

void WeaponsPage::showSequences() {
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
			item->setText(3, QString::number(eventsOf(qs(seq.name)).size()));
			item->setData(0, Qt::UserRole, qs(seq.name));
			if (seq.layer) item->setForeground(0, palette().color(QPalette::PlaceholderText));
			if (qs(seq.name).compare(kept, Qt::CaseInsensitive) == 0) select = item;
			// The idle first, as the game shows the weapon in the hands.
			if (!select && (iequals(seq.activity, "ACT_VM_IDLE") || iequals(seq.name, "idle01") || iequals(seq.name, "idle"))) select = item;
		}
		if (!select && m_sequences->topLevelItemCount() > 0) select = m_sequences->topLevelItem(0);
		if (select) m_sequences->setCurrentItem(select);
	}
	updateTransport();
}

int WeaponsPage::sequenceIndex() const {
	if (!m_animated) return -1;
	return m_animated->findSequence(currentSequence().toStdString());
}

QString WeaponsPage::currentSequence() const {
	auto* item = m_sequences->currentItem();
	return item ? item->data(0, Qt::UserRole).toString() : QString();
}

bool WeaponsPage::selectSequence(const QString& name) {
	for (int i = 0; i < m_sequences->topLevelItemCount(); ++i) {
		auto* item = m_sequences->topLevelItem(i);
		if (item->data(0, Qt::UserRole).toString().compare(name, Qt::CaseInsensitive) == 0) {
			m_sequences->setCurrentItem(item);
			return true;
		}
	}
	return false;
}

void WeaponsPage::setPlaying(bool on) {
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

void WeaponsPage::tick() {
	const int index = sequenceIndex();
	if (index < 0) {
		setPlaying(false);
		return;
	}
	const auto& seq = m_animated->sequences()[std::size_t(index)];
	const float seconds = float(m_clock.restart()) / 1000.f;
	m_cycle += seconds / std::max(seq.duration(), 1.f / 30.f);
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

void WeaponsPage::applyPose() {
	if (!m_geometry || !m_animated) return;
	if (m_animated->bones().empty() || m_geometry->weights.size() != m_geometry->vertices.size()) return;
	std::vector<ModelVertex> vertices;
	skinVertices(*m_geometry, m_animated->skinning(sequenceIndex(), m_cycle), vertices);
	m_viewer->setPose(std::move(vertices));
}

void WeaponsPage::checkBounds() {
	if (!m_geometry || m_geometry->vertices.empty()) return;
	// A weapon model is at most a few hundred units long; a far bigger box means it is no weapon
	// model (a map-sized prop, broken data), and framing it would show nothing useful.
	const auto size = m_geometry->bounds.size();
	if (m_geometry->bounds.empty() || std::max({size.x, size.y, size.z}) > 4096) {
		m_viewer->setMessage(tr("This model is %1 units big - it does not look like a weapon model").arg(int(std::max({size.x, size.y, size.z}))));
	}
}

void WeaponsPage::updateTransport() {
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
	// Frames counted from 1 as the timeline shows them; a one-frame pose reads "frame 1 / 1".
	const int frames = std::max(1, seq.frames);
	const int frame = frames > 1 ? int(std::lround(m_cycle * float(frames - 1))) : 0;
	m_time->setText(seq.frames > 1 ? tr("frame %1 / %2").arg(frame + 1).arg(frames) : tr("one pose (no motion)"));
}

std::vector<ModelEvent> WeaponsPage::eventsOf(const QString& sequence) const {
	const std::string key = toLower(sequence.toStdString());
	if (const auto it = m_pendingEvents.find(key); it != m_pendingEvents.end()) return it->second;
	for (const auto& seq : m_modelEvents) {
		if (iequals(seq.sequence, key)) return seq.events;
	}
	return {};
}

void WeaponsPage::setEvents(const QString& sequence, std::vector<ModelEvent> events) {
	m_pendingEvents[toLower(sequence.toStdString())] = std::move(events);
	showSequences();
	showSounds();
	updateButtons();
}

QStringList WeaponsPage::wavesOf(const QString& sound) const {
	QStringList out;
	const std::string name = sound.toStdString();
	if (endsWithI(name, ".wav") || endsWithI(name, ".mp3")) {
		out << qs(normalizePath(stripSoundChars(name)));
		return out;
	}
	for (const auto& entry : m_scripts) {
		if (!iequals(entry.name, name)) continue;
		for (const auto& wave : entry.waves) out << qs(normalizePath(stripSoundChars(wave)));
		break;
	}
	return out;
}

void WeaponsPage::showSounds() {
	const QString keptKind = m_sounds->currentItem() ? m_sounds->currentItem()->text(0) : QString();
	QSignalBlocker block(m_sounds);
	m_sounds->clear();
	const auto* script = currentScript();
	if (!script) return;
	const auto dir = modDir();
	const auto status = [this, &dir](const QString& wave) {
		if (soundReplacement(dir, wave.toStdString())) return tr("replaced");
		if (!dir.empty() && resolveI(dir, "sound/" + wave.toStdString())) return tr("mod's own");
		if (m_editor->game() && !m_editor->game()->files.exists("sound/" + wave.toStdString())) return tr("missing");
		return QString();
	};
	const auto addWaves = [&](QTreeWidgetItem* parent, const QString& sound) {
		for (const QString& wave : wavesOf(sound)) {
			auto* item = new QTreeWidgetItem(parent);
			item->setText(0, wave);
			item->setText(1, status(wave));
			item->setData(0, ROLE_KIND, WaveRow);
			item->setData(0, ROLE_VALUE, wave);
		}
	};
	// The weapon script's scriptRoot.
	auto* scriptRoot = new QTreeWidgetItem(m_sounds, {tr("Weapon script")});
	scriptRoot->setFirstColumnSpanned(true);
	for (const auto& [slot, sound] : script->sounds()) {
		auto* item = new QTreeWidgetItem(scriptRoot);
		item->setText(0, qs(weaponSoundSlotLabel(slot)));
		item->setText(1, qs(sound));
		item->setToolTip(0, qs(slot));
		item->setData(0, ROLE_KIND, SlotRow);
		item->setData(0, ROLE_VALUE, qs(slot));
		addWaves(item, qs(sound));
	}
	if (!m_scriptsLoaded) new QTreeWidgetItem(scriptRoot, {tr("Reading the sound scripts ...")});
	// The model's animation events (all events, sounds playable).
	auto* events = new QTreeWidgetItem(m_sounds, {tr("Animation events")});
	events->setFirstColumnSpanned(true);
	if (m_animated) {
		for (const auto& seq : m_animated->sequences()) {
			const QString name = qs(seq.name);
			const auto list = eventsOf(name);
			for (std::size_t i = 0; i < list.size(); ++i) {
				const auto& e = list[i];
				auto* item = new QTreeWidgetItem(events);
				item->setText(0, tr("%1, frame %2: %3").arg(name).arg(e.frame(seq.frames)).arg(qs(modelEventLabel(e))));
				item->setText(1, qs(e.options));
				if (const auto* known = findKnownModelEvent(e.id())) item->setToolTip(0, qs(known->description));
				item->setData(0, ROLE_KIND, EventRow);
				item->setData(0, ROLE_VALUE, name);
				item->setData(0, ROLE_INDEX, int(i));
				if (m_pendingEvents.count(toLower(seq.name))) {
					QFont font = item->font(0);
					font.setItalic(true);
					item->setFont(0, font);
				}
				if (isSoundEvent(e)) addWaves(item, qs(e.options));
			}
		}
	}
	if (events->childCount() == 0) new QTreeWidgetItem(events, {m_animated ? tr("No events in this model") : tr("Reading the animations ...")});
	m_sounds->expandItem(scriptRoot);
	m_sounds->expandItem(events);
	if (!keptKind.isEmpty()) {
		const auto found = m_sounds->findItems(keptKind, Qt::MatchExactly | Qt::MatchRecursive);
		if (!found.isEmpty()) m_sounds->setCurrentItem(found.front());
	}
	updateButtons();
}

void WeaponsPage::showLook() {
	const QString kept = selectedMaterial();
	QSignalBlocker block(m_materialList);
	m_materialList->clear();
	if (!m_geometry) return;
	std::set<std::string> seen;
	const auto dir = modDir();
	for (const auto& mesh : m_geometry->meshes) {
		if (!seen.insert(toLower(mesh.material)).second) continue;
		const QString name = qs(mesh.material);
		auto* item = new QListWidgetItem(name);
		item->setData(Qt::UserRole, name);
		const QString texture = baseTextureOf(name);
		if (!texture.isEmpty() && textureReplacement(dir, texture.toStdString())) item->setText(name + tr("  (texture replaced)"));
		if (m_materials) {
			if (const auto info = m_materials->get(name); info && !info->image.isNull()) {
				item->setIcon(QIcon(QPixmap::fromImage(info->image.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
			}
		}
		m_materialList->addItem(item);
		if (name.compare(kept, Qt::CaseInsensitive) == 0) m_materialList->setCurrentItem(item);
	}
	if (!m_materialList->currentItem() && m_materialList->count() > 0) m_materialList->setCurrentRow(0);
	updateButtons();
}

void WeaponsPage::showStats() {
	// Rebuilt per weapon: a form row per field and skill value.
	while (m_statsForm->rowCount() > 0) m_statsForm->removeRow(0);
	m_statEditors.clear();
	m_statShown.clear();
	const auto* script = currentScript();
	if (!script) {
		m_statsNote->clear();
		return;
	}
	const auto addRow = [this](const QString& key, const QString& label, const QString& help, QWidget* editor, const QString& shown) {
		editor->setObjectName("stat:" + key);
		editor->setToolTip(help);
		auto* caption = new QLabel(label);
		caption->setToolTip(help);
		m_statsForm->addRow(caption, editor);
		m_statEditors.insert(key, editor);
		m_statShown.insert(key, shown);
	};
	m_statsForm->addRow(capsLabel(tr("Weapon script")));
	for (const auto& field : weaponFields()) {
		const QString key = qs(field.key);
		const QString value = qs(script->get(field.key));
		const QString fallback = qs(field.fallback);
		QWidget* editor = nullptr;
		QString shown = value;
		switch (field.kind) {
		case WeaponField::Kind::Integer: {
			auto* spin = new QSpinBox;
			spin->setRange(-1, 100000);
			bool ok = false;
			const int v = value.toInt(&ok);
			const int f = fallback.toInt();
			spin->setValue(ok ? v : f);
			shown = QString::number(spin->value());
			editor = spin;
			break;
		}
		case WeaponField::Kind::Number: {
			auto* spin = new QDoubleSpinBox;
			spin->setRange(0, 1000);
			spin->setDecimals(2);
			spin->setSingleStep(0.1);
			bool ok = false;
			const double v = value.toDouble(&ok);
			spin->setValue(ok ? v : fallback.toDouble());
			shown = QString::number(spin->value(), 'g', 6);
			editor = spin;
			break;
		}
		case WeaponField::Kind::Switch: {
			auto* box = new QCheckBox;
			box->setChecked((value.isEmpty() ? fallback : value) != "0");
			shown = box->isChecked() ? "1" : "0";
			editor = box;
			break;
		}
		case WeaponField::Kind::Ammo: {
			auto* combo = new QComboBox;
			combo->setEditable(true);
			for (const auto ammo : weaponAmmoTypes()) combo->addItem(qs(ammo));
			combo->setCurrentText(value.isEmpty() ? fallback : value);
			shown = combo->currentText();
			editor = combo;
			break;
		}
		case WeaponField::Kind::Text:
		case WeaponField::Kind::Model: {
			auto* line = new QLineEdit(value);
			if (value.isEmpty() && !fallback.isEmpty()) line->setPlaceholderText(fallback);
			editor = line;
			break;
		}
		}
		addRow(key, qs(field.label), qs(field.help), editor, shown);
	}
	// Skill values of the ammo: the mod's skill.cfg, else the game's.
	const auto skills = weaponSkillValues(*script);
	if (!skills.empty()) {
		m_statsForm->addRow(capsLabel(tr("Damage and ammo (skill.cfg)")));
		std::map<std::string, std::string> values;
		if (const auto own = readFileText(modDir() / "cfg" / "skill.cfg")) {
			values = readSkillValues(*own);
		} else if (auto* game = m_editor->game()) {
			if (const auto text = game->files.readText("cfg/skill.cfg")) values = readSkillValues(*text);
		}
		for (const auto& skill : skills) {
			const QString key = qs(skill.name);
			const auto it = values.find(skill.name);
			auto* line = new QLineEdit(it != values.end() ? qs(it->second) : QString());
			line->setPlaceholderText(tr("game default"));
			addRow(key, qs(skill.label), tr("%1 - a console variable the mod's cfg/skill.cfg sets at every map start").arg(key), line, line->text());
		}
	}
	m_statsNote->setText(script->inMod ? tr("The mod has its own copy of %1.").arg(qs(script->file))
	                                   : tr("Saving makes the mod's own copy of %1.").arg(qs(script->file)));
}

bool WeaponsPage::saveStats() {
	const auto* script = currentScript();
	const auto dir = modDir();
	if (!script || dir.empty()) return false;
	WeaponScript changed = *script;
	bool scriptChanged = false;
	std::map<std::string, std::string> skills;
	for (auto it = m_statEditors.cbegin(); it != m_statEditors.cend(); ++it) {
		QString value;
		if (auto* spin = qobject_cast<QSpinBox*>(it.value())) value = QString::number(spin->value());
		else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(it.value())) value = QString::number(dspin->value(), 'g', 6);
		else if (auto* box = qobject_cast<QCheckBox*>(it.value())) value = box->isChecked() ? "1" : "0";
		else if (auto* combo = qobject_cast<QComboBox*>(it.value())) value = combo->currentText().trimmed();
		else if (auto* line = qobject_cast<QLineEdit*>(it.value())) value = line->text().trimmed();
		if (value == m_statShown.value(it.key())) continue;
		const std::string key = it.key().toStdString();
		if (startsWithI(key, "sk_")) {
			if (!value.isEmpty()) skills[key] = value.toStdString();
		} else {
			if (value.contains('"')) value.remove('"');
			changed.data.set(key, value.toStdString());
			scriptChanged = true;
		}
	}
	if (!scriptChanged && skills.empty()) {
		message(tr("Nothing changed"));
		return true;
	}
	std::string error;
	if (scriptChanged) {
		if (!saveWeaponScript(dir, changed, &error)) {
			message(tr("Could not save the script: %1").arg(qs(error)));
			return false;
		}
		noteModFile(dir / toNativePath(changed.file));
		m_weapons[std::size_t(m_current)] = changed;
		m_weapons[std::size_t(m_current)].inMod = true;
	}
	if (!skills.empty()) {
		std::string gameSkill;
		if (auto* game = m_editor->game()) gameSkill = game->files.readOriginal("cfg/skill.cfg", dir).value_or(game->files.readText("cfg/skill.cfg").value_or(""));
		if (!saveSkillValues(dir, skills, gameSkill, &error)) {
			message(tr("Could not save skill.cfg: %1").arg(qs(error)));
			return false;
		}
		noteModFile(dir / "cfg" / "skill.cfg");
	}
	showStats();
	message(tr("Saved %1 into the mod - start a new map (or reload it) in the game to see it").arg(currentWeapon()));
	return true;
}

bool WeaponsPage::restoreStats() {
	const auto* script = currentScript();
	const auto dir = modDir();
	if (!script || dir.empty()) return false;
	std::string error;
	const std::string file = script->file;
	if (!restoreWeaponScript(dir, file, &error)) {
		message(tr("Could not restore: %1").arg(qs(error)));
		return false;
	}
	noteModFile(dir / toNativePath(file));
	// Skill values back to the game's.
	if (auto* game = m_editor->game(); game && std::filesystem::exists(dir / "cfg" / "skill.cfg")) {
		if (const auto original = game->files.readOriginal("cfg/skill.cfg", dir)) {
			const auto values = readSkillValues(*original);
			std::map<std::string, std::string> back;
			for (const auto& skill : weaponSkillValues(*script)) {
				if (const auto it = values.find(skill.name); it != values.end()) back[skill.name] = it->second;
			}
			if (!back.empty()) saveSkillValues(dir, back, *original, &error);
		}
	}
	const QString kept = currentWeapon();
	refresh();
	selectWeapon(kept);
	message(tr("%1 is the game's again").arg(kept));
	return true;
}

QString WeaponsPage::selectedWave() const {
	auto* item = m_sounds->currentItem();
	if (!item) return {};
	if (item->data(0, ROLE_KIND).toInt() == WaveRow) return item->data(0, ROLE_VALUE).toString();
	// A slot or event: its first wave.
	for (int i = 0; i < item->childCount(); ++i) {
		if (item->child(i)->data(0, ROLE_KIND).toInt() == WaveRow) return item->child(i)->data(0, ROLE_VALUE).toString();
	}
	return {};
}

QTreeWidgetItem* WeaponsPage::selectedEventItem() const {
	auto* item = m_sounds->currentItem();
	while (item && item->data(0, ROLE_KIND).toInt() != EventRow) item = item->parent();
	return item;
}

void WeaponsPage::playSound(const QString& soundOrWave) {
	if (!m_player || !SoundPlayer::available() || !m_editor->game()) return;
	const QStringList waves = wavesOf(soundOrWave);
	if (waves.isEmpty()) {
		message(tr("%1: no sound file found").arg(soundOrWave));
		return;
	}
	const auto bytes = m_editor->game()->files.read("sound/" + waves.front().toStdString());
	if (!bytes) {
		message(tr("sound/%1 is missing").arg(waves.front()));
		return;
	}
	std::string error;
	auto audio = decodeAudio(*bytes, waves.front().toStdString(), &error);
	if (!audio) {
		message(tr("Cannot play %1: %2").arg(waves.front(), qs(error)));
		return;
	}
	m_player->play(std::make_shared<const DecodedAudio>(std::move(*audio)));
}

bool WeaponsPage::replaceWave(const QString& wave, const QString& sourceFile) {
	const auto dir = modDir();
	auto* game = m_editor->game();
	if (dir.empty() || !game) return false;
	const auto bytes = readFileBytes(sourceFile);
	std::string error;
	auto audio = bytes ? decodeAudio(*bytes, sourceFile.toStdString(), &error) : std::nullopt;
	if (!audio) {
		message(tr("Cannot read %1: %2").arg(sourceFile, bytes ? qs(error) : tr("no access")));
		return false;
	}
	// Sample rate and channels like the game's file (the engine plays it in the same place).
	std::optional<DecodedAudio> original;
	if (const auto text = game->files.readOriginal("sound/" + wave.toStdString(), dir)) {
		const std::vector<std::byte> raw(reinterpret_cast<const std::byte*>(text->data()), reinterpret_cast<const std::byte*>(text->data() + text->size()));
		original = decodeAudio(raw, wave.toStdString());
	}
	const auto record = replaceSound(dir, wave.toStdString(), *audio, original ? &*original : nullptr, &error);
	if (!record) {
		message(tr("Could not replace %1: %2").arg(wave, qs(error)));
		return false;
	}
	noteModFile(dir / "sound" / toNativePath(normalizePath(stripSoundChars(wave.toStdString()))));
	showSounds();
	message(tr("%1 replaced in the mod - the game plays your sound there").arg(wave));
	return true;
}

bool WeaponsPage::restoreWave(const QString& wave) {
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

bool WeaponsPage::setSlotSound(const QString& slot, const QString& sound) {
	const auto* script = currentScript();
	const auto dir = modDir();
	if (!script || dir.empty() || sound.contains('"')) return false;
	WeaponScript changed = *script;
	KvNode* block = changed.data.find("SoundData");
	if (!block) {
		changed.data.children.push_back(KvNode::makeBlock("SoundData"));
		block = &changed.data.children.back();
	}
	block->set(slot.toStdString(), sound.toStdString());
	std::string error;
	if (!saveWeaponScript(dir, changed, &error)) {
		message(tr("Could not save the script: %1").arg(qs(error)));
		return false;
	}
	noteModFile(dir / toNativePath(changed.file));
	changed.inMod = true;
	m_weapons[std::size_t(m_current)] = changed;
	showSounds();
	showStats();
	message(tr("%1 of %2 plays %3 now (saved in the mod's script)").arg(qs(weaponSoundSlotLabel(slot.toStdString())), currentWeapon(), sound));
	return true;
}

void WeaponsPage::editSelectedEvent(bool changeSound) {
	auto* item = selectedEventItem();
	if (!item || !m_animated) return;
	const QString sequence = item->data(0, ROLE_VALUE).toString();
	const int index = item->data(0, ROLE_INDEX).toInt();
	auto events = eventsOf(sequence);
	if (index < 0 || index >= int(events.size())) return;
	auto& e = events[std::size_t(index)];
	const int seqIndex = m_animated->findSequence(sequence.toStdString());
	const int frames = seqIndex >= 0 ? m_animated->sequences()[std::size_t(seqIndex)].frames : 1;
	bool ok = false;
	if (changeSound) {
		QStringList names;
		if (m_editor->game()) {
			for (const auto& name : soundScriptNames(m_editor->game()->files)) names << qs(name);
		}
		const int at = std::max(0, int(names.indexOf(qs(e.options))));
		const QString chosen = QInputDialog::getItem(this, tr("Sound of the event"), tr("Sound script entry or wave:"), names, at, true, &ok);
		if (!ok || chosen.trimmed().isEmpty()) return;
		e.options = cleanEventOptions(chosen.trimmed().toStdString());
	} else {
		const int frame = QInputDialog::getInt(this, tr("Frame of the event"), tr("Frame (0 to %1):").arg(frames - 1), e.frame(frames), 0, std::max(0, frames - 1), 1, &ok);
		if (!ok) return;
		e.setFrame(frame, frames);
	}
	setEvents(sequence, std::move(events));
}

void WeaponsPage::addSoundEvent() {
	const QString sequence = currentSequence();
	const int seqIndex = sequenceIndex();
	if (sequence.isEmpty() || seqIndex < 0) {
		message(tr("Choose an animation on the Animations tab first"));
		return;
	}
	QStringList names;
	if (m_editor->game()) {
		for (const auto& name : soundScriptNames(m_editor->game()->files)) names << qs(name);
	}
	bool ok = false;
	const QString chosen = QInputDialog::getItem(this, tr("Add a sound to %1").arg(sequence), tr("Sound script entry or wave:"), names, 0, true, &ok);
	if (!ok || chosen.trimmed().isEmpty()) return;
	auto events = eventsOf(sequence);
	ModelEvent e;
	e.name = "AE_CL_PLAYSOUND";
	e.options = cleanEventOptions(chosen.trimmed().toStdString());
	e.cycle = m_cycle;
	events.push_back(std::move(e));
	std::stable_sort(events.begin(), events.end(), [](const ModelEvent& a, const ModelEvent& b) { return a.cycle < b.cycle; });
	setEvents(sequence, std::move(events));
}

void WeaponsPage::removeSelectedEvent() {
	auto* item = selectedEventItem();
	if (!item) return;
	const QString sequence = item->data(0, ROLE_VALUE).toString();
	const int index = item->data(0, ROLE_INDEX).toInt();
	auto events = eventsOf(sequence);
	if (index < 0 || index >= int(events.size())) return;
	events.erase(events.begin() + index);
	setEvents(sequence, std::move(events));
}

bool WeaponsPage::buildModel() {
	const auto* script = currentScript();
	if (!script || script->viewModel().empty()) return false;
	ModelRebuildChanges changes;
	changes.events = m_pendingEvents;
	m_buildLog->clear();
	m_buildLog->setVisible(true);
	const QString model = qs(normalizePath(script->viewModel()));
	m_buildLog->appendPlainText(tr("Building %1 ...").arg(model));
	const bool started = m_job->start(model, changes);
	updateButtons();
	return started;
}

bool WeaponsPage::restoreModel() {
	const auto* script = currentScript();
	if (!script) return false;
	QString error;
	const QString model = qs(normalizePath(script->viewModel()));
	if (!m_job->restore(model, &error)) {
		message(tr("Could not restore %1: %2").arg(model, error));
		return false;
	}
	m_pendingEvents.clear();
	loadModel();
	message(tr("%1 is the game's model again").arg(model));
	return true;
}

QString WeaponsPage::selectedMaterial() const {
	auto* item = m_materialList ? m_materialList->currentItem() : nullptr;
	return item ? item->data(Qt::UserRole).toString() : QString();
}

QString WeaponsPage::baseTextureOf(const QString& material) const {
	auto* game = m_editor->game();
	if (!game || material.isEmpty()) return {};
	const auto parsed = loadMaterial(game->files, material.toStdString());
	if (!parsed) return {};
	for (const auto& [key, value] : parsed->params) {
		if (iequals(key, "$basetexture")) return qs(normalizePath(value));
	}
	return {};
}

bool WeaponsPage::replaceTexture(const QString& material, const QString& pictureFile) {
	const auto dir = modDir();
	const QString texture = baseTextureOf(material);
	if (dir.empty() || texture.isEmpty()) {
		message(tr("%1 has no base texture to replace").arg(material));
		return false;
	}
	const auto bytes = readFileBytes(pictureFile);
	std::string error;
	auto image = bytes ? decodeImageFile(*bytes, &error) : std::nullopt;
	if (!image) {
		message(tr("Cannot read %1: %2").arg(pictureFile, bytes ? qs(error) : tr("no access")));
		return false;
	}
	if (!sme::replaceTexture(dir, texture.toStdString(), *image, 2048, &error)) {
		message(tr("Could not replace the texture: %1").arg(qs(error)));
		return false;
	}
	noteModFile(dir / "materials" / toNativePath(texture.toStdString() + ".vtf"));
	if (m_materials) m_materials->reload(material);
	showLook();
	message(tr("%1 has your picture now (materials/%2.vtf in the mod)").arg(material, texture));
	return true;
}

bool WeaponsPage::restoreTexture(const QString& material) {
	const auto dir = modDir();
	const QString texture = baseTextureOf(material);
	if (dir.empty() || texture.isEmpty()) return false;
	std::string error;
	if (!sme::restoreTexture(dir, texture.toStdString(), &error)) {
		message(tr("Could not restore the texture: %1").arg(qs(error)));
		return false;
	}
	noteModFile(dir / "materials" / toNativePath(texture.toStdString() + ".vtf"));
	if (m_materials) m_materials->reload(material);
	showLook();
	message(tr("%1: the game's texture again").arg(material));
	return true;
}

void WeaponsPage::updateButtons() {
	auto* item = m_sounds->currentItem();
	const int kind = item ? item->data(0, ROLE_KIND).toInt() : 0;
	const QString wave = selectedWave();
	const auto dir = modDir();
	m_playSound->setEnabled(SoundPlayer::available() && (kind == WaveRow || kind == SlotRow || (kind == EventRow && !wave.isEmpty())));
	m_replaceSound->setEnabled(!wave.isEmpty() && soundReplaceOptions(wave.toStdString(), nullptr).has_value());
	m_editSound->setEnabled(!wave.isEmpty());
	m_restoreSound->setEnabled(!wave.isEmpty() && soundReplacement(dir, wave.toStdString()).has_value());
	bool inSlot = false;
	for (auto* i = item; i; i = i->parent()) inSlot = inSlot || i->data(0, ROLE_KIND).toInt() == SlotRow;
	m_slotSound->setEnabled(inSlot);
	auto* event = selectedEventItem();
	const bool soundEvent = event && [&] {
		const auto events = eventsOf(event->data(0, ROLE_VALUE).toString());
		const int index = event->data(0, ROLE_INDEX).toInt();
		return index >= 0 && index < int(events.size()) && isSoundEvent(events[std::size_t(index)]);
	}();
	m_eventSound->setEnabled(soundEvent);
	m_eventFrame->setEnabled(event != nullptr);
	m_eventRemove->setEnabled(event != nullptr);
	const auto* script = currentScript();
	const bool viewModel = script && !script->viewModel().empty();
	m_eventAdd->setEnabled(viewModel && m_animated && sequenceIndex() >= 0);
	m_build->setEnabled(viewModel && !m_pendingEvents.empty() && !m_job->busy());
	const QString model = viewModel ? qs(normalizePath(script->viewModel())) : QString();
	m_restoreModel->setEnabled(viewModel && !m_job->busy() && m_job->rebuilt(model));
	if (m_job->busy()) {
		m_buildNote->setText(tr("Building ..."));
	} else if (!m_pendingEvents.empty()) {
		m_buildNote->setText(tr("%n animation(s) changed - Build model puts them into the game", nullptr, int(m_pendingEvents.size())));
	} else if (viewModel && m_job->rebuilt(model)) {
		m_buildNote->setText(tr("The mod has a rebuilt copy of this model"));
	} else {
		m_buildNote->clear();
	}
	const QString material = selectedMaterial();
	m_editMaterial->setEnabled(!material.isEmpty());
	m_replaceTexture->setEnabled(!material.isEmpty());
	const QString texture = material.isEmpty() ? QString() : baseTextureOf(material);
	m_restoreTexture->setEnabled(!texture.isEmpty() && textureReplacement(dir, texture.toStdString()).has_value());
	m_enhance->setEnabled(m_materialList->count() > 0);
	m_check->setEnabled(!m_model.isEmpty());
	m_glow->setEnabled(viewModel && m_geometry && !m_geometry->meshes.empty());
	m_editAnimation->setEnabled(!m_model.isEmpty() && !currentSequence().isEmpty());
	m_handPose->setEnabled(!m_model.isEmpty());
	m_saveStats->setEnabled(script != nullptr);
	m_restoreStats->setEnabled(script && weaponScriptSaved(dir, script->file));
}

void WeaponsPage::noteModFile(const std::filesystem::path& file) {
	auto* game = m_editor->game();
	if (!game) return;
	std::error_code ec;
	if (std::filesystem::exists(file, ec)) {
		game->files.noteWrittenFile(file);
	} else {
		game->files.noteRemovedFile(file);
	}
}

void WeaponsPage::message(const QString& text) {
	emit m_editor->message(text);
}

} // namespace sme::app
