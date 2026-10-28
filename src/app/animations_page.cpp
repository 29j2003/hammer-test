// animations_page.cpp - The Animations page (see animations_page.h).

#include "animations_page.h"
#include "process_guard.h"

#include "asset_browser.h"
#include "editor.h"
#include "model_cache.h"
#include "model_viewer.h"
#include "samples_folder.h"

#include <sme/animation_use.h>
#include <sme/assets.h>
#include <sme/filesystem.h>
#include <sme/samples.h>
#include <sme/text.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <map>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace sme::app {

namespace {

QString qs(const std::string& s) {
	return QString::fromStdString(s);
}

/// A section title in a list: not selectable.
void addHeader(QListWidget* list, const QString& title) {
	auto* item = new QListWidgetItem(title, list);
	item->setFlags(Qt::NoItemFlags);
	QFont font = item->font();
	font.setBold(true);
	item->setFont(font);
	item->setForeground(QColor(240, 160, 40));
}

QLabel* hint(const QString& text) {
	auto* label = new QLabel(text);
	label->setWordWrap(true);
	label->setProperty("role", "description");
	return label;
}

constexpr int SCRUB_STEPS = 1000;

} // namespace

AnimationsPage::AnimationsPage(Editor* editor, AssetBrowser* assets, MaterialCache* materials, QWidget* parent)
    : QWidget(parent), m_editor(editor), m_assets(assets), m_materials(materials) {
	setObjectName("animationsPage");
	auto* outer = new QHBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto* split = new QSplitter(Qt::Horizontal);
	outer->addWidget(split);

	// Left: the models, the map's first.
	auto* left = new QWidget;
	auto* lv = new QVBoxLayout(left);
	lv->setContentsMargins(8, 8, 4, 8);
	m_search = new QLineEdit;
	m_search->setObjectName("animationModelSearch");
	m_search->setPlaceholderText(tr("Search models (words: alyx, combine soldier) ..."));
	m_search->setClearButtonEnabled(true);
	lv->addWidget(m_search);
	m_models = new QListWidget;
	m_models->setObjectName("animationModels");
	m_models->setUniformItemSizes(true);
	lv->addWidget(m_models, 1);
	split->addWidget(left);

	// Middle: the 3D view and the transport.
	auto* middle = new QWidget;
	auto* mv = new QVBoxLayout(middle);
	mv->setContentsMargins(4, 8, 4, 8);
	m_title = new QLabel(tr("Pick a model on the left"));
	m_title->setObjectName("animationTitle");
	QFont titleFont = m_title->font();
	titleFont.setBold(true);
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
	m_title->setFont(titleFont);
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	mv->addWidget(m_title);
	m_viewer = new ModelViewer(materials);
	m_viewer->setObjectName("animationViewer");
	m_viewer->setMinimumSize(320, 240);
	mv->addWidget(m_viewer, 1);
	auto* transport = new QHBoxLayout;
	m_play = new QToolButton;
	m_play->setObjectName("animationPlay");
	m_play->setText(QStringLiteral("▶"));
	m_play->setToolTip(tr("Play / pause (Space)"));
	m_play->setShortcut(Qt::Key_Space);
	m_play->setMinimumWidth(36);
	transport->addWidget(m_play);
	m_scrub = new QSlider(Qt::Horizontal);
	m_scrub->setObjectName("animationScrub");
	m_scrub->setRange(0, SCRUB_STEPS);
	m_scrub->setToolTip(tr("Drag to any point of the animation"));
	transport->addWidget(m_scrub, 1);
	m_time = new QLabel;
	m_time->setObjectName("animationTime");
	m_time->setMinimumWidth(130);
	transport->addWidget(m_time);
	m_speed = new QComboBox;
	m_speed->setObjectName("animationSpeed");
	for (const double s : {0.1, 0.25, 0.5, 1.0, 1.5, 2.0}) m_speed->addItem(QStringLiteral("%1×").arg(s), s);
	m_speed->setCurrentIndex(3);
	m_speed->setToolTip(tr("Playback speed"));
	transport->addWidget(m_speed);
	m_loop = new QCheckBox(tr("Loop"));
	m_loop->setObjectName("animationLoop");
	m_loop->setChecked(true);
	m_loop->setToolTip(tr("Start again at the end (off: stop on the last frame)"));
	transport->addWidget(m_loop);
	mv->addLayout(transport);
	split->addWidget(middle);

	// Right: the sequences, where the map uses them, and what to do with them.
	auto* right = new QWidget;
	auto* rv = new QVBoxLayout(right);
	rv->setContentsMargins(4, 8, 8, 8);
	m_sequenceSearch = new QLineEdit;
	m_sequenceSearch->setObjectName("animationSequenceSearch");
	m_sequenceSearch->setPlaceholderText(tr("Search animations (name or activity) ..."));
	m_sequenceSearch->setClearButtonEnabled(true);
	rv->addWidget(m_sequenceSearch);
	m_sequences = new QTreeWidget;
	m_sequences->setObjectName("animationSequences");
	m_sequences->setHeaderLabels({tr("Animation"), tr("Activity"), tr("Length"), tr("From")});
	m_sequences->setRootIsDecorated(false);
	m_sequences->setUniformRowHeights(true);
	m_sequences->setSortingEnabled(false);
	m_sequences->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_sequences->header()->setStretchLastSection(false);
	rv->addWidget(m_sequences, 3);
	auto* usesTitle = new QLabel(tr("Used in this map"));
	usesTitle->setProperty("role", "sectionTitle");
	rv->addWidget(usesTitle);
	m_uses = new QListWidget;
	m_uses->setObjectName("animationUses");
	m_uses->setToolTip(tr("Click: show that animation - double-click: select the entity in the map"));
	rv->addWidget(m_uses, 1);
	m_use = new QPushButton(tr("Use on selected entity"));
	m_use->setObjectName("animationUse");
	m_use->setProperty("accent", true);
	rv->addWidget(m_use);
	m_add = new QPushButton(tr("Add animation ..."));
	m_add->setObjectName("animationAdd");
	m_add->setToolTip(tr("Bring an animation (.smd from Blender's Source Tools or a decompile) onto this model's skeleton"));
	m_addSample = new QPushButton(tr("Use a sample ..."));
	m_addSample->setObjectName("animationSample");
	m_addSample->setEnabled(!samplesFolder().isEmpty());
	m_addSample->setToolTip(m_addSample->isEnabled() ? tr("Try it with Mapsmith's sample animations: a nod and a wave for Half-Life 2's people, a swing for "
	                                                      "the sample hanging sign (samples/animations)")
	                                                 : tr("No samples folder next to Mapsmith"));
	auto* addRow = new QHBoxLayout;
	addRow->setContentsMargins(0, 0, 0, 0);
	addRow->addWidget(m_add, 1);
	addRow->addWidget(m_addSample);
	rv->addLayout(addRow);
	// New animations wait here until they are built into the mod.
	m_pendingBar = new QFrame;
	m_pendingBar->setObjectName("animationPending");
	m_pendingBar->setFrameShape(QFrame::StyledPanel);
	auto* pv = new QVBoxLayout(m_pendingBar);
	pv->setContentsMargins(8, 6, 8, 6);
	m_pendingLabel = new QLabel;
	m_pendingLabel->setWordWrap(true);
	pv->addWidget(m_pendingLabel);
	m_compile = new QPushButton(tr("Build into the mod"));
	m_compile->setObjectName("animationCompile");
	m_compile->setProperty("accent", true);
	m_compile->setToolTip(tr("Makes models/mapsmith/... with the model and its new animations (studiomdl from the SDK)"));
	pv->addWidget(m_compile);
	m_progress = new QProgressBar;
	m_progress->setRange(0, 0);   // studiomdl tells no progress: a busy bar
	m_progress->setTextVisible(false);
	m_progress->setMaximumHeight(8);
	m_progress->hide();
	pv->addWidget(m_progress);
	m_log = new QPlainTextEdit;
	m_log->setObjectName("animationCompileLog");
	m_log->setReadOnly(true);   // selectable and copyable
	m_log->setMaximumHeight(140);
	m_log->hide();
	pv->addWidget(m_log);
	m_pendingBar->hide();
	rv->addWidget(m_pendingBar);
	rv->addWidget(hint(tr("Props play the animation as Default animation; a scripted_sequence gets it in the field you pick; "
	                      "an NPC gets a pose helper (a scripted_sequence that keeps it in the animation).")));
	split->addWidget(right);
	split->setStretchFactor(0, 2);
	split->setStretchFactor(1, 5);
	split->setStretchFactor(2, 3);
	split->setSizes({260, 640, 380});

	m_timer = new QTimer(this);
	m_timer->setInterval(16);
	connect(m_timer, &QTimer::timeout, this, &AnimationsPage::tick);
	m_mapRefresh = new QTimer(this);
	m_mapRefresh->setSingleShot(true);
	m_mapRefresh->setInterval(400);
	connect(m_mapRefresh, &QTimer::timeout, this, [this] {
		if (!isVisible()) return;   // showEvent catches up: no scan of the map while mapping elsewhere
		refreshModels();
		updateUses();
		updateUseButton();
	});

	connect(m_search, &QLineEdit::textChanged, this, &AnimationsPage::fillModelList);
	connect(m_models, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
		if (item && (item->flags() & Qt::ItemIsEnabled)) selectModel(item->data(Qt::UserRole).toString());
	});
	connect(m_sequenceSearch, &QLineEdit::textChanged, this, &AnimationsPage::showSequences);
	connect(m_sequences, &QTreeWidget::currentItemChanged, this, [this] {
		m_cycle = 0;
		m_clock.restart();
		applyPose();
		updateTransport();
		updateUses();
		updateUseButton();
	});
	connect(m_play, &QToolButton::clicked, this, [this] { setPlaying(!m_playing); });
	connect(m_scrub, &QSlider::valueChanged, this, [this](int value) {
		if (m_scrub->isSliderDown() || !m_playing) {
			m_cycle = float(value) / SCRUB_STEPS;
			applyPose();
			updateTransport();
		}
	});
	connect(m_scrub, &QSlider::sliderPressed, this, [this] { setPlaying(false); });
	connect(m_speed, &QComboBox::currentIndexChanged, this, [this] { m_speedFactor = float(m_speed->currentData().toDouble()); });
	connect(m_loop, &QCheckBox::toggled, this, [this](bool on) { m_looping = on; });
	connect(m_uses, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { selectSequence(item->data(Qt::UserRole + 1).toString()); });
	connect(m_uses, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { emit entityRequested(item->data(Qt::UserRole).toInt()); });
	connect(m_use, &QPushButton::clicked, this, [this] {
		const auto& selection = m_editor->selection();
		const Entity* entity = selection.entities.size() == 1 ? m_editor->doc().findEntity(selection.entities.front()) : nullptr;
		const auto keys = entity ? sequenceKeys(entity->classname()) : std::vector<SequenceKey>{};
		if (keys.size() > 1) {
			// A scripted_sequence: which of its fields.
			QMenu menu;
			for (const auto& key : keys) {
				menu.addAction(qs(key.label), this, [this, key = qs(key.key)] { useOnSelection(key); });
			}
			menu.exec(m_use->mapToGlobal(QPoint(0, m_use->height())));
			return;
		}
		useOnSelection();
	});
	connect(m_add, &QPushButton::clicked, this, [this] { askImport(); });
	connect(m_addSample, &QPushButton::clicked, this, [this] { askImport(samplesFolder(SAMPLE_ANIMATIONS)); });
	connect(m_compile, &QPushButton::clicked, this, &AnimationsPage::compileImports);

	connect(editor->models(), &ModelCache::modelReady, this, [this](const QString& path) {
		if (path.compare(m_model, Qt::CaseInsensitive) != 0) return;
		m_geometry = m_editor->models()->get(m_model);
		m_viewer->setModel(m_geometry);
		if (!m_geometry) m_viewer->setMessage(tr("Cannot load: %1").arg(m_editor->models()->error(m_model)));
		applyPose();
	});
	connect(&m_loader, &QFutureWatcherBase::finished, this, [this] {
		if (m_loader.property("generation").toInt() != m_generation) return;
		m_animated = m_loader.result();
		// New animations not built yet stay on their model.
		if (m_animated && originalModel().compare(m_importModel, Qt::CaseInsensitive) == 0) {
			for (const auto& pending : m_imports) {
				if (const auto smd = parseSmd(pending.smdText.toStdString())) addPreviewSequence(*m_animated, *smd, pending.options);
			}
		}
		showSequences();
		if (!m_pendingSequence.isEmpty()) {
			selectSequence(m_pendingSequence);
			m_pendingSequence.clear();
			if (m_pendingPlay) setPlaying(true);
			m_pendingPlay = false;
		}
		updateUses();
		emit modelLoaded();
	});
	connect(editor, &Editor::gameAboutToChange, this, [this] {
		++m_generation;
		m_loader.waitForFinished();   // it reads the old game's files
	});
	connect(editor, &Editor::gameChanged, this, [this] {
		m_model.clear();
		m_animated.reset();
		m_geometry.reset();
		m_viewer->setModel(nullptr);
		showSequences();
		m_mapRefresh->start();
	});
	connect(editor, &Editor::documentChanged, this, [this] { m_mapRefresh->start(); });
	connect(editor, &Editor::documentReplaced, this, [this] { m_mapRefresh->start(); });
	connect(editor, &Editor::selectionChanged, this, &AnimationsPage::updateUseButton);
	connect(assets, &AssetBrowser::materialsIndexed, this, &AnimationsPage::fillModelList);
	refreshModels();
	updateTransport();
	updateUseButton();
}

