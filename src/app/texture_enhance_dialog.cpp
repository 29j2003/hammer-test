// texture_enhance_dialog.cpp - "Enhance textures ..." (see texture_enhance_dialog.h).

#include "texture_enhance_dialog.h"

#include "ai_upscaler.h"
#include "dialog_fit.h"
#include "editor.h"
#include "enhancements.h"
#include "process_guard.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QThreadPool>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <mutex>

#include <sme/filesystem.h>
#include <sme/material_decisions.h>

namespace sme::app {

namespace {

QString sizeText(unsigned w, unsigned h) { return QStringLiteral("%1 x %2").arg(w).arg(h); }

QString bytesText(std::uint64_t bytes) {
	if (bytes >= 1024ull * 1024 * 1024) return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024 * 1024), 0, 'f', 1);
	if (bytes >= 1024ull * 1024) return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024), 0, 'f', 0);
	return QStringLiteral("%1 KB").arg((bytes + 1023) / 1024);
}

constexpr int MATERIAL_ROLE = Qt::UserRole;

} // namespace

// --- The comparison --------------------------------------------------------------------------

EnhanceCompareView::EnhanceCompareView(QWidget* parent) : QWidget(parent) {
	setObjectName("enhanceCompare");
	setMinimumSize(220, 180);
	setCursor(Qt::SplitHCursor);
	setToolTip(tr("Left: the texture as the game shows it now, only larger. Right: enhanced. Drag to move the line."));
}

void EnhanceCompareView::setPictures(const QImage& before, const QImage& after, const QString& afterLabel) {
	m_before = before;
	m_after = after;
	m_afterLabel = afterLabel;
	m_text.clear();
	update();
}

void EnhanceCompareView::clear(const QString& text) {
	m_before = m_after = {};
	m_text = text;
	update();
}

void EnhanceCompareView::setSplit(double split) {
	m_split = std::clamp(split, 0.0, 1.0);
	update();
}

void EnhanceCompareView::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.fillRect(rect(), palette().color(QPalette::Base));
	if (m_before.isNull() || m_after.isNull()) {
		p.setPen(palette().color(QPalette::PlaceholderText));
		p.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, m_text);
		return;
	}
	// Both at the same place and scale (pixels of the enhanced texture 1:1 where they fit).
	const QSize size = m_after.size().scaled(this->size(), Qt::KeepAspectRatio).boundedTo(m_after.size() * 2);
	const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
	const int x = target.left() + int(std::lround(m_split * target.width()));
	p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	p.save();
	p.setClipRect(QRect(target.left(), target.top(), x - target.left(), target.height()));
	p.drawImage(target, m_before);
	p.restore();
	p.save();
	p.setClipRect(QRect(x, target.top(), target.right() - x + 1, target.height()));
	p.drawImage(target, m_after);
	p.restore();
	p.setPen(QPen(QColor(255, 210, 60), 2));
	p.drawLine(x, target.top(), x, target.bottom());
	const auto tag = [&p](const QPoint& at, const QString& text, bool right) {
		const QFontMetrics fm(p.font());
		QRect box(at, QSize(fm.horizontalAdvance(text) + 10, fm.height() + 4));
		if (right) box.moveRight(at.x());
		p.fillRect(box, QColor(0, 0, 0, 160));
		p.setPen(Qt::white);
		p.drawText(box, Qt::AlignCenter, text);
	};
	tag(target.topLeft() + QPoint(4, 4), tr("Before"), false);
	tag(target.topRight() + QPoint(-4, 4), m_afterLabel, true);
}

void EnhanceCompareView::mousePressEvent(QMouseEvent* event) { mouseMoveEvent(event); }

void EnhanceCompareView::mouseMoveEvent(QMouseEvent* event) {
	if (!(event->buttons() & Qt::LeftButton) || m_after.isNull()) return;
	const QSize size = m_after.size().scaled(this->size(), Qt::KeepAspectRatio).boundedTo(m_after.size() * 2);
	const int left = (width() - size.width()) / 2;
	setSplit(double(event->position().x() - left) / std::max(1, size.width()));
}

