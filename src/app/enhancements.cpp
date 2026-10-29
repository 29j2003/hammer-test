// enhancements.cpp - Game enhancements of the mod (see enhancements.h).

#include "enhancements.h"

#include "editor.h"
#include "toggle_switch.h"

#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <sme/container_models.h>
#include <sme/game.h>
#include <sme/map.h>
#include <sme/text.h>

#include <algorithm>

namespace sme::app {

namespace {

QLabel* noteLabel(const QString& text = {}) {
	auto* label = new QLabel(text);
	label->setProperty("role", "description");
	label->setWordWrap(true);
	return label;
}

} // namespace

// --- GameEnhancements ---------------------------------------------------------------------

namespace {

/// The materials on the open map's brushes (lower case).
std::set<std::string> openMapMaterials(const MapDocument& doc) {
	std::set<std::string> out;
	const auto add = [&out](const std::vector<Solid>& solids) {
		for (const auto& solid : solids) {
			for (const auto& side : solid.sides) {
				std::string name = toLower(side.material);
				std::replace(name.begin(), name.end(), '\\', '/');
				if (!name.empty()) out.insert(std::move(name));
			}
		}
	};
	add(doc.world.solids);
	for (const auto& entity : doc.entities) add(entity.solids);
	return out;
}

} // namespace

GameEnhancements::GameEnhancements(Editor* editor) : m_editor(editor) {
	// New materials on the open map's walls get the pattern relief: checked a moment after the
	// changes stop (reading every side on each step would slow editing down).
	m_mapCheck.setSingleShot(true);
	m_mapCheck.setInterval(2000);
	connect(&m_mapCheck, &QTimer::timeout, this, [this] {
		if (!available()) return;
		auto materials = openMapMaterials(m_editor->doc());
		if (materials == m_openMapMaterials) return;
		m_openMapMaterials = std::move(materials);
		sync();
	});
	connect(editor, &Editor::documentChanged, this, [this] { m_mapCheck.start(); });
	connect(editor, &Editor::documentReplaced, this, [this] { m_mapCheck.start(); });
	connect(editor, &Editor::gameAboutToChange, this, [this] {
		m_again = false;
		m_running = false;
		++m_generation;
		m_watcher.waitForFinished();   // it reads and writes the old game's files
	});
	connect(editor, &Editor::gameChanged, this, &GameEnhancements::gameChanged);
	connect(&m_watcher, &QFutureWatcherBase::finished, this, &GameEnhancements::finished);
}

GameEnhancements::~GameEnhancements() {
	m_watcher.waitForFinished();
}

bool GameEnhancements::available() const {
	return m_editor->game() && m_editor->gameEntry() && m_editor->hasFidelity();
}

void GameEnhancements::gameChanged() {
	m_ready = false;
	m_candidates.clear();
	m_candidateSets.clear();
	m_excluded.clear();
	m_active.clear();
	m_modelMaterials.clear();
	m_keptOwn.clear();
	m_containerModels.clear();
	m_liquidModels.clear();
	m_failed.clear();
	m_lastWritten = 0;
	m_settings = {};
	m_modDir.clear();
	m_patternRelief = 0;
	m_heldBack = 0;
	m_brokenOwn.clear();
	m_openMapMaterials.clear();
	if (available()) {
		m_modDir = m_editor->gameEntry()->gameDir;
		m_openMapMaterials = openMapMaterials(m_editor->doc());
		m_settings = loadEnhancementSettings(m_modDir);
		sync();
	}
	emit changed();
}

void GameEnhancements::sync() {
	if (!available()) return;
	if (m_running) {
		m_again = true;   // once this run is done
		return;
	}
	GameFileSystem* fs = &m_editor->game()->files;
	const bool scan = !m_ready;
	m_running = true;
	const QString mapDir = m_editor->gameSettings().mapDir;
	m_watcher.setFuture(QtConcurrent::run([fs, scan, generation = m_generation, modDir = m_modDir, settings = m_settings, candidates = m_candidates,
	                                       mapDir, openMap = m_openMapMaterials] {
		Result result;
		result.generation = generation;
		result.scanned = scan;
		result.candidates = scan ? findEffectCandidates(*fs, modDir) : candidates;
		result.excluded = excludedMaterials(*fs, settings);
		// The walls of the mod's maps (its map folder and the open map) get the pattern relief.
		// (Not the autosaves: old copies, often dozens, read again after every edit of the open map.)
		std::set<std::string> mapMaterials = vmfMaterialNames(mapSourceFiles(mapDir.isEmpty() ? std::filesystem::path() : toPath(mapDir)));
		mapMaterials.insert(openMap.begin(), openMap.end());
		EnhancementOptions options;
		options.shaderDll = modHasShaderDll(modDir);
		options.mapMaterials = &mapMaterials;
		result.shaderDll = options.shaderDll;
		result.plan = planEnhancements(*fs, modDir, settings, result.excluded, &result.candidates, options);
		for (const auto& change : result.plan.changes) {
			const QString name = QString::fromStdString(change.material);
			const QString path = fromPath(change.file);
			bool ok = true;
			if (change.remove) {
				ok = QFile::remove(path) || !QFileInfo::exists(path);
				if (ok) fs->noteRemovedFile(change.file);
			} else {
				QDir().mkpath(QFileInfo(path).absolutePath());
				QSaveFile file(path);
				ok = file.open(QIODevice::WriteOnly) && file.write(change.text.data(), qint64(change.text.size())) == qint64(change.text.size()) &&
				     file.commit();
				if (ok) fs->noteWrittenFile(change.file);
			}
			// A pattern relief's height map goes with its material.
			if (ok && !change.heightFile.empty()) {
				const QString height = fromPath(change.heightFile);
				if (!change.heightVtf.empty() && !change.remove) {
					QSaveFile file(height);
					ok = file.open(QIODevice::WriteOnly) &&
					     file.write(reinterpret_cast<const char*>(change.heightVtf.data()), qint64(change.heightVtf.size())) == qint64(change.heightVtf.size()) &&
					     file.commit();
					if (ok) fs->noteWrittenFile(change.heightFile);
				} else if (QFileInfo::exists(height)) {
					ok = QFile::remove(height);
					if (ok) fs->noteRemovedFile(change.heightFile);
				}
			}
			(ok ? result.written : result.failed) << name;
		}
		// Container model copies (their own materials are written above): the list first, so a run
		// stopped halfway still knows the copies are ours (as deploy.ps1 does).
		const auto manifest = modDir / std::filesystem::path(std::string(CONTAINER_MODELS_MANIFEST));
		const auto writeBytes = [](const QString& path, const char* data, qint64 size) {
			QDir().mkpath(QFileInfo(path).absolutePath());
			QSaveFile file(path);
			return file.open(QIODevice::WriteOnly) && file.write(data, size) == size && file.commit();
		};
		bool listOk = true;
		if (result.plan.modelsManifest && !result.plan.modelsManifest->empty()) {
			const auto& text = *result.plan.modelsManifest;
			listOk = writeBytes(fromPath(manifest), text.data(), qint64(text.size()));
			if (!listOk) result.failed << QString::fromStdString(std::string(CONTAINER_MODELS_MANIFEST));
		}
		for (const auto& change : result.plan.modelChanges) {
			const QString path = fromPath(change.file);
			bool ok = false;
			if (change.remove) {
				ok = QFile::remove(path) || !QFileInfo::exists(path);
				if (ok) fs->noteRemovedFile(change.file);
			} else if (listOk) {
				ok = writeBytes(path, reinterpret_cast<const char*>(change.bytes.data()), qint64(change.bytes.size()));
				if (ok) fs->noteWrittenFile(change.file);
			}
			(ok ? result.writtenModels : result.failed) << QString::fromStdString(change.model);
		}
		if (result.plan.modelsManifest && result.plan.modelsManifest->empty()) QFile::remove(fromPath(manifest));
		// The mapper's own materials with the mod's shaders: broken in the game without the DLL.
		if (!options.shaderDll) {
			for (const auto& material : ownMaterialsNeedingShaderDll(modDir)) result.brokenOwn << QString::fromStdString(material);
		}
		return result;
	}));
	emit changed();
}

void GameEnhancements::finished() {
	Result result = m_watcher.result();
	if (result.generation != m_generation || !available()) return;
	m_running = false;
	if (result.scanned) {
		m_candidates = std::move(result.candidates);
		m_candidateSets.clear();
		for (const auto& [effect, materials] : m_candidates) m_candidateSets[effect] = {materials.begin(), materials.end()};
		m_ready = true;
	}
	m_excluded = std::move(result.excluded);
	m_active = result.plan.active;
	m_patternRelief = result.plan.patternRelief;
	m_heldBack = result.plan.heldBack;
	m_shaderDll = result.shaderDll;
	m_brokenOwn = result.brokenOwn;
	m_keptOwn.clear();
	for (const auto& material : result.plan.keptOwn) m_keptOwn << QString::fromStdString(material);
	m_containerModels = {result.plan.containerModels.begin(), result.plan.containerModels.end()};
	m_liquidModels.clear();
	for (const auto& model : result.plan.liquidModels) m_liquidModels << QString::fromStdString(model);
	m_failed = result.failed;
	m_lastWritten = int(result.written.size());
	if (!result.written.isEmpty()) {
		emit m_editor->message(tr("Game enhancements: %n material(s) of the mod brought in line with its settings", nullptr, m_lastWritten));
		emit materialsChanged(result.written);
	}
	if (!result.writtenModels.isEmpty()) {
		emit m_editor->message(tr("Game enhancements: model copies with liquid: %1").arg(result.writtenModels.join(QStringLiteral(", "))));
		emit modelsChanged(result.writtenModels);
	}
	for (const auto& note : result.plan.modelNotes) emit m_editor->message(tr("Game enhancements: %1").arg(QString::fromStdString(note)));
	if (!m_failed.isEmpty()) {
		emit m_editor->message(tr("Game enhancements: could not write %1").arg(m_failed.join(QStringLiteral(", "))));
	}
	if (m_again) {
		m_again = false;
		sync();
	}
	emit changed();
}

void GameEnhancements::save() {
	if (!saveEnhancementSettings(m_modDir, m_settings)) {
		emit m_editor->message(tr("Could not write %1").arg(QDir::toNativeSeparators(fromPath(enhancementSettingsFile(m_modDir)))));
	}
}

void GameEnhancements::setEffectEnabled(GameEffect effect, bool on) {
	if (!available() || m_settings.enabled(effect) == on) return;
	if (on) m_settings.off.erase(effect);
	else m_settings.off.insert(effect);
	save();
	sync();
}

void GameEnhancements::setExcluded(GameEffect effect, const QString& item, bool excluded) {
	if (!available() || item.isEmpty() || m_settings.isExcluded(effect, item.toStdString()) == excluded) return;
	m_settings.setExcluded(effect, item.toStdString(), excluded);
	save();
	sync();
}

std::vector<std::string> GameEnhancements::materialsOf(const QString& model) const {
	const QString key = model.toLower();
	auto it = m_modelMaterials.find(key);
	if (it == m_modelMaterials.end()) {
		it = m_modelMaterials.emplace(key, m_editor->game() ? modelMaterialNames(m_editor->game()->files, key.toStdString()) : std::vector<std::string>{}).first;
	}
	return it->second;
}

std::vector<GameEffect> GameEnhancements::effectsFor(const QString& model) const {
	std::vector<GameEffect> out;
	if (!available() || !m_ready || model.isEmpty()) return out;
	const auto materials = materialsOf(model);
	for (const GameEffect effect : automaticEffects()) {
		// A container on a shared material gets liquid through its own model copy.
		if (effect == GameEffect::Liquid && m_containerModels.count(model.toLower().toStdString())) {
			out.push_back(effect);
			continue;
		}
		const auto it = m_candidateSets.find(effect);
		if (it == m_candidateSets.end()) continue;
		if (std::any_of(materials.begin(), materials.end(), [&](const std::string& m) { return it->second.count(m) > 0; })) out.push_back(effect);
	}
	return out;
}

bool GameEnhancements::sharesExcludedMaterial(GameEffect effect, const QString& model) const {
	if (m_settings.isExcluded(effect, model.toStdString())) return false;
	const auto it = m_excluded.find(effect);
	if (it == m_excluded.end()) return false;
	const auto materials = materialsOf(model);
	return std::any_of(materials.begin(), materials.end(), [&](const std::string& m) { return it->second.count(m) > 0; });
}

int GameEnhancements::candidateCount(GameEffect effect) const {
	const auto it = m_candidates.find(effect);
	return it == m_candidates.end() ? 0 : int(it->second.size());
}

int GameEnhancements::activeCount(GameEffect effect) const {
	const auto it = m_active.find(effect);
	return it == m_active.end() ? 0 : it->second;
}

QString GameEnhancements::status() const {
	if (!available()) return {};
	if (busy()) return m_ready ? tr("Writing the mod's materials ...") : tr("Looking for the materials the effects are made for ...");
	int total = 0;
	for (const auto& [effect, count] : m_active) total += count;
	QStringList parts{tr("Up to date: %n material(s) with an effect", nullptr, total)};
	if (m_patternRelief > 0) parts << tr("%n wall(s) of your maps with relief from their pattern", nullptr, m_patternRelief);
	if (!m_liquidModels.isEmpty()) parts << tr("model copies with liquid: %1 (%2)").arg(m_liquidModels.size()).arg(m_liquidModels.join(QStringLiteral(", ")));
	if (!m_keptOwn.isEmpty()) parts << tr("%n kept (your own material)", nullptr, int(m_keptOwn.size()));
	if (!m_failed.isEmpty()) parts << tr("%n could not be written", nullptr, int(m_failed.size()));
	return parts.join(QStringLiteral(" · "));
}

// --- EnhancementSwitches ------------------------------------------------------------------

EnhancementSwitches::EnhancementSwitches(GameEnhancements* enhancements, QWidget* parent)
    : QGroupBox(tr("Game enhancements"), parent), m_enhancements(enhancements) {
	setObjectName("enhancementSwitches");
	m_rows = new QVBoxLayout(this);
	m_rows->setSpacing(4);
	connect(enhancements, &GameEnhancements::changed, this, &EnhancementSwitches::refresh);
}

void EnhancementSwitches::setModel(const QString& model) {
	m_model = model;
	refresh();
}

void EnhancementSwitches::refresh() {
	// Rebuilt in a fresh widget: a switch may be the one whose click led here.
	while (auto* item = m_rows->takeAt(0)) {
		if (auto* widget = item->widget()) {
			widget->hide();
			widget->deleteLater();
		}
		delete item;
	}
	const auto effects = m_enhancements->effectsFor(m_model);
	if (parentWidget()) setVisible(!effects.empty());
	if (effects.empty()) return;
	auto* content = new QWidget;
	auto* v = new QVBoxLayout(content);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(4);
	const auto& settings = m_enhancements->settings();
	for (const GameEffect effect : effects) {
		const auto& info = gameEffectInfo(effect);
		auto* row = new QHBoxLayout;
		row->setSpacing(8);
		auto* toggle = new ToggleSwitch;
		toggle->setObjectName(QStringLiteral("enhancement:%1").arg(QLatin1String(info.id)));
		const bool modWide = settings.enabled(effect);
		const bool excluded = settings.isExcluded(effect, m_model.toStdString());
		const bool shared = modWide && !excluded && m_enhancements->sharesExcludedMaterial(effect, m_model);
		toggle->setChecked(modWide && !excluded && !shared);
		toggle->setEnabled(modWide && !shared);
		QString note;
		if (!modWide) note = tr("off for the whole mod (Project > Overview)");
		else if (shared) note = tr("off: a material is shared with a model that is left out");
		else if (excluded) note = tr("left out");
		toggle->setToolTip(QString::fromUtf8(info.description));
		connect(toggle, &QCheckBox::toggled, this, [this, effect](bool on) { m_enhancements->setExcluded(effect, m_model, !on); });
		row->addWidget(toggle);
		auto* title = new QLabel(QString::fromUtf8(info.title));
		title->setToolTip(QString::fromUtf8(info.description));
		row->addWidget(title);
		if (!note.isEmpty()) row->addWidget(noteLabel(note), 1);
		row->addStretch(note.isEmpty() ? 1 : 0);
		v->addLayout(row);
	}
	v->addWidget(noteLabel(tr("On by default. Models share materials: what one model leaves out is off for every model that uses the material.")));
	m_rows->addWidget(content);
}

// --- EnhancementsSection ------------------------------------------------------------------

EnhancementsSection::EnhancementsSection(GameEnhancements* enhancements, QWidget* parent)
    : QGroupBox(tr("Game enhancements"), parent), m_enhancements(enhancements) {
	setObjectName("enhancementsSection");
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
	auto* v = new QVBoxLayout(this);
	m_intro = noteLabel();
	v->addWidget(m_intro);
	m_body = new QWidget;
	auto* body = new QHBoxLayout(m_body);
	body->setContentsMargins(0, 0, 0, 0);
	body->setSpacing(16);

	// Left: one switch per effect for the whole mod, with what it does and how many materials have it.
	auto* grid = new QGridLayout;
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(6);
	int row = 0;
	for (const GameEffect effect : automaticEffects()) {
		const auto& info = gameEffectInfo(effect);
		auto* toggle = new ToggleSwitch;
		toggle->setObjectName(QStringLiteral("enhancementMod:%1").arg(QLatin1String(info.id)));
		connect(toggle, &QCheckBox::toggled, this, [this, effect](bool on) { m_enhancements->setEffectEnabled(effect, on); });
		auto* title = new QLabel(QStringLiteral("<b>%1</b>").arg(QString::fromUtf8(info.title).toHtmlEscaped()));
		auto* count = noteLabel();
		count->setWordWrap(false);
		auto* description = noteLabel(QString::fromUtf8(info.description));
		grid->addWidget(toggle, row, 0, Qt::AlignTop);
		grid->addWidget(title, row, 1, Qt::AlignTop);
		grid->addWidget(count, row, 2, Qt::AlignTop | Qt::AlignRight);
		grid->addWidget(description, row + 1, 1, 1, 2);
		m_switches[effect] = toggle;
		m_counts[effect] = count;
		row += 2;
	}
	grid->setColumnStretch(1, 1);
	grid->setRowStretch(row, 1);
	body->addLayout(grid, 3);

	// Right: what is left out, per model (or material).
	auto* right = new QVBoxLayout;
	right->setSpacing(4);
	right->addWidget(new QLabel(tr("Left out")));
	m_excluded = new QTreeWidget;
	m_excluded->setObjectName("enhancementExcluded");
	m_excluded->setHeaderLabels({tr("Model or material"), tr("Effect")});
	m_excluded->setRootIsDecorated(false);
	m_excluded->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_excluded->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_excluded->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_excluded->header()->setStretchLastSection(false);
	m_excluded->setMinimumHeight(70);
	m_excluded->setMaximumHeight(130);
	right->addWidget(m_excluded, 1);
	auto* buttons = new QHBoxLayout;
	m_remove = new QPushButton(tr("Remove from list"));
	m_remove->setObjectName("enhancementRemove");
	m_remove->setToolTip(tr("The effect comes back on for the chosen models"));
	buttons->addWidget(m_remove);
	buttons->addStretch(1);
	right->addLayout(buttons);
	body->addLayout(right, 2);
	v->addWidget(m_body);
	m_status = noteLabel();
	m_status->setObjectName("enhancementStatus");
	v->addWidget(m_status);
	// Without the mod's shader DLL these effects would be wireframes in the game.
	m_dllBox = new QWidget;
	m_dllBox->setObjectName("enhancementDllMissing");
	auto* dll = new QHBoxLayout(m_dllBox);
	dll->setContentsMargins(0, 0, 0, 0);
	m_dllText = noteLabel();
	m_dllText->setStyleSheet(QStringLiteral("color: #ffc070;"));
	dll->addWidget(m_dllText, 1);
	auto* install = new QPushButton(tr("Install features"));
	install->setObjectName("enhancementInstallFeatures");
	install->setProperty("accent", true);
	install->setToolTip(tr("Puts the mod's shaders and the rest of the features into the mod (Project › Features)"));
	connect(install, &QPushButton::clicked, enhancements, &GameEnhancements::requestInstallFeatures);
	dll->addWidget(install, 0, Qt::AlignTop);
	v->addWidget(m_dllBox);

	connect(m_excluded, &QTreeWidget::itemSelectionChanged, this, [this] { m_remove->setEnabled(!m_excluded->selectedItems().isEmpty()); });
	connect(m_remove, &QPushButton::clicked, this, [this] {
		std::vector<std::pair<GameEffect, QString>> items;
		for (auto* item : m_excluded->selectedItems()) items.emplace_back(static_cast<GameEffect>(item->data(1, Qt::UserRole).toInt()), item->text(0));
		for (const auto& [effect, name] : items) m_enhancements->setExcluded(effect, name, false);
	});
	connect(enhancements, &GameEnhancements::changed, this, &EnhancementsSection::refresh);
	refresh();
}

void EnhancementsSection::refresh() {
	const bool available = m_enhancements->available();
	m_body->setVisible(available);
	m_status->setVisible(available);
	m_dllBox->setVisible(false);
	m_intro->setText(available ? tr("The extra effects of mapbase-fidelity, on for everything they are made for. Mapsmith writes the "
	                                "materials into the mod (as deploy.bat does) and keeps your own materials as they are. Switch an "
	                                "effect off here, or for one model in its properties.")
	                           : tr("This mod has no mapbase-fidelity (cfg/fidelity.cfg or its shaders), so there is nothing to switch."));
	if (!available) return;
	const auto& settings = m_enhancements->settings();
	for (const GameEffect effect : automaticEffects()) {
		auto* toggle = m_switches[effect];
		const QSignalBlocker block(toggle);
		toggle->setChecked(settings.enabled(effect));
		const int candidates = m_enhancements->candidateCount(effect);
		QString count;
		if (m_enhancements->ready()) {
			count = candidates == 0 ? tr("nothing in this game")
			                        : tr("on for %1 of %n material(s)", nullptr, candidates).arg(m_enhancements->activeCount(effect));
		}
		m_counts[effect]->setText(count);
	}
	m_excluded->clear();
	for (const auto& [effect, items] : settings.excluded) {
		for (const auto& item : items) {
			auto* row = new QTreeWidgetItem({QString::fromStdString(item), QString::fromUtf8(gameEffectInfo(effect).title)});
			row->setData(1, Qt::UserRole, int(effect));
			m_excluded->addTopLevelItem(row);
		}
	}
	if (m_excluded->topLevelItemCount() == 0) {
		auto* none = new QTreeWidgetItem({tr("Nothing left out")});
		none->setToolTip(0, tr("Switch an effect off for a model in its properties or on the Models page"));
		none->setFlags(Qt::NoItemFlags);
		m_excluded->addTopLevelItem(none);
	}
	m_remove->setEnabled(!m_excluded->selectedItems().isEmpty());
	m_status->setText(m_enhancements->status());
	const bool dllMissing = available && m_enhancements->ready() && !m_enhancements->shaderDll();
	m_dllBox->setVisible(dllMissing);
	if (dllMissing) {
		QString text = tr("The mod's shaders are not installed (bin\\game_shader_dx6.dll). Without them the game draws relief, liquids and "
		                  "the new water as wireframes - a floor turns see-through and shows the sky, bottles vanish. So Mapsmith holds "
		                  "these effects back (%n material(s)) until the features are installed.",
		                  nullptr, m_enhancements->heldBackCount());
		const auto& own = m_enhancements->brokenOwn();
		if (!own.isEmpty()) {
			text += QStringLiteral(" ") + tr("Your own materials with these shaders are broken in the game until then: %1.")
			                                  .arg(QStringList(own.mid(0, 6)).join(QStringLiteral(", ")) + (own.size() > 6 ? QStringLiteral(" ...") : QString()));
		}
		m_dllText->setText(text);
	}
	m_status->setToolTip(m_enhancements->keptOwn().isEmpty() ? QString()
	                                                          : tr("Kept (your own material):\n%1").arg(m_enhancements->keptOwn().join('\n')));
}

} // namespace sme::app
