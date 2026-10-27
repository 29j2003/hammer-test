// maps_panel.cpp - Map list with batch compile (see maps_panel.h).

#include "maps_panel.h"

#include "compile.h"
#include "editor.h"

#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <sme/assets.h>
#include <sme/game.h>
#include <sme/map.h>
#include <sme/text.h>

namespace sme::app {

namespace {

enum Column { NameColumn, FolderColumn, ModifiedColumn, BspColumn, ResultColumn, ColumnCount };
constexpr int PathRole = Qt::UserRole;

/// Map file names: letters, digits, '_' and '-' (the engine and the compile tools dislike the rest).
bool validMapName(const QString& name) {
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_\\-]{1,64}$"));
	return pattern.match(name).hasMatch();
}

QString gameDirOf(const Editor* editor) {
	return editor->game() ? fromPath(editor->game()->entry.gameDir) : QString();
}

} // namespace

QString compiledBsp(const QString& vmfPath, const QString& gameDir) {
	const QFileInfo vmf(vmfPath);
	const QString name = vmf.completeBaseName() + ".bsp";
	for (const QString& candidate : {gameDir.isEmpty() ? QString() : QDir(gameDir).filePath("maps/" + name), vmf.absoluteDir().filePath(name)}) {
		if (!candidate.isEmpty() && QFileInfo::exists(candidate)) return candidate;
	}
	return {};
}

BspState bspState(const QString& vmfPath, const QString& gameDir) {
	const QString bsp = compiledBsp(vmfPath, gameDir);
	if (bsp.isEmpty()) return BspState::NotCompiled;
	return QFileInfo(bsp).lastModified() >= QFileInfo(vmfPath).lastModified() ? BspState::UpToDate : BspState::Outdated;
}