// --- The dialog ------------------------------------------------------------------------------

struct TextureEnhanceDialog::RunResult {
	QStringList written;
	QStringList removed;
	QStringList problems;
	int ai = 0;
	bool cancelled = false;
	bool removing = false;
};

TextureEnhanceDialog::TextureEnhanceDialog(Editor* editor, const QStringList& materials, QWidget* parent)
    : QDialog(parent), m_editor(editor), m_ai(makeAiUpscaler()) {
	setObjectName("textureEnhanceDialog");
	setWindowTitle(tr("Enhance textures"));
	auto* layout = new QVBoxLayout(this);
	auto* intro = new QLabel(tr("Makes the textures sharper and larger and gives materials without one a bump map. The new files go into "
	                            "the mod and cover the game's own; the game's files stay as they are. On walls the textures keep their size."));
	intro->setWordWrap(true);
	layout->addWidget(intro);

	auto* split = new QSplitter(Qt::Horizontal);
	m_list = new QTreeWidget;
	m_list->setObjectName("enhanceList");
	m_list->setRootIsDecorated(false);
	m_list->setUniformRowHeights(true);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setHeaderLabels({tr("Material"), tr("Now"), tr("After"), tr("Bump map"), tr("Note")});
	m_list->header()->setStretchLastSection(true);
	m_list->setMinimumWidth(320);
	split->addWidget(m_list);
	m_compare = new EnhanceCompareView;
	m_compare->clear(tr("Choose a material to compare"));
	split->addWidget(m_compare);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 2);
	layout->addWidget(split, 1);
	m_summary = new QLabel;
	m_summary->setObjectName("enhanceSummary");
	m_summary->setWordWrap(true);
	layout->addWidget(m_summary);

	auto* form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	QSettings settings;
	m_factor = new QComboBox;
	m_factor->setObjectName("enhanceFactor");
	m_factor->addItem(tr("Off (only bump maps)"), 1);
	m_factor->addItem(tr("2x"), 2);
	m_factor->addItem(tr("4x"), 4);
	m_factor->setCurrentIndex(std::max(0, m_factor->findData(settings.value("enhance/factor", 2).toInt())));
	m_method = new QComboBox;
	m_method->setObjectName("enhanceMethod");
	m_method->addItem(tr("AI (Real-ESRGAN, needs a Vulkan graphics card)"), int(UpscaleMethod::Ai));
	m_method->addItem(tr("Sharp (built in, fast)"), int(UpscaleMethod::Sharp));
	if (!m_ai) {
		m_method->setCurrentIndex(1);
		m_method->setItemData(0, false, Qt::UserRole - 1);   // disabled entry
		m_method->setToolTip(tr("The AI upscaler is not next to Mapsmith (tools/realesrgan): the built-in scaling is used."));
	} else {
		m_method->setCurrentIndex(settings.value("enhance/method", int(UpscaleMethod::Ai)).toInt() == int(UpscaleMethod::Sharp) ? 1 : 0);
	}
	auto* upscaleRow = new QHBoxLayout;
	upscaleRow->addWidget(m_factor);
	upscaleRow->addWidget(m_method, 1);
	form->addRow(tr("Upscale:"), upscaleRow);
	m_maxSize = new QComboBox;
	m_maxSize->setObjectName("enhanceMaxSize");
	for (int size : {1024, 2048, 4096}) m_maxSize->addItem(QString::number(size), size);
	m_maxSize->setCurrentIndex(std::max(0, m_maxSize->findData(settings.value("enhance/maxSize", 2048).toInt())));
	m_maxSize->setToolTip(tr("The longer side at most. Larger textures need more video memory in the game."));
	form->addRow(tr("Max size:"), m_maxSize);
	m_bumps = new QCheckBox(tr("Bump maps for materials without one"));
	m_bumps->setObjectName("enhanceBumps");
	m_bumps->setChecked(settings.value("enhance/bumps", true).toBool());
	m_bigBumps = new QCheckBox(tr("Their own bump maps larger too"));
	m_bigBumps->setObjectName("enhanceBigBumps");
	m_bigBumps->setChecked(settings.value("enhance/bigBumps", false).toBool());
	m_masks = new QCheckBox(tr("Reflection masks (shiny only where bright)"));
	m_masks->setObjectName("enhanceMasks");
	m_masks->setChecked(settings.value("enhance/masks", false).toBool());
	m_masks->setToolTip(tr("For materials with a reflection ($envmap) but no mask: dark parts reflect less."));
	m_compress = new QCheckBox(tr("Compress (DXT, 4-8 times smaller)"));
	m_compress->setObjectName("enhanceCompress");
	m_compress->setChecked(settings.value("enhance/compress", true).toBool());
	auto* checks = new QGridLayout;
	checks->addWidget(m_bumps, 0, 0);
	checks->addWidget(m_bigBumps, 0, 1);
	checks->addWidget(m_masks, 1, 0);
	checks->addWidget(m_compress, 1, 1);
	form->addRow(tr("Add:"), checks);
	layout->addLayout(form);
	m_note = new QLabel(tr("New bump maps on walls show after the maps are compiled again (VRAD bakes the light for bump maps)."));
	m_note->setObjectName("enhanceNote");
	m_note->setWordWrap(true);
	m_note->setEnabled(false);
	layout->addWidget(m_note);

	auto* bottom = new QHBoxLayout;
	m_progress = new QProgressBar;
	m_progress->setVisible(false);
	m_status = new QLabel;
	m_status->setObjectName("enhanceStatus");
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	bottom->addWidget(m_progress, 1);
	bottom->addWidget(m_status, 2);
	m_remove = new QPushButton(tr("Remove enhancements"));
	m_remove->setObjectName("enhanceRemove");
	m_remove->setToolTip(tr("Takes out what Enhance textures wrote for these materials: the game's textures again."));
	m_cancel = new QPushButton(tr("Cancel"));
	m_cancel->setObjectName("enhanceCancel");
	m_cancel->setVisible(false);
	m_apply = new QPushButton(tr("Apply"));
	m_apply->setObjectName("enhanceApply");
	m_apply->setDefault(true);
	auto* closeButton = new QPushButton(tr("Close"));
	bottom->addWidget(m_remove);
	bottom->addWidget(m_cancel);
	bottom->addWidget(m_apply);
	bottom->addWidget(closeButton);
	layout->addLayout(bottom);

	m_replanSoon.setSingleShot(true);
	m_replanSoon.setInterval(150);
	connect(&m_replanSoon, &QTimer::timeout, this, &TextureEnhanceDialog::replan);
	const auto changed = [this] {
		QSettings s;
		s.setValue("enhance/factor", m_factor->currentData().toInt());
		if (m_ai) s.setValue("enhance/method", m_method->currentData().toInt());
		s.setValue("enhance/maxSize", m_maxSize->currentData().toInt());
		s.setValue("enhance/bumps", m_bumps->isChecked());
		s.setValue("enhance/bigBumps", m_bigBumps->isChecked());
		s.setValue("enhance/masks", m_masks->isChecked());
		s.setValue("enhance/compress", m_compress->isChecked());
		m_replanSoon.start();
	};
	for (auto* box : {m_factor, m_method, m_maxSize}) connect(box, &QComboBox::currentIndexChanged, this, changed);
	for (auto* box : {m_bumps, m_bigBumps, m_masks, m_compress}) connect(box, &QCheckBox::toggled, this, changed);
	connect(m_list, &QTreeWidget::currentItemChanged, this, [this] { showPreview(); });
	connect(&m_planWatcher, &QFutureWatcher<std::vector<MaterialEnhancement>>::finished, this, &TextureEnhanceDialog::planned);
	connect(&m_previewWatcher, &QFutureWatcher<std::optional<EnhancePreview>>::finished, this, &TextureEnhanceDialog::previewReady);
	connect(&m_runWatcher, &QFutureWatcher<std::shared_ptr<RunResult>>::finished, this, &TextureEnhanceDialog::ran);
	connect(m_apply, &QPushButton::clicked, this, &TextureEnhanceDialog::apply);
	connect(m_remove, &QPushButton::clicked, this, [this] {
		if (QMessageBox::question(this, tr("Remove enhancements"),
		                          tr("Take out the enhanced textures, bump maps and masks of these materials? The game's own come back.")) != QMessageBox::Yes) {
			return;
		}
		removeAll();
	});
	connect(m_cancel, &QPushButton::clicked, this, [this] { m_stop->store(true); });
	connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
	m_tick.setInterval(200);
	connect(&m_tick, &QTimer::timeout, this, [this] {
		m_progress->setValue(m_done->load());
		m_status->setText(tr("%1 of %2 ...").arg(m_done->load()).arg(m_total));
	});
	// The game's files go away (another game): stop reading them first.
	connect(m_editor, &Editor::gameAboutToChange, this, [this] {
		stopAll();
		close();
	});
	resize(980, 680);
	fitDialogToScreen(this);
	setMaterials(materials);
}