AnimationsPage::~AnimationsPage() {
	stopOwnProcesses(this);   // studiomdl may still run: its finish must not reach a page that is gone
	++m_generation;
	m_loader.waitForFinished();
}

void AnimationsPage::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	refreshModels();
	updateUses();
	updateUseButton();
	if (m_playing) {
		m_clock.restart();
		m_timer->start();
	}
}

void AnimationsPage::hideEvent(QHideEvent* event) {
	QWidget::hideEvent(event);
	m_timer->stop();   // nothing runs while the page is not seen
}

void AnimationsPage::refreshModels() {
	// The models the map's entities animate, most used first.
	std::map<QString, int> counts;
	const auto* catalog = m_editor->entities();
	for (const auto& entity : m_editor->doc().entities) {
		if (!entity.solids.empty()) continue;
		const bool animates = isNpcClass(entity.classname()) || !sequenceKeys(entity.classname()).empty() || startsWithI(entity.classname(), "prop_");
		if (!animates) continue;
		const auto model = animatedModel(m_editor->doc(), entity, catalog);
		if (!model.empty()) ++counts[qs(model)];
	}
	QStringList models;
	for (const auto& [model, count] : counts) models << model;
	std::stable_sort(models.begin(), models.end(), [&counts](const QString& a, const QString& b) { return counts[a] > counts[b]; });
	if (models != m_mapModels) {
		m_mapModels = models;
		fillModelList();
	}
}