MapsPanel::MapsPanel(Editor* editor, CompileRunner* compiler, QWidget* parent) : QWidget(parent), m_editor(editor), m_compiler(compiler) {
	setObjectName("mapsPanel");
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(8);

	m_folderLabel = new QLabel;
	m_folderLabel->setProperty("role", "description");
	m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(m_folderLabel);

	// File buttons.
	auto* files = new QHBoxLayout;
	const auto button = [this](QHBoxLayout* row, const QString& text, const QString& name, const QString& tip) {
		auto* b = new QPushButton(text);
		b->setObjectName(name);
		b->setToolTip(tip);
		row->addWidget(b);
		return b;
	};
	m_open = button(files, tr("Open"), "mapsOpen", tr("Open the selected map in the editor (or double-click it)"));
	auto* create = button(files, tr("New map ..."), "mapsNew", tr("An empty map in the map folder"));
	m_duplicate = button(files, tr("Duplicate"), "mapsDuplicate", tr("A copy of the selected map"));
	m_rename = button(files, tr("Rename ..."), "mapsRename", tr("Give the selected map file another name"));
	m_delete = button(files, tr("Delete"), "mapsDelete", tr("Move the selected map file to the trash"));
	files->addStretch(1);
	auto* refreshButton = button(files, tr("Refresh"), "mapsRefresh", tr("Read the map folder again"));
	v->addLayout(files);

	m_table = new QTreeWidget;
	m_table->setObjectName("mapsTable");
	m_table->setColumnCount(ColumnCount);
	m_table->setHeaderLabels({tr("Map"), tr("Folder"), tr("Changed"), tr("Compiled"), tr("Last compile")});
	m_table->setRootIsDecorated(false);
	m_table->setUniformRowHeights(true);
	m_table->setSortingEnabled(true);
	m_table->sortByColumn(NameColumn, Qt::AscendingOrder);
	m_table->header()->setSectionResizeMode(NameColumn, QHeaderView::ResizeToContents);
	m_table->header()->setStretchLastSection(true);
	v->addWidget(m_table, 1);

	// Batch compile.
	auto* batch = new QHBoxLayout;
	m_compileChecked = button(batch, tr("Compile checked"), "mapsCompileChecked", tr("Compile every checked map, one after another"));
	m_compileChecked->setProperty("accent", true);
	m_compileOutdated = button(batch, tr("Compile all outdated"), "mapsCompileOutdated",
	                           tr("Compile every map that was never compiled or changed since its last compile"));
	m_runAfter = new QCheckBox(tr("Run the game afterwards"));
	m_runAfter->setObjectName("mapsRunAfter");
	m_runAfter->setToolTip(tr("Start the game with the last map that compiled (the compile settings decide window, console ...)"));
	m_runAfter->setChecked(QSettings().value("maps/runAfter", false).toBool());
	batch->addWidget(m_runAfter);
	batch->addStretch(1);
	m_cancel = button(batch, tr("Cancel"), "mapsCancel", tr("Stop after cancelling the running compile"));
	v->addLayout(batch);
	m_progress = new QProgressBar;
	m_progress->setObjectName("mapsProgress");
	m_progress->setRange(0, 1000);
	m_progress->setTextVisible(false);
	m_progress->setMaximumHeight(6);
	v->addWidget(m_progress);
	m_batchLabel = new QLabel;
	m_batchLabel->setObjectName("mapsBatchLabel");
	m_batchLabel->setProperty("role", "description");
	v->addWidget(m_batchLabel);

	// Buttons.
	const auto withSelected = [this](auto action) {
		return [this, action] {
			if (const QString path = currentPath(); !path.isEmpty()) action(path);
		};
	};
	connect(m_open, &QPushButton::clicked, this, withSelected([this](const QString& path) { emit openMapRequested(path); }));
	connect(m_table, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { emit openMapRequested(item->data(NameColumn, PathRole).toString()); });
	connect(create, &QPushButton::clicked, this, [this] {
		bool ok = false;
		const QString name = QInputDialog::getText(this, tr("New map"), tr("Name of the map (letters, digits, _ and -):"), QLineEdit::Normal, {}, &ok).trimmed();
		if (!ok || name.isEmpty()) return;
		QString error;
		const QString path = createMap(name, &error);
		if (path.isEmpty()) {
			QMessageBox::warning(this, tr("New map"), error);
		} else {
			emit openMapRequested(path);
		}
	});
	connect(m_duplicate, &QPushButton::clicked, this, withSelected([this](const QString& path) {
		QString error;
		if (duplicateMap(path, &error).isEmpty()) QMessageBox::warning(this, tr("Duplicate"), error);
	}));
	connect(m_rename, &QPushButton::clicked, this, withSelected([this](const QString& path) {
		bool ok = false;
		const QString name = QInputDialog::getText(this, tr("Rename map"), tr("New name:"), QLineEdit::Normal, QFileInfo(path).completeBaseName(), &ok).trimmed();
		if (!ok || name.isEmpty() || name == QFileInfo(path).completeBaseName()) return;
		QString error;
		if (renameMap(path, name, &error).isEmpty()) QMessageBox::warning(this, tr("Rename map"), error);
	}));
	connect(m_delete, &QPushButton::clicked, this, withSelected([this](const QString& path) {
		if (QMessageBox::question(this, tr("Delete map"), tr("Move %1 to the trash?").arg(QFileInfo(path).fileName())) != QMessageBox::Yes) return;
		QString error;
		if (!deleteMap(path, &error)) QMessageBox::warning(this, tr("Delete map"), error);
	}));
	connect(refreshButton, &QPushButton::clicked, this, &MapsPanel::refresh);
	connect(m_compileChecked, &QPushButton::clicked, this, [this] {
		if (!compileMaps(checkedMaps(), m_runAfter->isChecked())) {
			QMessageBox::information(this, tr("Compile"), tr("Check the maps to compile first (the box in front of each name)."));
		}
	});
	connect(m_compileOutdated, &QPushButton::clicked, this, [this] {
		QStringList outdated;
		for (const QString& path : maps()) {
			if (bspState(path, gameDirOf(m_editor)) != BspState::UpToDate) outdated << path;
		}
		if (!compileMaps(outdated, m_runAfter->isChecked())) {
			QMessageBox::information(this, tr("Compile"), tr("Every map is compiled and up to date."));
		}
	});
	connect(m_runAfter, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("maps/runAfter", on); });
	connect(m_cancel, &QPushButton::clicked, this, &MapsPanel::cancelBatch);
	connect(m_table, &QTreeWidget::currentItemChanged, this, &MapsPanel::updateButtons);

	// Progress of the map compiling now.
	if (m_compiler) {
		connect(m_compiler, &CompileRunner::started, this, [this](const QString&, const QStringList& steps) {
			m_stepCount = static_cast<int>(steps.size());
			m_step = 0;
		});
		connect(m_compiler, &CompileRunner::stepStarted, this, [this](int index, const QString&) {
			if (m_current.isEmpty()) return;
			m_step = index;
			const auto names = m_compiler->stepNames();
			if (auto* item = rowFor(m_current)) {
				item->setText(ResultColumn, tr("compiling: %1 (%2 of %3)").arg(index < names.size() ? names[index] : QString()).arg(index + 1).arg(m_stepCount));
			}
			const double done = m_compiled + m_failed + (m_stepCount > 0 ? double(index) / m_stepCount : 0.0);
			m_progress->setValue(m_total > 0 ? int(1000 * done / m_total) : 0);
		});
		connect(m_compiler, &CompileRunner::finished, this, [this](bool ok) {
			if (!m_current.isEmpty()) finishCurrent(ok);
		});
	}
	connect(editor, &Editor::gameChanged, this, &MapsPanel::refresh);
	refresh();
}