TextureEnhanceDialog::~TextureEnhanceDialog() {
	stopAll();
	stopOwnProcesses(this);
}

void TextureEnhanceDialog::stopAll() {
	m_stop->store(true);
	m_previewStop->store(true);
	m_planWatcher.waitForFinished();
	m_previewWatcher.waitForFinished();
	m_runWatcher.waitForFinished();
}

bool TextureEnhanceDialog::busy() const { return m_planWatcher.isRunning() || m_previewWatcher.isRunning() || m_runWatcher.isRunning() || m_running; }

EnhanceOptions TextureEnhanceDialog::options() const {
	EnhanceOptions o;
	o.factor = unsigned(m_factor->currentData().toInt());
	o.method = UpscaleMethod(m_method->currentData().toInt());
	if (!m_ai) o.method = UpscaleMethod::Sharp;
	o.maxSize = unsigned(m_maxSize->currentData().toInt());
	o.bumpMaps = m_bumps->isChecked();
	o.upscaleBumpMaps = m_bigBumps->isChecked();
	o.glossMasks = m_masks->isChecked();
	o.compress = m_compress->isChecked();
	return o;
}

void TextureEnhanceDialog::setMaterials(const QStringList& materials) {
	m_materials = materials;
	m_materials.removeDuplicates();
	replan();
}