void AnimationsPage::fillModelList() {
	const QStringList words = m_search->text().split(' ', Qt::SkipEmptyParts);
	const auto matches = [&words](const QString& path) {
		return std::all_of(words.begin(), words.end(), [&path](const QString& w) { return path.contains(w, Qt::CaseInsensitive); });
	};
	QSignalBlocker block(m_models);
	m_models->clear();
	QListWidgetItem* current = nullptr;
	const auto add = [&](const QString& path, const QString& text) {
		auto* item = new QListWidgetItem(text, m_models);
		item->setData(Qt::UserRole, path);
		item->setToolTip(path);
		if (path.compare(m_model, Qt::CaseInsensitive) == 0 && !current) current = item;
	};
	QStringList inMap;
	for (const auto& model : m_mapModels) {
		if (matches(model)) inMap << model;
	}
	if (!inMap.isEmpty()) {
		addHeader(m_models, tr("In this map"));
		for (const auto& model : inMap) add(model, model.section('/', -1));
	}
	QStringList all;
	if (m_assets) {
		int shown = 0;
		for (const auto& model : m_assets->modelNames()) {
			if (!matches(model)) continue;
			all << model;
			if (++shown >= 5000) break;   // the search narrows it down
		}
	}
	if (!all.isEmpty()) {
		addHeader(m_models, words.isEmpty() ? tr("All models") : tr("Models matching \"%1\"").arg(m_search->text()));
		for (const auto& model : all) add(model, model.mid(model.startsWith("models/") ? 7 : 0));
	}
	if (m_models->count() == 0) addHeader(m_models, tr("No model matches"));
	if (current) m_models->setCurrentItem(current);
}