void MapsPanel::refresh() {
	m_folder = m_editor->game() ? m_editor->gameSettings().mapDir : QString();
	m_folderLabel->setText(m_folder.isEmpty() ? tr("No game chosen") : tr("Map folder: %1").arg(QDir::toNativeSeparators(m_folder)));
	buildRows();
	updateButtons();
}

void MapsPanel::buildRows() {
	const QStringList checked = checkedMaps();
	const QString selected = currentPath();
	m_table->setSortingEnabled(false);
	m_table->clear();
	if (!m_folder.isEmpty() && QFileInfo(m_folder).isDir()) {
		QDirIterator it(m_folder, {QStringLiteral("*.vmf")}, QDir::Files, QDirIterator::Subdirectories);
		int count = 0;
		while (it.hasNext() && count < 5000) {
			const QString path = it.next();
			if (path.endsWith(QLatin1String("_cordon.vmf"))) continue;   // made by compiling with cordons
			auto* item = new QTreeWidgetItem(m_table);
			item->setData(NameColumn, PathRole, path);
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
			item->setCheckState(NameColumn, checked.contains(path) ? Qt::Checked : Qt::Unchecked);
			updateRow(item);
			if (path == selected) m_table->setCurrentItem(item);
			++count;
		}
	}
	m_table->setSortingEnabled(true);
}

void MapsPanel::updateRow(QTreeWidgetItem* item) {
	const QString path = item->data(NameColumn, PathRole).toString();
	const QFileInfo info(path);
	item->setText(NameColumn, info.completeBaseName());
	item->setToolTip(NameColumn, QDir::toNativeSeparators(path));
	const QString folder = QDir(m_folder).relativeFilePath(info.absolutePath());
	item->setText(FolderColumn, folder == "." ? QString() : folder);
	item->setText(ModifiedColumn, QLocale().toString(info.lastModified(), QLocale::ShortFormat));
	switch (bspState(path, gameDirOf(m_editor))) {
		case BspState::NotCompiled:
			item->setText(BspColumn, tr("not compiled"));
			item->setForeground(BspColumn, QColor(150, 150, 150));
			break;
		case BspState::UpToDate:
			item->setText(BspColumn, tr("up to date"));
			item->setForeground(BspColumn, QColor(92, 207, 122));
			break;
		case BspState::Outdated:
			item->setText(BspColumn, tr("outdated"));
			item->setForeground(BspColumn, QColor(240, 170, 70));
			break;
	}
	if (path == m_current) return;   // shows the running step
	if (m_queue.contains(path)) {
		item->setText(ResultColumn, tr("waiting"));
		item->setForeground(ResultColumn, QColor(150, 150, 150));
		return;
	}
	const QStringList stored = QSettings().value(settingsKey(path)).toString().split('|');
	if (stored.size() == 2) {
		const bool ok = stored[0] == "ok";
		const QDateTime when = QDateTime::fromString(stored[1], Qt::ISODate);
		item->setText(ResultColumn, (ok ? tr("compiled") : tr("failed")) + QStringLiteral(" · ") + QLocale().toString(when, QLocale::ShortFormat));
		item->setForeground(ResultColumn, ok ? QColor(92, 207, 122) : QColor(240, 110, 100));
	} else {
		item->setText(ResultColumn, QString());
	}
}