void TextureEnhanceDialog::replan() {
	const LoadedGame* game = m_editor->game();
	const GameEntry* entry = m_editor->gameEntry();
	if (!game || !entry) {
		m_summary->setText(tr("Choose a game first."));
		m_apply->setEnabled(false);
		return;
	}
	if (m_running) return;
	if (m_planWatcher.isRunning()) {
		m_replanSoon.start();   // after this one
		return;
	}
	++m_planGeneration;
	m_summary->setText(tr("Looking at %n material(s) ...", nullptr, int(m_materials.size())));
	m_apply->setEnabled(false);
	std::vector<std::string> names;
	names.reserve(std::size_t(m_materials.size()));
	for (const auto& name : m_materials) names.push_back(name.toLower().toStdString());
	const GameFileSystem* fs = &game->files;
	m_planWatcher.setFuture(QtConcurrent::run([fs, modDir = entry->gameDir, names = std::move(names), options = options(), stop = m_stop] {
		const TextureManifest manifest = loadTextureManifest(modDir);
		const MaterialDecisions decisions = loadMaterialDecisions(modDir);
		std::vector<MaterialEnhancement> plans;
		plans.reserve(names.size());
		for (const auto& name : names) {
			if (stop->load()) break;
			plans.push_back(planEnhancement(*fs, modDir, name, options, &manifest, &decisions));
		}
		return plans;
	}));
}