void AnimationsPage::selectModel(const QString& path) {
	const QString model = QString::fromStdString(normalizePath(path.toStdString()));
	if (model.isEmpty() || model.compare(m_model, Qt::CaseInsensitive) == 0) return;
	m_model = model;
	m_title->setText(model);
	setPlaying(false);
	m_cycle = 0;
	m_animated.reset();
	showSequences();
	// The triangles from the shared model cache, the animations read in the background.
	m_geometry = m_editor->models() ? m_editor->models()->get(m_model) : nullptr;
	m_viewer->setModel(m_geometry);
	if (!m_geometry) m_viewer->setMessage(tr("Loading %1 ...").arg(model.section('/', -1)));
	loadAnimations();
	// Keep the list in step (e.g. when another page asked for the model).
	for (int i = 0; i < m_models->count(); ++i) {
		if (m_models->item(i)->data(Qt::UserRole).toString().compare(m_model, Qt::CaseInsensitive) == 0) {
			QSignalBlocker block(m_models);
			m_models->setCurrentRow(i);
			break;
		}
	}
	updateUses();
	updateUseButton();
}

void AnimationsPage::loadAnimations() {
	const auto* game = m_editor->game();
	++m_generation;
	if (!game || m_model.isEmpty()) return;
	const GameFileSystem* fs = &game->files;
	const std::string path = m_model.toStdString();
	m_loader.setProperty("generation", m_generation);
	m_loader.setFuture(QtConcurrent::run([fs, path] { return AnimatedModel::load(*fs, path); }));
}

void AnimationsPage::showSequences() {
	const QString previous = currentSequence();
	QSignalBlocker block(m_sequences);
	m_sequences->clear();
	if (!m_animated) {
		updateTransport();
		return;
	}
	const QStringList words = m_sequenceSearch->text().split(' ', Qt::SkipEmptyParts);
	const auto files = m_animated->files();
	QTreeWidgetItem* current = nullptr;
	const auto& sequences = m_animated->sequences();
	for (std::size_t i = 0; i < sequences.size(); ++i) {
		const auto& seq = sequences[i];
		const QString name = qs(seq.name), activity = qs(seq.activity);
		const bool match = std::all_of(words.begin(), words.end(), [&](const QString& w) {
			return name.contains(w, Qt::CaseInsensitive) || activity.contains(w, Qt::CaseInsensitive);
		});
		if (!match) continue;
		auto* item = new QTreeWidgetItem(m_sequences);
		item->setText(0, name);
		item->setText(1, activity);
		item->setText(2, seq.frames > 1 ? tr("%1 s").arg(double(seq.duration()), 0, 'f', 2) : tr("pose"));
		const QString from = seq.model >= 0 && seq.model < int(files.size()) ? qs(files[std::size_t(seq.model)]).section('/', -1) : QString();
		item->setText(3, seq.model == 0 ? tr("own") : seq.model < 0 ? tr("new - not built") : from);
		if (seq.model < 0) {
			for (int c = 0; c < 4; ++c) item->setForeground(c, QColor(240, 160, 40));
		}
		item->setToolTip(0, tr("%1 frames at %2 fps%3%4").arg(seq.frames).arg(double(seq.fps)).arg(seq.looping ? tr(", loops") : QString(),
		                                                                            seq.delta ? tr(", adds onto another pose") : QString()));
		item->setData(0, Qt::UserRole, int(i));
		if (name.compare(previous, Qt::CaseInsensitive) == 0) current = item;
	}
	if (!current && m_sequences->topLevelItemCount() > 0) {
		// Something to look at right away: the idle if there is one, else the first.
		for (int i = 0; i < m_sequences->topLevelItemCount() && !current; ++i) {
			if (m_sequences->topLevelItem(i)->text(1).compare("ACT_IDLE", Qt::CaseInsensitive) == 0) current = m_sequences->topLevelItem(i);
		}
		if (!current) current = m_sequences->topLevelItem(0);
	}
	if (current) m_sequences->setCurrentItem(current);
	m_sequences->resizeColumnToContents(1);
	m_sequences->resizeColumnToContents(2);
	applyPose();
	updateTransport();
	updateUseButton();
}

int AnimationsPage::sequenceIndex() const {
	const auto* item = m_sequences->currentItem();
	return item ? item->data(0, Qt::UserRole).toInt() : -1;
}