QTreeWidgetItem* MapsPanel::rowFor(const QString& vmfPath) const {
	for (int i = 0; i < m_table->topLevelItemCount(); ++i) {
		if (m_table->topLevelItem(i)->data(NameColumn, PathRole).toString() == vmfPath) return m_table->topLevelItem(i);
	}
	return nullptr;
}

QString MapsPanel::currentPath() const {
	const auto* item = m_table->currentItem();
	return item ? item->data(NameColumn, PathRole).toString() : QString();
}

QString MapsPanel::settingsKey(const QString& vmfPath) const {
	// Per game and per map file (the key may not contain slashes of its own).
	return QStringLiteral("maps/%1/%2").arg(QString(m_editor->gameKey()).replace('/', '_'), QString(vmfPath).replace('/', '|').replace('\\', '|'));
}

QStringList MapsPanel::maps() const {
	QStringList out;
	for (int i = 0; i < m_table->topLevelItemCount(); ++i) out << m_table->topLevelItem(i)->data(NameColumn, PathRole).toString();
	return out;
}

QStringList MapsPanel::checkedMaps() const {
	QStringList out;
	for (int i = 0; i < m_table->topLevelItemCount(); ++i) {
		const auto* item = m_table->topLevelItem(i);
		if (item->checkState(NameColumn) == Qt::Checked) out << item->data(NameColumn, PathRole).toString();
	}
	return out;
}

void MapsPanel::setChecked(const QString& vmfPath, bool checked) {
	if (auto* item = rowFor(vmfPath)) item->setCheckState(NameColumn, checked ? Qt::Checked : Qt::Unchecked);
}

QString MapsPanel::lastResult(const QString& vmfPath) const {
	return QSettings().value(settingsKey(vmfPath)).toString().section('|', 0, 0);
}

QString MapsPanel::createMap(const QString& name, QString* error) {
	if (m_folder.isEmpty()) {
		if (error) *error = tr("Choose a game first.");
		return {};
	}
	if (!validMapName(name)) {
		if (error) *error = tr("Use only letters, digits, _ and - in map names.");
		return {};
	}
	const QString path = QDir(m_folder).filePath(name + ".vmf");
	if (QFileInfo::exists(path)) {
		if (error) *error = tr("There is already a map called %1.").arg(name);
		return {};
	}
	// An empty map with a sky the game has.
	MapDocument doc;
	if (const auto* game = m_editor->game()) {
		const auto skies = skyboxNames(game->files);
		const std::string current = doc.world.get("skyname");
		if (!skies.empty() && std::find(skies.begin(), skies.end(), current) == skies.end()) doc.world.set("skyname", skies.front());
	}
	QDir().mkpath(m_folder);
	QSaveFile file(path);
	const std::string text = doc.serialize();
	if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), qint64(text.size())) != qint64(text.size()) || !file.commit()) {
		if (error) *error = tr("Could not write %1").arg(path);
		return {};
	}
	refresh();
	return path;
}

QString MapsPanel::duplicateMap(const QString& vmfPath, QString* error) {
	const QFileInfo info(vmfPath);
	QString target;
	for (int n = 1; n < 1000; ++n) {
		target = info.absoluteDir().filePath(QStringLiteral("%1_copy%2.vmf").arg(info.completeBaseName(), n == 1 ? QString() : QString::number(n)));
		if (!QFileInfo::exists(target)) break;
	}
	if (!QFile::copy(vmfPath, target)) {
		if (error) *error = tr("Could not copy %1").arg(info.fileName());
		return {};
	}
	refresh();
	return target;
}

QString MapsPanel::renameMap(const QString& vmfPath, const QString& newName, QString* error) {
	if (!validMapName(newName)) {
		if (error) *error = tr("Use only letters, digits, _ and - in map names.");
		return {};
	}
	const QString target = QFileInfo(vmfPath).absoluteDir().filePath(newName + ".vmf");
	if (QFileInfo::exists(target)) {
		if (error) *error = tr("There is already a map called %1.").arg(newName);
		return {};
	}
	if (m_editor->filePath() == vmfPath) {
		if (error) *error = tr("The map is open in the editor - save it under the new name instead (File > Save as).");
		return {};
	}
	if (!QFile::rename(vmfPath, target)) {
		if (error) *error = tr("Could not rename %1").arg(QFileInfo(vmfPath).fileName());
		return {};
	}
	QSettings().remove(settingsKey(vmfPath));
	refresh();
	return target;
}