void TextureEnhanceDialog::planned() {
	if (m_stop->load() && !m_running) m_stop->store(false);
	m_plans = m_planWatcher.result();
	fillList();
}

void TextureEnhanceDialog::fillList() {
	const QString current = m_list->currentItem() ? m_list->currentItem()->data(0, MATERIAL_ROLE).toString() : QString();
	m_list->setUpdatesEnabled(false);
	m_list->clear();
	int doing = 0, worldBumps = 0;
	std::uint64_t bytes = 0;
	QTreeWidgetItem* select = nullptr;
	QList<QTreeWidgetItem*> items;
	for (const auto& plan : m_plans) {
		auto* item = new QTreeWidgetItem;
		const QString name = qs(plan.material);
		item->setText(0, name);
		item->setData(0, MATERIAL_ROLE, name);
		item->setToolTip(0, name);
		const EnhanceTexture* base = nullptr;
		for (const auto& t : plan.textures) {
			if (t.key == "$basetexture") base = &t;
		}
		if (plan.skipped.empty()) {
			++doing;
			bytes += plan.bytesEstimate();
			if (base) {
				item->setText(1, sizeText(base->fromWidth, base->fromHeight));
				item->setText(2, sizeText(base->toWidth, base->toHeight));
			}
			item->setText(3, plan.bumpName.empty() ? QString() : tr("new"));
			QStringList notes;
			if (plan.kind == EnhanceKind::World && plan.scaleU != 1.0) notes << tr("same size on walls");
			if (!plan.maskName.empty()) notes << tr("reflection mask");
			if (plan.textures.size() > (base ? 1u : 0u)) notes << tr("%n more texture(s)", nullptr, int(plan.textures.size() - (base ? 1 : 0)));
			item->setText(4, notes.join(QStringLiteral(", ")));
			if (plan.kind == EnhanceKind::World && !plan.bumpName.empty()) ++worldBumps;
		} else {
			item->setText(4, qs(plan.skipped));
			item->setToolTip(4, qs(plan.skipped));
			for (int c = 0; c < 5; ++c) item->setForeground(c, palette().color(QPalette::PlaceholderText));
		}
		if (name == current || (!select && current.isEmpty() && plan.skipped.empty())) select = item;
		items << item;
	}
	m_list->addTopLevelItems(items);
	m_list->resizeColumnToContents(1);
	m_list->resizeColumnToContents(2);
	m_list->resizeColumnToContents(3);
	m_list->setColumnWidth(0, std::min(m_list->columnWidth(0) + 40, 320));
	m_list->setUpdatesEnabled(true);
	const int skipped = int(m_plans.size()) - doing;
	m_summary->setText(doing == 0 ? tr("Nothing to do for these %n material(s) (see Note).", nullptr, int(m_plans.size()))
	                              : tr("%1 of %2 materials get enhanced (about %3 of new files); %4 stay as they are.")
	                                    .arg(doing)
	                                    .arg(m_plans.size())
	                                    .arg(bytesText(bytes))
	                                    .arg(skipped));
	m_note->setEnabled(worldBumps > 0);
	m_apply->setEnabled(doing > 0 && !m_running);
	if (select) m_list->setCurrentItem(select);
	else showPreview();
}