QString AnimationsPage::currentSequence() const {
	const auto* item = m_sequences->currentItem();
	return item ? item->text(0) : QString();
}

bool AnimationsPage::selectSequence(const QString& name) {
	if (!m_animated) {
		m_pendingSequence = name;
		return false;
	}
	if (!m_sequenceSearch->text().isEmpty()) m_sequenceSearch->clear();
	for (int i = 0; i < m_sequences->topLevelItemCount(); ++i) {
		if (m_sequences->topLevelItem(i)->text(0).compare(name, Qt::CaseInsensitive) == 0) {
			m_sequences->setCurrentItem(m_sequences->topLevelItem(i));
			m_sequences->scrollToItem(m_sequences->currentItem());
			return true;
		}
	}
	return false;
}

void AnimationsPage::showFor(const QString& model, const QString& sequence) {
	if (!model.isEmpty() && model.compare(m_model, Qt::CaseInsensitive) != 0) {
		selectModel(model);
	}
	if (sequence.isEmpty()) return;
	if (m_animated) {
		if (selectSequence(sequence)) setPlaying(true);
		else emit m_editor->message(tr("%1 has no animation \"%2\"").arg(m_model.section('/', -1), sequence));
	} else {
		m_pendingSequence = sequence;
		m_pendingPlay = true;
	}
}

void AnimationsPage::setPlaying(bool on) {
	m_playing = on && m_animated && sequenceIndex() >= 0;
	m_play->setText(m_playing ? QStringLiteral("❚❚") : QStringLiteral("▶"));
	if (m_playing) {
		const auto& seq = m_animated->sequences()[std::size_t(sequenceIndex())];
		if (!seq.looping && !m_looping && m_cycle >= 1.f) m_cycle = 0;   // play again from the start
		m_clock.restart();
		if (isVisible()) m_timer->start();
	} else {
		m_timer->stop();
	}
}

void AnimationsPage::setCycle(float cycle) {
	m_cycle = std::clamp(cycle, 0.f, 1.f);
	applyPose();
	updateTransport();
}

void AnimationsPage::setSpeed(float speed) {
	m_speedFactor = speed;
	for (int i = 0; i < m_speed->count(); ++i) {
		if (std::abs(m_speed->itemData(i).toDouble() - double(speed)) < 1e-3) m_speed->setCurrentIndex(i);
	}
}

void AnimationsPage::setLooping(bool on) {
	m_loop->setChecked(on);
	m_looping = on;
}

void AnimationsPage::tick() {
	const int index = sequenceIndex();
	if (!m_animated || index < 0) {
		setPlaying(false);
		return;
	}
	const float seconds = float(m_clock.restart()) / 1000.f;
	const float duration = std::max(m_animated->sequences()[std::size_t(index)].duration(), 1.f / 30.f);
	m_cycle += seconds * m_speedFactor / duration;
	if (m_cycle >= 1.f) {
		if (m_looping) {
			m_cycle = std::fmod(m_cycle, 1.f);
		} else {
			m_cycle = 1.f;
			setPlaying(false);
		}
	}
	applyPose();
	updateTransport();
}

void AnimationsPage::applyPose() {
	if (!m_geometry || !m_animated) return;
	if (m_animated->bones().empty() || m_geometry->weights.size() != m_geometry->vertices.size()) return;
	std::vector<ModelVertex> vertices;
	skinVertices(*m_geometry, m_animated->skinning(sequenceIndex(), m_cycle), vertices);
	m_viewer->setPose(std::move(vertices));
}

void AnimationsPage::updateTransport() {
	const int index = sequenceIndex();
	const bool have = m_animated && index >= 0;
	m_play->setEnabled(have);
	m_scrub->setEnabled(have);
	if (!m_scrub->isSliderDown()) {
		QSignalBlocker block(m_scrub);
		m_scrub->setValue(int(std::lround(m_cycle * SCRUB_STEPS)));
	}
	if (!have) {
		m_time->setText(m_model.isEmpty() ? QString() : (m_animated ? tr("no animations") : tr("reading ...")));
		return;
	}
	const auto& seq = m_animated->sequences()[std::size_t(index)];
	const int frame = int(std::lround(m_cycle * float(std::max(0, seq.frames - 1))));
	m_time->setText(tr("frame %1 / %2 · %3 s").arg(frame).arg(std::max(0, seq.frames - 1)).arg(double(m_cycle * seq.duration()), 0, 'f', 2));
}

void AnimationsPage::updateUses() {
	m_uses->clear();
	if (m_model.isEmpty()) return;
	const auto uses = sequenceUses(m_editor->doc(), m_editor->entities(), m_model.toStdString());
	const QString current = currentSequence();
	for (const auto& use : uses) {
		const Entity* entity = m_editor->doc().findEntity(use.entityId);
		const Entity* actor = m_editor->doc().findEntity(use.actorId);
		if (!entity) continue;
		const auto name = [](const Entity* e) {
			const auto target = e->get("targetname");
			return target.empty() ? qs(e->classname()) : qs(target);
		};
		QString text = tr("%1 - %2: %3").arg(actor && actor != entity ? name(actor) + QStringLiteral(" (") + name(entity) + QStringLiteral(")") : name(entity),
		                                      qs(use.label), qs(use.sequence));
		auto* item = new QListWidgetItem(text, m_uses);
		item->setData(Qt::UserRole, use.entityId);
		item->setData(Qt::UserRole + 1, qs(use.sequence));
		if (qs(use.sequence).compare(current, Qt::CaseInsensitive) == 0) {
			QFont font = item->font();
			font.setBold(true);
			item->setFont(font);
		}
	}
	if (uses.empty()) {
		auto* item = new QListWidgetItem(tr("No entity of this map plays this model's animations"), m_uses);
		item->setFlags(Qt::NoItemFlags);
	}
}