bool MapsPanel::deleteMap(const QString& vmfPath, QString* error) {
	if (m_editor->filePath() == vmfPath) {
		if (error) *error = tr("The map is open in the editor.");
		return false;
	}
	QFile file(vmfPath);
	if (!file.moveToTrash()) {
		if (error) *error = tr("Could not move %1 to the trash.").arg(QFileInfo(vmfPath).fileName());
		return false;
	}
	QSettings().remove(settingsKey(vmfPath));
	refresh();
	return true;
}

bool MapsPanel::compileMaps(const QStringList& vmfPaths, bool runAfter) {
	if (vmfPaths.isEmpty() || batchRunning() || !m_compiler || m_compiler->running() || !m_editor->game()) {
		return false;
	}
	m_queue = vmfPaths;
	m_total = static_cast<int>(vmfPaths.size());
	m_compiled = m_failed = 0;
	m_lastOk.clear();
	m_runGame = runAfter;
	m_progress->setValue(0);
	for (int i = 0; i < m_table->topLevelItemCount(); ++i) updateRow(m_table->topLevelItem(i));
	emit compileStarted();
	updateButtons();
	startNext();
	return true;
}

void MapsPanel::cancelBatch() {
	m_queue.clear();
	m_runGame = false;
	if (!m_current.isEmpty() && m_compiler && m_compiler->running()) {
		m_compiler->cancel();   // finished(false) ends the batch
	}
	for (int i = 0; i < m_table->topLevelItemCount(); ++i) updateRow(m_table->topLevelItem(i));
	updateButtons();
}

void MapsPanel::startNext() {
	if (!m_current.isEmpty()) return;
	if (!m_compiler) {
		m_queue.clear();
	}
	if (m_queue.isEmpty()) {
		// Done: optionally play the last map that compiled.
		m_progress->setValue(1000);
		m_batchLabel->setText(tr("%1 compiled, %2 failed").arg(m_compiled).arg(m_failed));
		if (m_runGame && !m_lastOk.isEmpty() && m_compiler && !m_compiler->running() && m_editor->game()) {
			auto options = loadCompileOptions(m_editor->gameKey());
			options.vbsp = options.vvis = options.vrad = options.copy = options.cubemaps = false;
			options.run = true;
			m_compiler->start(m_lastOk, gameDirOf(m_editor), m_editor->gameSettings(), options);
		}
		m_runGame = false;
		updateButtons();
		emit batchFinished(m_compiled, m_failed);
		return;
	}
	if (m_compiler->running()) {
		// Another compile (started from the map) runs: wait for it.
		QTimer::singleShot(500, this, &MapsPanel::startNext);
		return;
	}
	m_current = m_queue.takeFirst();
	m_batchLabel->setText(tr("Compiling %1 (%2 of %3) ...").arg(QFileInfo(m_current).completeBaseName()).arg(m_compiled + m_failed + 1).arg(m_total));
	// The user's compile settings, but no game (and no cubemaps, which start the game) in between.
	auto options = loadCompileOptions(m_editor->gameKey());
	options.run = false;
	options.cubemaps = false;
	m_compiler->start(m_current, gameDirOf(m_editor), m_editor->gameSettings(), options);
}

void MapsPanel::finishCurrent(bool ok) {
	const QString path = m_current;
	m_current.clear();
	QSettings().setValue(settingsKey(path), QStringLiteral("%1|%2").arg(ok ? "ok" : "failed", QDateTime::currentDateTime().toString(Qt::ISODate)));
	if (ok) {
		++m_compiled;
		m_lastOk = path;
	} else {
		++m_failed;
	}
	m_progress->setValue(m_total > 0 ? 1000 * (m_compiled + m_failed) / m_total : 0);
	if (auto* item = rowFor(path)) updateRow(item);
	// Not inside the runner's signal: the next start would run inside its finished().
	QTimer::singleShot(0, this, &MapsPanel::startNext);
}

void MapsPanel::updateButtons() {
	const bool selected = !currentPath().isEmpty();
	const bool busy = batchRunning();
	for (auto* b : {m_open, m_duplicate, m_rename, m_delete}) b->setEnabled(selected);
	m_compileChecked->setEnabled(!busy && m_editor->game());
	m_compileOutdated->setEnabled(!busy && m_editor->game());
	m_cancel->setEnabled(busy);
}

} // namespace sme::app