void TextureEnhanceDialog::showPreview() {
	const QTreeWidgetItem* item = m_list->currentItem();
	const LoadedGame* game = m_editor->game();
	if (!item || !game) {
		m_compare->clear(tr("Choose a material to compare"));
		return;
	}
	const QString name = item->data(0, MATERIAL_ROLE).toString();
	const auto it = std::find_if(m_plans.begin(), m_plans.end(), [&](const MaterialEnhancement& p) { return qs(p.material) == name; });
	if (it == m_plans.end() || !it->skipped.empty() || it->textures.empty() || it->textures.front().key != "$basetexture") {
		m_compare->clear(it != m_plans.end() && !it->skipped.empty() ? qs(it->skipped) : tr("Only a bump map or mask is added - nothing to compare."));
		return;
	}
	if (m_previewWatcher.isRunning()) {
		m_previewStop->store(true);
		m_previewAgain = true;
		return;
	}
	m_previewStop = std::make_shared<std::atomic<bool>>(false);
	m_compare->clear(tr("Making the comparison ..."));
	const GameFileSystem* fs = &game->files;
	m_previewWatcher.setFuture(QtConcurrent::run([fs, plan = *it, options = options(), ai = m_ai, stop = m_previewStop] {
		return previewEnhancement(*fs, plan, options, ai ? &ai : nullptr, stop.get(), 384);
	}));
}

void TextureEnhanceDialog::previewReady() {
	if (m_previewAgain) {
		m_previewAgain = false;
		showPreview();
		return;
	}
	const auto preview = m_previewWatcher.result();
	if (!preview) {
		m_compare->clear(tr("The texture cannot be read."));
		return;
	}
	QString label = preview->usedAi ? tr("After (AI)") : tr("After (sharp)");
	if (!preview->note.empty() && options().method == UpscaleMethod::Ai) {
		label = tr("After (sharp - AI: %1)").arg(qs(preview->note).left(60));
	}
	m_compare->setPictures(toQImage(preview->before), toQImage(preview->after), label);
}

void TextureEnhanceDialog::setRunning(bool on) {
	m_running = on;
	m_progress->setVisible(on);
	m_cancel->setVisible(on);
	m_apply->setEnabled(!on);
	m_remove->setEnabled(!on);
	for (QWidget* w : std::initializer_list<QWidget*>{m_factor, m_method, m_maxSize, m_bumps, m_bigBumps, m_masks, m_compress}) w->setEnabled(!on);
	if (on) m_tick.start();
	else m_tick.stop();
}

void TextureEnhanceDialog::apply() {
	const LoadedGame* game = m_editor->game();
	const GameEntry* entry = m_editor->gameEntry();
	if (!game || !entry || m_running || m_planWatcher.isRunning()) return;
	std::vector<MaterialEnhancement> work;
	for (const auto& plan : m_plans) {
		if (plan.skipped.empty()) work.push_back(plan);
	}
	if (work.empty()) return;
	m_stop->store(false);
	m_done->store(0);
	m_total = int(work.size());
	m_progress->setRange(0, m_total);
	m_progress->setValue(0);
	setRunning(true);
	GameFileSystem* fs = &m_editor->game()->files;
	const EnhanceOptions o = options();
	const AiUpscaler ai = o.method == UpscaleMethod::Ai ? m_ai : AiUpscaler{};
	m_runWatcher.setFuture(QtConcurrent::run([fs, modDir = entry->gameDir, work = std::move(work), o, ai, stop = m_stop, done = m_done]() mutable {
		auto result = std::make_shared<RunResult>();
		std::mutex mutex;
		std::vector<EnhanceOutput> pending;
		const auto flush = [&] {
			// Written in small groups: a run stopped halfway keeps (and records) what it made.
			std::vector<std::string> problems;
			for (const auto& name : writeEnhancements(modDir, pending, &problems, fs)) result->written << qs(name);
			for (const auto& p : problems) result->problems << qs(p);
			pending.clear();
		};
		const auto one = [&](const MaterialEnhancement& plan) {
			if (stop->load()) return;
			EnhanceOutput out = makeEnhancement(*fs, modDir, plan, o, ai ? &ai : nullptr, stop.get());
			std::lock_guard lock(mutex);
			if (!out.error.empty() && out.error != "cancelled") result->problems << qs(plan.material + ": " + out.error);
			for (const auto& note : out.notes) result->problems << qs(note);
			if (out.usedAi) ++result->ai;
			if (out.error.empty()) pending.push_back(std::move(out));
			if (pending.size() >= 8) flush();
			done->fetch_add(1);
		};
		if (ai) {
			for (const auto& plan : work) one(plan);   // the GPU does one at a time
		} else {
			// The built-in scaling on every core but one (the editor stays usable).
			QThreadPool pool;
			pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
			QtConcurrent::blockingMap(&pool, work, one);
		}
		std::lock_guard lock(mutex);
		flush();
		result->cancelled = stop->load();
		return result;
	}));
}