void AnimationsPage::updateUseButton() {
	const auto& selection = m_editor->selection();
	const Entity* entity = selection.entities.size() == 1 ? m_editor->doc().findEntity(selection.entities.front()) : nullptr;
	const bool takes = entity && (!sequenceKeys(entity->classname()).empty() || isNpcClass(entity->classname()));
	const int index = sequenceIndex();
	const bool preview = m_animated && index >= 0 && m_animated->sequences()[std::size_t(index)].model < 0;
	m_use->setEnabled(takes && !currentSequence().isEmpty() && !preview);
	if (preview) {
		m_use->setToolTip(tr("Build the new animation into the mod first"));
	} else if (!entity) {
		m_use->setToolTip(tr("Select one prop, NPC or scripted_sequence in the map first"));
	} else if (!takes) {
		m_use->setToolTip(tr("%1 does not play animations").arg(qs(entity->classname())));
	} else {
		const auto model = animatedModel(m_editor->doc(), *entity, m_editor->entities());
		const QString other = !model.empty() && qs(model).compare(m_model, Qt::CaseInsensitive) != 0
		                          ? tr("\nNote: it shows %1, not this model").arg(qs(model).section('/', -1))
		                          : QString();
		m_use->setToolTip(tr("Play \"%1\" on %2%3").arg(currentSequence(), qs(entity->classname()), other));
	}
}

bool AnimationsPage::useOnSelection(const QString& key) {
	const auto& selection = m_editor->selection();
	const QString sequence = currentSequence();
	if (selection.entities.size() != 1 || sequence.isEmpty()) {
		emit m_editor->message(tr("Select one prop, NPC or scripted_sequence and an animation first"));
		return false;
	}
	const int id = selection.entities.front();
	const int index = sequenceIndex();
	if (m_animated && index >= 0 && m_animated->sequences()[std::size_t(index)].model < 0) {
		emit m_editor->message(tr("Build the new animation into the mod first"));
		return false;
	}
	// Animations only Mapsmith's model has: the entity switches to that model as well.
	std::string model;
	if (m_model.startsWith(QLatin1String("models/mapsmith/"), Qt::CaseInsensitive)) {
		if (const Entity* entity = m_editor->doc().findEntity(id)) {
			if (qs(animatedModel(m_editor->doc(), *entity, m_editor->entities())).compare(m_model, Qt::CaseInsensitive) != 0) model = m_model.toStdString();
		}
	}
	Change change = edit::useSequence(m_editor->doc(), id, sequence.toStdString(), key.toStdString(), model);
	if (change.empty()) {
		emit m_editor->message(tr("This entity cannot play animations"));
		return false;
	}
	m_editor->commit(std::move(change));
	emit m_editor->message(tr("The selected entity now plays \"%1\"").arg(sequence));
	updateUses();
	return true;
}

QString AnimationsPage::originalModel() const {
	if (m_model.startsWith(QLatin1String("models/mapsmith/"), Qt::CaseInsensitive)) return QStringLiteral("models/") + m_model.mid(16);
	return m_model;
}

void AnimationsPage::askImport(const QString& startFolder) {
	if (m_model.isEmpty() || !m_animated) {
		emit m_editor->message(tr("Pick a model first - the animation goes onto its skeleton"));
		return;
	}
	if (m_animated->bones().empty()) {
		emit m_editor->message(tr("%1 has no skeleton to animate").arg(m_model.section('/', -1)));
		return;
	}
	const QString path = QFileDialog::getOpenFileName(this, tr("Add animation to %1").arg(m_model.section('/', -1)), startFolder,
	                                                  tr("Source animations (*.smd)"));
	if (path.isEmpty()) return;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		emit m_editor->message(tr("Cannot read %1").arg(path));
		return;
	}
	std::string error;
	const auto smd = parseSmd(file.readAll().toStdString(), &error);
	if (!smd) {
		emit m_editor->message(tr("%1 is not a usable animation: %2").arg(QFileInfo(path).fileName(), qs(error)));
		return;
	}
	const SkeletonMatch match = matchSkeleton(*smd, m_animated->bones());
	// The options: what the new animation is called and how it plays.
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Add animation"));
	auto* form = new QFormLayout(&dialog);
	QString about = tr("%1 frames · %2 of %3 bones match %4").arg(smd->frames.size()).arg(match.matched()).arg(smd->nodes.size()).arg(m_model.section('/', -1));
	if (smd->relative) about += tr("\nMoves only its own bones, on top of the model's rest pose - the others keep it.");
	if (!match.notInModel.empty()) {
		QStringList names;
		for (const auto& n : match.notInModel) names << qs(n);
		about += tr("\nNot in the model (ignored): %1").arg(names.mid(0, 8).join(", ") + (names.size() > 8 ? QStringLiteral(" ...") : QString()));
	}
	auto* info = new QLabel(about);
	info->setWordWrap(true);
	form->addRow(info);
	auto* name = new QLineEdit(qs(sequenceNameFromFile(path.toStdString())));
	form->addRow(tr("Name"), name);
	auto* fps = new QDoubleSpinBox;
	fps->setRange(1, 240);
	fps->setValue(30);
	fps->setSuffix(tr(" fps"));
	form->addRow(tr("Speed"), fps);
	auto* loop = new QCheckBox(tr("Loops (idles, walks)"));
	form->addRow(QString(), loop);
	auto* activity = new QComboBox;
	activity->setEditable(true);
	activity->addItems({QString(), "ACT_IDLE", "ACT_WALK", "ACT_RUN", "ACT_SIGNAL1", "ACT_COWER", "ACT_BUSY_LEAN_BACK", "ACT_BUSY_SIT_CHAIR"});
	activity->setToolTip(tr("Optional: an activity lets the AI pick the animation by itself"));
	form->addRow(tr("Activity"), activity);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Add and preview"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (match.matched() == 0) {
		buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
		info->setText(info->text() + tr("\nNone of its bones is in this model - it was made for another skeleton."));
	}
	if (dialog.exec() != QDialog::Accepted) return;
	ImportedAnimation options;
	options.name = sequenceNameFromFile(name->text().toStdString());
	options.fps = float(fps->value());
	options.loop = loop->isChecked();
	options.activity = activity->currentText().trimmed().toStdString();
	QString importError;
	if (!importAnimation(path, options, &importError)) emit m_editor->message(importError);
}