void TextureEnhanceDialog::removeAll() {
	const LoadedGame* game = m_editor->game();
	const GameEntry* entry = m_editor->gameEntry();
	if (!game || !entry || m_running) return;
	std::vector<std::string> names;
	for (const auto& name : m_materials) names.push_back(name.toLower().toStdString());
	if (names.empty()) return;
	m_stop->store(false);
	m_done->store(0);
	m_total = int(names.size());
	m_progress->setRange(0, 0);
	setRunning(true);
	GameFileSystem* fs = &m_editor->game()->files;
	m_runWatcher.setFuture(QtConcurrent::run([fs, modDir = entry->gameDir, names = std::move(names)] {
		auto result = std::make_shared<RunResult>();
		result->removing = true;
		// Only those that are enhanced (the record knows).
		const TextureManifest manifest = loadTextureManifest(modDir);
		std::vector<std::string> enhanced;
		for (const auto& name : names) {
			if (manifest.find(name)) enhanced.push_back(name);
		}
		if (enhanced.empty()) return result;
		std::vector<std::string> problems;
		removeEnhancements(*fs, modDir, enhanced, &problems, fs);
		for (const auto& name : enhanced) result->removed << qs(name);
		for (const auto& p : problems) result->problems << qs(p);
		return result;
	}));
}

void TextureEnhanceDialog::ran() {
	const auto result = m_runWatcher.result();
	setRunning(false);
	m_stop->store(false);
	if (!result) return;
	const QStringList changed = result->written + result->removed;
	if (result->removing) {
		m_status->setText(result->removed.isEmpty() ? tr("None of these materials was enhanced.")
		                                            : tr("Removed the enhancement of %n material(s).", nullptr, int(result->removed.size())));
	} else {
		QString text = tr("Enhanced %n material(s)", nullptr, int(result->written.size()));
		if (result->ai > 0) text += tr(" (%n with the AI)", nullptr, result->ai);
		if (result->cancelled) text += tr(" - stopped");
		m_status->setText(text + QLatin1Char('.'));
	}
	if (!result->problems.isEmpty()) {
		m_status->setToolTip(result->problems.join(QLatin1Char('\n')));
		for (const auto& p : result->problems) emit m_editor->message(tr("Enhance textures: %1").arg(p));
	}
	if (!changed.isEmpty()) {
		emit m_editor->message(m_status->text());
		emit materialsWritten(changed);
		// The views read them again (the same signal Game enhancements gives), and Game
		// enhancements makes its materials from the enhanced ones.
		if (auto* enhancements = m_editor->enhancements()) {
			emit enhancements->materialsChanged(changed);
			if (enhancements->available()) enhancements->sync();
		}
	}
	replan();
	emit runFinished();
}

TextureEnhanceDialog* openTextureEnhanceDialog(Editor* editor, const QStringList& materials, QWidget* parent) {
	if (!editor->game()) {
		QMessageBox::information(parent, QObject::tr("Enhance textures"), QObject::tr("Choose a game first."));
		return nullptr;
	}
	auto* dialog = new TextureEnhanceDialog(editor, materials, parent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
	return dialog;
}

} // namespace sme::app