bool AnimationsPage::importAnimation(const QString& smdPath, ImportedAnimation options, QString* error) {
	const auto fail = [&](const QString& message) {
		if (error) *error = message;
		return false;
	};
	if (!m_animated || m_animated->bones().empty()) return fail(tr("Pick a model with a skeleton first"));
	QFile file(smdPath);
	if (!file.open(QIODevice::ReadOnly)) return fail(tr("Cannot read %1").arg(smdPath));
	QString text = QString::fromUtf8(file.readAll());
	std::string parseError;
	auto smd = parseSmd(text.toStdString(), &parseError);
	if (!smd) return fail(tr("%1 is not a usable animation: %2").arg(QFileInfo(smdPath).fileName(), qs(parseError)));
	if (matchSkeleton(*smd, m_animated->bones()).matched() == 0) return fail(tr("None of the animation's bones is in %1").arg(m_model.section('/', -1)));
	// A file that moves only some bones (or holds changes to the rest pose, like the samples)
	// becomes a whole animation of this model: what studiomdl builds is what the preview shows.
	if (animationNeedsCompleting(*smd, m_animated->bones())) {
		text = qs(completedAnimationSmd(*smd, m_animated->bones()));
		smd = parseSmd(text.toStdString(), &parseError);
		if (!smd) return fail(tr("%1 could not be completed for %2: %3").arg(QFileInfo(smdPath).fileName(), m_model.section('/', -1), qs(parseError)));
	}
	if (options.name.empty()) options.name = sequenceNameFromFile(smdPath.toStdString());
	options.smdFile = options.name + ".smd";
	if (originalModel().compare(m_importModel, Qt::CaseInsensitive) != 0) {
		m_imports.clear();   // they belonged to another model
		m_importModel = originalModel();
	}
	std::erase_if(m_imports, [&options](const PendingImport& p) { return iequals(p.options.name, options.name); });
	m_imports.push_back({options, text});
	addPreviewSequence(*m_animated, *smd, options);
	showSequences();
	selectSequence(qs(options.name));
	setPlaying(true);
	updatePendingBar();
	emit m_editor->message(tr("\"%1\" added to %2 - it plays now; Build into the mod makes it part of the game").arg(qs(options.name), m_model.section('/', -1)));
	return true;
}

void AnimationsPage::updatePendingBar() {
	const bool busy = m_studiomdl && m_studiomdl->state() != QProcess::NotRunning;
	m_pendingBar->setVisible(!m_imports.empty() || busy || !m_log->toPlainText().isEmpty());
	QStringList names;
	for (const auto& p : m_imports) names << qs(p.options.name);
	if (busy) {
		m_pendingLabel->setText(tr("Building %1 with studiomdl ...").arg(qs(animationModelPath(m_importModel.toStdString()))));
	} else if (!m_imports.empty()) {
		m_pendingLabel->setText(tr("New, not built yet: %1.\nBuilding makes %2: the model with all its animations and the new ones - "
		                           "entities switch to it when you use a new animation on them.")
		                            .arg(names.join(", "), qs(animationModelPath(m_importModel.toStdString()))));
	}
	m_compile->setVisible(!m_imports.empty());
	m_compile->setEnabled(!busy);
	m_progress->setVisible(busy);
	m_log->setVisible(!m_log->toPlainText().isEmpty());
}

QString AnimationsPage::studiomdlProgram() const {
	if (!m_studiomdlOverride.isEmpty()) return m_studiomdlOverride;
	const QString vbsp = m_editor->gameSettings().vbsp;
	if (vbsp.isEmpty()) return {};
	const QFileInfo info(vbsp);
	return info.dir().filePath(info.suffix().isEmpty() ? QStringLiteral("studiomdl") : QStringLiteral("studiomdl.exe"));
}

QString AnimationsPage::compileLog() const {
	return m_log->toPlainText();
}

bool AnimationsPage::compileImports() {
	const auto fail = [this](const QString& message) {
		m_log->setPlainText(message);
		updatePendingBar();
		emit m_editor->message(message);
		return false;
	};
	if (m_imports.empty()) return fail(tr("No new animation to build"));
	if (m_studiomdl && m_studiomdl->state() != QProcess::NotRunning) return false;
	const auto* entry = m_editor->gameEntry();
	const auto* game = m_editor->game();
	if (!entry || !game || !m_animated || !m_geometry) return fail(tr("The model is not loaded yet"));
	const QString program = studiomdlProgram();
	if (program.isEmpty() || !QFileInfo::exists(program)) {
		return fail(tr("studiomdl was not found (%1). It comes with the game's SDK tools, next to vbsp.exe - check the game's tools in Compile settings.")
		                .arg(program.isEmpty() ? tr("no SDK tools set") : program));
	}
	const QString original = m_importModel;
	const QString gameDir = fromPath(entry->gameDir);   // (generic_string() is the ANSI code page on Windows: a folder with umlauts broke)
	const QString folder = QDir(gameDir).filePath(qs(animationSourceFolder(original.toStdString())));
	if (!QDir().mkpath(folder)) return fail(tr("Cannot create %1").arg(folder));
	const auto write = [](const QString& path, const QString& text) {
		QFile file(path);
		return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text.toUtf8()) == text.toUtf8().size();
	};
	// Every animation added so far (the list next to the sources) and the new ones.
	std::vector<ImportedAnimation> list;
	if (QFile listFile(QDir(folder).filePath("animations.txt")); listFile.open(QIODevice::ReadOnly)) {
		list = readAnimationList(listFile.readAll().toStdString());
	}
	for (const auto& pending : m_imports) {
		std::erase_if(list, [&pending](const ImportedAnimation& a) { return iequals(a.name, pending.options.name); });
		list.push_back(pending.options);
		if (!write(QDir(folder).filePath(qs(pending.options.smdFile)), pending.smdText)) return fail(tr("Cannot write into %1").arg(folder));
	}
	// The original's triangles, skeleton, attachments, surface and keyvalues.
	const auto mdl = game->files.read(original.toStdString());
	ModelInfo info;
	std::vector<ModelAttachment> attachments;
	if (mdl) {
		if (auto read = readModelInfo(*mdl)) info = std::move(*read);
		attachments = readAttachments(*mdl);
	}
	const QString qc = qs(animationModelQc(original.toStdString(), *m_geometry, info, attachments, list));
	if (!write(QDir(folder).filePath("reference.smd"), qs(referenceSmd(*m_geometry, m_animated->bones()))) ||
	    !write(QDir(folder).filePath("animations.txt"), qs(writeAnimationList(list))) || !write(QDir(folder).filePath("model.qc"), qc)) {
		return fail(tr("Cannot write into %1").arg(folder));
	}
	// studiomdl writes models/mapsmith/... into the game folder.
	m_log->clear();
	auto* process = new QProcess(this);
	m_studiomdl = process;
	process->setProcessChannelMode(QProcess::MergedChannels);
	process->setWorkingDirectory(folder);
#ifdef Q_OS_WIN
	process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
#endif
	connect(process, &QProcess::readyRead, this, [this, process] {
		m_log->appendPlainText(QString::fromLocal8Bit(process->readAll()).remove('\r').trimmed());
	});
	connect(process, &QProcess::finished, this, [this, process, gameDir, original](int code, QProcess::ExitStatus status) {
		process->deleteLater();
		const QString built = qs(animationModelPath(original.toStdString()));
		const bool ok = status == QProcess::NormalExit && code == 0 && QFileInfo::exists(QDir(gameDir).filePath(built));
		if (ok) {
			// The game's file list and the model cache see the new files at once.
			if (auto* game = m_editor->game()) {
				for (const char* extension : {".mdl", ".vvd", ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".phy", ".ani"}) {
					const QString file = QDir(gameDir).filePath(built.chopped(4) + extension);
					if (QFileInfo::exists(file)) game->files.noteWrittenFile(toPath(file));
				}
			}
			if (m_editor->models()) m_editor->models()->forget(built);
			m_imports.clear();
			m_log->appendPlainText(tr("Built %1.").arg(built));
			emit m_editor->message(tr("Built %1 - its new animations are in the list; Use on selected entity switches an entity to it").arg(built));
			m_model.clear();   // shown again from the new files
			selectModel(built);
		} else {
			m_log->appendPlainText(tr("studiomdl failed (exit code %1) - the lines above say why.").arg(code));
			emit m_editor->message(tr("Building the animations failed - see the log on the Animations page"));
		}
		updatePendingBar();
		emit compileFinished(ok);
	});
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart) return;
		m_log->appendPlainText(tr("Could not start studiomdl: %1").arg(process->errorString()));
		process->deleteLater();
		updatePendingBar();
		emit compileFinished(false);
	});
	process->start(program, {"-game", gameDir, "-nop4", "-verbose", QDir(folder).filePath("model.qc")});
	updatePendingBar();
	return true;
}

} // namespace sme::app
