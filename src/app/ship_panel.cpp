// ship_panel.cpp - The Ship panel (see ship_panel.h).

#include "ship_panel.h"

#include "editor.h"
#include "ui_helpers.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <fstream>

#include <sme/game.h>
#include <sme/text.h>

namespace sme::app {

namespace {

QString sizeText(std::uint64_t bytes) {
	return QLocale().formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeTraditionalFormat);
}

QString kindText(ShipFile::Kind kind) {
	switch (kind) {
		case ShipFile::Kind::Material: return QObject::tr("material");
		case ShipFile::Kind::Texture: return QObject::tr("texture");
		case ShipFile::Kind::Model: return QObject::tr("model");
		case ShipFile::Kind::Sound: return QObject::tr("sound");
		case ShipFile::Kind::Particles: return QObject::tr("particles");
		case ShipFile::Kind::Soundscape: return QObject::tr("soundscape");
		case ShipFile::Kind::Other: break;
	}
	return QObject::tr("file");
}

/// A card with a small upper-case heading; returns the layout for its content.
QVBoxLayout* card(QVBoxLayout* parent, const QString& title, const QString& subtitle = {}) {
	auto* frame = new QFrame;
	frame->setProperty("role", "card");
	auto* v = new QVBoxLayout(frame);
	v->setContentsMargins(12, 10, 12, 12);
	v->setSpacing(8);
	auto* heading = new QLabel(title.toUpper());
	heading->setStyleSheet("color: #a8a8a8; font-size: 8pt; font-weight: 700; letter-spacing: 1px;");
	v->addWidget(heading);
	if (!subtitle.isEmpty()) {
		auto* text = new QLabel(subtitle);
		text->setWordWrap(true);
		text->setProperty("role", "description");
		v->addWidget(text);
	}
	parent->addWidget(frame);
	return v;
}

} // namespace

ShipPanel::ShipPanel(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	setObjectName("shipPanel");
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* content = new QWidget(scroll->viewport());   // built in place (see MainWindow::createDocks)
	auto* layout = new QVBoxLayout(content);
	layout->setContentsMargins(16, 12, 16, 12);
	layout->setSpacing(12);
	buildMapCard(layout);
	buildModCard(layout);
	buildWorkshopCard(layout);

	auto* logLayout = card(layout, tr("Output"));
	m_log = new QPlainTextEdit;
	m_log->setObjectName("shipLog");
	m_log->setReadOnly(true);
	m_log->setMinimumHeight(110);
	m_log->setPlaceholderText(tr("What the jobs above did shows up here."));
	logLayout->addWidget(m_log);
	layout->addStretch(1);
	scroll->setWidget(content);
	outer->addWidget(scroll);

	connect(&m_scanWatcher, &QFutureWatcher<ScanResult>::finished, this, [this] {
		auto result = m_scanWatcher.result();
		if (result.generation != m_generation) return;   // the game changed meanwhile
		m_deps = std::move(result.deps);
		showDependencies();
		emit scanned();
	});
	connect(&m_taskWatcher, &QFutureWatcher<TaskResult>::finished, this, [this] {
		const auto result = m_taskWatcher.result();
		log(result.message, !result.ok);
		setBusy(false);
		emit taskFinished(result.ok, result.message);
	});
	connect(editor, &Editor::gameAboutToChange, this, [this] {
		// Background readers of the old game's files must stop first.
		++m_generation;
		m_scanWatcher.waitForFinished();
		m_taskWatcher.waitForFinished();
	});
	connect(editor, &Editor::gameChanged, this, &ShipPanel::gameChanged);
	connect(editor, &Editor::documentReplaced, this, [this] {
		if (!m_bspEdited) m_bsp->setText(defaultBsp());
		m_deps = {};
		showDependencies();
	});
	gameChanged();
}

ShipPanel::~ShipPanel() {
	++m_generation;
	m_scanWatcher.waitForFinished();
	m_taskWatcher.waitForFinished();
	if (m_bspzip && m_bspzip->state() != QProcess::NotRunning) {
		m_bspzip->kill();
		m_bspzip->waitForFinished(2000);
	}
}

void ShipPanel::buildMapCard(QVBoxLayout* layout) {
	auto* v = card(layout, tr("Share this map"),
	               tr("What the open map needs, and where it comes from. Players have the base game; everything from your mod's own "
	                  "folder has to travel with the map - packed into the BSP, or next to it in a ZIP."));
	auto* row = new QHBoxLayout;
	row->addWidget(new QLabel(tr("Compiled map")));
	m_bsp = new QLineEdit;
	m_bsp->setObjectName("shipBsp");
	m_bsp->setPlaceholderText(tr("maps/<name>.bsp of the mod (compile the map first)"));
	connect(m_bsp, &QLineEdit::textEdited, this, [this] { m_bspEdited = true; });
	row->addWidget(m_bsp, 1);
	auto* browse = new QPushButton(tr("Browse ..."));
	connect(browse, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Compiled map"), QFileInfo(bspPath()).absolutePath(), tr("Compiled map (*.bsp)"));
		if (!path.isEmpty()) {
			m_bspEdited = true;
			m_bsp->setText(path);
		}
	});
	row->addWidget(browse);
	v->addLayout(row);

	auto* scanRow = new QHBoxLayout;
	auto* scanButton = new QPushButton(tr("Check what the map needs"));
	scanButton->setObjectName("shipScan");
	connect(scanButton, &QPushButton::clicked, this, &ShipPanel::scan);
	scanRow->addWidget(scanButton);
	m_summary = new QLabel;
	m_summary->setObjectName("shipSummary");
	m_summary->setWordWrap(true);
	scanRow->addWidget(m_summary, 1);
	v->addLayout(scanRow);

	m_files = new QTreeWidget;
	m_files->setObjectName("shipFiles");
	m_files->setHeaderLabels({tr("File"), tr("Kind"), tr("Size"), tr("Needed by")});
	m_files->setMinimumHeight(240);
	m_files->setUniformRowHeights(true);
	m_files->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_files->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_files->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	m_files->header()->setSectionResizeMode(3, QHeaderView::Interactive);
	m_files->header()->resizeSection(3, 220);
	v->addWidget(m_files);

	auto* readmeLabel = new QLabel(tr("Readme for players (optional, goes into the ZIP as readme.txt)"));
	readmeLabel->setProperty("role", "description");
	v->addWidget(readmeLabel);
	m_readme = new QPlainTextEdit;
	m_readme->setObjectName("shipReadme");
	m_readme->setMaximumHeight(70);
	m_readme->setPlaceholderText(tr("Map name, how to start it, credits ..."));
	v->addWidget(m_readme);

	auto* buttons = new QHBoxLayout;
	auto* pack = new QPushButton(tr("Pack into BSP"));
	pack->setObjectName("shipPack");
	pack->setToolTip(tr("Puts your mod's own files the map needs into the BSP itself (bspzip), so one file is enough"));
	connect(pack, &QPushButton::clicked, this, &ShipPanel::packIntoBsp);
	auto* zip = new QPushButton(tr("Make share ZIP ..."));
	zip->setObjectName("shipZip");
	zip->setProperty("accent", true);
	zip->setToolTip(tr("maps/<name>.bsp with your mod's own files next to it, ready to unpack into a game folder"));
	connect(zip, &QPushButton::clicked, this, [this] {
		const QString start = QFileInfo(bspPath()).dir().filePath(QFileInfo(bspPath()).completeBaseName() + ".zip");
		const QString path = QFileDialog::getSaveFileName(this, tr("Share ZIP"), start, tr("ZIP (*.zip)"));
		if (!path.isEmpty()) makeShareZip(path);
	});
	buttons->addWidget(pack);
	buttons->addWidget(zip);
	buttons->addStretch(1);
	v->addLayout(buttons);
	m_jobButtons << scanButton << pack << zip;
}

void ShipPanel::buildModCard(QVBoxLayout* layout) {
	auto* v = card(layout, tr("Share the mod"),
	               tr("A ZIP of the whole mod folder for players. Map sources, logs, compile leftovers, screenshots, saved games and "
	                  "your own config.cfg stay out."));
	auto* row = new QHBoxLayout;
	auto* check = new QPushButton(tr("Check mod folder"));
	check->setObjectName("shipModCheck");
	auto* make = new QPushButton(tr("Make mod ZIP ..."));
	make->setObjectName("shipModZip");
	row->addWidget(check);
	row->addWidget(make);
	m_modSummary = new QLabel;
	m_modSummary->setObjectName("shipModSummary");
	m_modSummary->setWordWrap(true);
	row->addWidget(m_modSummary, 1);
	v->addLayout(row);
	m_excluded = new QTreeWidget;
	m_excluded->setObjectName("shipExcluded");
	m_excluded->setHeaderLabels({tr("Stays out"), tr("Why")});
	m_excluded->setRootIsDecorated(false);
	m_excluded->setMaximumHeight(160);
	m_excluded->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_excluded->hide();
	v->addWidget(m_excluded);
	connect(check, &QPushButton::clicked, this, [this] {
		const auto dir = modDir();
		if (dir.empty()) return;
		const auto plan = planModPackage(dir);
		m_modSummary->setText(tr("%n file(s), %1 - %2 left out", nullptr, int(plan.files.size())).arg(sizeText(plan.bytes)).arg(plan.excluded.size()));
		m_excluded->clear();
		for (const auto& [path, why] : plan.excluded) {
			m_excluded->addTopLevelItem(new QTreeWidgetItem({fromPath(path), qs(why)}));
		}
		m_excluded->setVisible(!plan.excluded.empty());
	});
	connect(make, &QPushButton::clicked, this, [this] {
		const auto dir = modDir();
		if (dir.empty()) return;
		const QString start = QDir(fromPath(dir.parent_path())).filePath(fromPath(dir.filename()) + ".zip");
		const QString path = QFileDialog::getSaveFileName(this, tr("Mod ZIP"), start, tr("ZIP (*.zip)"));
		if (!path.isEmpty()) makeModZip(path);
	});
	m_jobButtons << check << make;
}

void ShipPanel::buildWorkshopCard(QVBoxLayout* layout) {
	auto* v = card(layout, tr("Steam Workshop (advanced)"));
	auto* toggle = new QToolButton;
	toggle->setObjectName("shipWorkshopToggle");
	toggle->setText(tr("Show upload settings"));
	toggle->setCheckable(true);
	toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	toggle->setArrowType(Qt::RightArrow);
	v->addWidget(toggle);
	m_workshop = new QWidget;
	m_workshop->setObjectName("shipWorkshop");
	m_workshop->hide();
	connect(toggle, &QToolButton::toggled, this, [this, toggle](bool on) {
		m_workshop->setVisible(on);
		toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
	});
	auto* w = new QVBoxLayout(m_workshop);
	w->setContentsMargins(0, 0, 0, 0);
	auto* note = new QLabel(tr("For games with a Workshop (Half-Life 2 and others). The editor builds the content folder from the share "
	                           "package and writes the item file; Valve's steamcmd uploads it. steamcmd asks for your Steam login "
	                           "itself - the editor never sees or stores a password."));
	note->setWordWrap(true);
	note->setProperty("role", "description");
	w->addWidget(note);
	auto* form = new QFormLayout;
	m_appId = new QSpinBox;
	m_appId->setRange(0, 99999999);
	m_appId->setToolTip(tr("Steam app of the game the item is for (Half-Life 2: 220)"));
	m_publishedId = new QLineEdit;
	m_publishedId->setPlaceholderText(tr("empty = a new item; the item's id to update it"));
	m_title = new QLineEdit;
	m_title->setObjectName("shipTitle");
	m_description = new QPlainTextEdit;
	m_description->setMaximumHeight(80);
	m_changeNote = new QLineEdit;
	m_preview = new QLineEdit;
	m_preview->setPlaceholderText(tr("JPG or PNG, under 1 MB"));
	auto* previewRow = new QHBoxLayout;
	previewRow->addWidget(m_preview, 1);
	auto* previewBrowse = new QPushButton(tr("Browse ..."));
	connect(previewBrowse, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Preview image"), {}, tr("Images (*.jpg *.jpeg *.png)"));
		if (!path.isEmpty()) m_preview->setText(path);
	});
	previewRow->addWidget(previewBrowse);
	m_visibility = new QComboBox;
	m_visibility->addItems({tr("Public"), tr("Friends only"), tr("Private"), tr("Unlisted")});
	m_visibility->setCurrentIndex(2);   // private first: check it, then make it public on the Workshop page
	QSettings settings;
	m_steamcmd = new QLineEdit(settings.value("ship/steamcmd").toString());
	m_steamcmd->setPlaceholderText(tr("path of steamcmd (optional)"));
	connect(m_steamcmd, &QLineEdit::editingFinished, this, [this] { QSettings().setValue("ship/steamcmd", m_steamcmd->text().trimmed()); });
	m_steamUser = new QLineEdit(settings.value("ship/steamUser").toString());
	m_steamUser->setPlaceholderText(tr("Steam account name (no password)"));
	connect(m_steamUser, &QLineEdit::editingFinished, this, [this] { QSettings().setValue("ship/steamUser", m_steamUser->text().trimmed()); });
	form->addRow(tr("Game (app id)"), m_appId);
	form->addRow(tr("Item id"), m_publishedId);
	form->addRow(tr("Title"), m_title);
	form->addRow(tr("Description"), m_description);
	form->addRow(tr("Change note"), m_changeNote);
	form->addRow(tr("Preview image"), previewRow);
	form->addRow(tr("Visibility"), m_visibility);
	form->addRow(tr("steamcmd"), m_steamcmd);
	form->addRow(tr("Steam account"), m_steamUser);
	w->addLayout(form);
	auto* buttons = new QHBoxLayout;
	auto* prepare = new QPushButton(tr("Prepare upload"));
	prepare->setObjectName("shipWorkshopPrepare");
	auto* run = new QPushButton(tr("Run steamcmd"));
	run->setObjectName("shipWorkshopRun");
	buttons->addWidget(prepare);
	buttons->addWidget(run);
	buttons->addStretch(1);
	w->addLayout(buttons);
	m_command = new QLineEdit;
	m_command->setObjectName("shipWorkshopCommand");
	m_command->setReadOnly(true);
	m_command->setPlaceholderText(tr("The steamcmd command shows up here"));
	auto* commandRow = new QHBoxLayout;
	commandRow->addWidget(m_command, 1);
	auto* copy = new QPushButton(tr("Copy"));
	connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_command->text()); });
	commandRow->addWidget(copy);
	w->addLayout(commandRow);
	v->addWidget(m_workshop);
	connect(prepare, &QPushButton::clicked, this, [this] { prepareWorkshop(); });
	connect(run, &QPushButton::clicked, this, [this] {
		const QString command = prepareWorkshop();
		if (command.isEmpty()) return;
		const QString steamcmd = m_steamcmd->text().trimmed();
		if (steamcmd.isEmpty() || !QFileInfo(steamcmd).isFile()) {
			log(tr("Set the path of steamcmd first, or run the command above yourself."), true);
			return;
		}
		// Its own console window (Windows): steamcmd asks for the password and Steam Guard code there.
		const QString vdf = m_command->property("vdf").toString();
		QStringList args{"+login", m_steamUser->text().trimmed(), "+workshop_build_item", vdf, "+quit"};
		if (m_steamUser->text().trimmed().isEmpty()) args = {"+workshop_build_item", vdf, "+quit"};
		if (QProcess::startDetached(steamcmd, args)) {
			log(tr("steamcmd started - log in there; it uploads and closes when done."));
		} else {
			log(tr("Could not start %1").arg(steamcmd), true);
		}
	});
}

std::filesystem::path ShipPanel::modDir() const {
	const auto* game = m_editor->game();
	return game ? game->info.gameDir : std::filesystem::path{};
}

QString ShipPanel::defaultBsp() const {
	const auto dir = modDir();
	const QString map = m_editor->filePath();
	if (dir.empty() || map.isEmpty()) return {};
	return QDir(fromPath(dir / "maps")).filePath(QFileInfo(map).completeBaseName() + ".bsp");
}

QString ShipPanel::bspPath() const {
	return m_bsp->text().trimmed();
}

void ShipPanel::setBspPath(const QString& path) {
	m_bspEdited = true;
	m_bsp->setText(path);
}

void ShipPanel::gameChanged() {
	m_deps = {};
	if (!m_bspEdited) m_bsp->setText(defaultBsp());
	if (const auto* entry = m_editor->gameEntry()) {
		m_appId->setValue(static_cast<int>(entry->baseAppId ? entry->baseAppId : entry->ownerAppId));
	}
	showDependencies();
}

QString ShipPanel::bspzipPath() const {
	const QString vbsp = m_editor->gameSettings().vbsp;
	if (vbsp.isEmpty()) return {};
	const QDir bin = QFileInfo(vbsp).dir();
	for (const char* name : {"bspzip.exe", "bspzip", "bspzip_linux"}) {
		if (QFileInfo(bin.filePath(name)).isFile()) return bin.filePath(name);
	}
	return {};
}

void ShipPanel::setBusy(bool busy) {
	m_busy = busy;
	for (auto* button : m_jobButtons) button->setEnabled(!busy);
}

void ShipPanel::log(const QString& line, bool problem) {
	if (line.isEmpty()) return;
	m_log->appendHtml(problem ? QStringLiteral("<span style='color:#ff8a70'>%1</span>").arg(line.toHtmlEscaped()) : line.toHtmlEscaped());
}

void ShipPanel::scan() {
	const auto* game = m_editor->game();
	if (!game) {
		m_summary->setText(tr("Pick a game or mod first."));
		return;
	}
	m_summary->setText(tr("Checking ..."));
	const int generation = ++m_generation;
	const GameFileSystem* fs = &game->files;
	m_scanWatcher.setFuture(QtConcurrent::run([doc = m_editor->doc(), fs, dir = modDir(), generation] {
		return ScanResult{mapDependencies(doc, *fs, dir), generation};
	}));
}

void ShipPanel::showDependencies() {
	m_files->clear();
	if (m_deps.files.empty()) {
		m_summary->setText(m_editor->game() ? tr("Not checked yet.") : tr("Pick a game or mod first."));
		return;
	}
	struct Group {
		ShipFile::Origin origin;
		QString title, tip;
	};
	const Group groups[] = {
		{ShipFile::Origin::Custom, tr("Your mod"), tr("Players do not have these: they go into the BSP or the share ZIP")},
		{ShipFile::Origin::Missing, tr("Missing"), tr("Nobody has these: the game shows a checkerboard, an error model or silence")},
		{ShipFile::Origin::Shared, tr("Other mod folders"), tr("From folders outside the installed games (shared content); players need them too")},
		{ShipFile::Origin::BaseGame, tr("Base game"), tr("Players already have these")},
	};
	for (const auto& group : groups) {
		const auto count = m_deps.count(group.origin);
		if (count == 0) continue;
		const auto bytes = m_deps.bytes(group.origin);
		auto* top = new QTreeWidgetItem({QStringLiteral("%1  (%2)").arg(group.title).arg(count), QString(), bytes ? sizeText(bytes) : QString()});
		top->setData(0, Qt::UserRole, int(group.origin));
		top->setToolTip(0, group.tip);
		QFont bold = top->font(0);
		bold.setBold(true);
		top->setFont(0, bold);
		if (group.origin == ShipFile::Origin::Missing) top->setForeground(0, QColor(255, 138, 112));
		for (const auto& file : m_deps.files) {
			if (file.origin != group.origin) continue;
			auto* item = new QTreeWidgetItem({qs(file.path), kindText(file.kind), file.size ? sizeText(file.size) : QString(), qs(file.neededBy)});
			item->setToolTip(3, qs(file.neededBy));
			top->addChild(item);
		}
		m_files->addTopLevelItem(top);
		top->setExpanded(group.origin == ShipFile::Origin::Custom || group.origin == ShipFile::Origin::Missing);
	}
	const auto custom = m_deps.count(ShipFile::Origin::Custom), missing = m_deps.count(ShipFile::Origin::Missing);
	QString text = tr("%n file(s) needed: ", nullptr, int(m_deps.files.size())) +
	               tr("%1 from your mod (%2)").arg(custom).arg(sizeText(m_deps.bytes(ShipFile::Origin::Custom)));
	if (missing) text += QStringLiteral(" - <span style='color:#ff8a70'>%1</span>").arg(tr("%n missing", nullptr, int(missing)));
	m_summary->setText(text);
}

void ShipPanel::startTask(const QString& what, std::function<TaskResult()> job) {
	if (m_busy) return;
	setBusy(true);
	log(what);
	m_taskWatcher.setFuture(QtConcurrent::run(std::move(job)));
}

void ShipPanel::makeShareZip(const QString& zipPath) {
	const auto* game = m_editor->game();
	const QString bsp = bspPath();
	if (!game || bsp.isEmpty() || !QFileInfo(bsp).isFile()) {
		const QString message = tr("No compiled map at %1 - compile the map first (Run map).").arg(bsp);
		log(message, true);
		emit taskFinished(false, message);
		return;
	}
	if (m_deps.files.empty()) {
		m_deps = mapDependencies(m_editor->doc(), game->files, modDir());   // not checked yet: now, before packing
		showDependencies();
	}
	const auto entries = sharePackage(toPath(bsp), m_deps, game->files, m_readme->toPlainText().toStdString());
	startTask(tr("Writing %1 ...").arg(QDir::toNativeSeparators(zipPath)), [entries, zip = toPath(zipPath)] {
		std::string error;
		if (!writeZip(zip, entries, &error)) return TaskResult{false, QObject::tr("Share ZIP failed: %1").arg(qs(error))};
		return TaskResult{true, QObject::tr("Share ZIP written: %1 (%n file(s))", nullptr, int(entries.size())).arg(QDir::toNativeSeparators(fromPath(zip)))};
	});
}

void ShipPanel::makeModZip(const QString& zipPath) {
	const auto dir = modDir();
	if (dir.empty()) {
		emit taskFinished(false, tr("Pick a game or mod first."));
		return;
	}
	startTask(tr("Writing %1 ...").arg(QDir::toNativeSeparators(zipPath)), [dir, zip = toPath(zipPath)] {
		const auto plan = planModPackage(dir);
		std::string error;
		if (!writeZip(zip, modPackage(dir, plan), &error)) return TaskResult{false, QObject::tr("Mod ZIP failed: %1").arg(qs(error))};
		return TaskResult{true, QObject::tr("Mod ZIP written: %1 (%2 files, %3 left out)")
		                            .arg(QDir::toNativeSeparators(fromPath(zip)))
		                            .arg(plan.files.size())
		                            .arg(plan.excluded.size())};
	});
}

void ShipPanel::packIntoBsp() {
	const auto* game = m_editor->game();
	const QString bsp = bspPath();
	const QString bspzip = bspzipPath();
	const auto fail = [this](const QString& message) {
		log(message, true);
		emit taskFinished(false, message);
	};
	if (!game) return fail(tr("Pick a game or mod first."));
	if (bsp.isEmpty() || !QFileInfo(bsp).isFile()) return fail(tr("No compiled map at %1 - compile the map first (Run map).").arg(bsp));
	if (bspzip.isEmpty()) return fail(tr("bspzip was not found next to the compile tools (game settings, VBSP)."));
	if (m_busy) return;
	if (m_deps.files.empty()) {
		m_deps = mapDependencies(m_editor->doc(), game->files, modDir());
		showDependencies();
	}
	// Files from the mod's own VPKs have no file on disk: they go to a folder of their own first.
	const QString staging = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("sme_ship_staging");
	QDir(staging).removeRecursively();
	std::vector<std::pair<std::string, std::filesystem::path>> list;
	for (const auto& file : m_deps.files) {
		if (file.origin != ShipFile::Origin::Custom) continue;
		if (auto disk = fileOnDisk(game->files, file)) {
			list.push_back({file.path, *disk});
		} else if (const auto data = game->files.read(file.path)) {
			const auto target = toPath(staging) / toNativePath(file.path);
			std::filesystem::create_directories(target.parent_path());
			std::ofstream(target, std::ios::binary).write(reinterpret_cast<const char*>(data->data()), static_cast<std::streamsize>(data->size()));
			list.push_back({file.path, target});
		}
	}
	if (list.empty()) return fail(tr("The map needs no files of your mod - nothing to pack."));
	const QString listFile = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("sme_bspzip_files.txt");
	{
		std::ofstream out(toPath(listFile), std::ios::binary | std::ios::trunc);
		out << bspzipFileList(list);
	}
	if (!m_bspzip) {
		m_bspzip = new QProcess(this);
		m_bspzip->setProcessChannelMode(QProcess::MergedChannels);
		connect(m_bspzip, &QProcess::readyRead, this, [this] {
			for (const auto& line : QString::fromLocal8Bit(m_bspzip->readAll()).split('\n', Qt::SkipEmptyParts)) log(line.trimmed());
		});
		connect(m_bspzip, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
			const bool ok = status == QProcess::NormalExit && code == 0;
			const QString message = ok ? tr("Packed into %1.").arg(QDir::toNativeSeparators(bspPath())) : tr("bspzip failed (exit code %1).").arg(code);
			log(message, !ok);
			setBusy(false);
			emit taskFinished(ok, message);
		});
		connect(m_bspzip, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
			if (error != QProcess::FailedToStart) return;
			const QString message = tr("bspzip could not be started: %1").arg(m_bspzip->errorString());
			log(message, true);
			setBusy(false);
			emit taskFinished(false, message);
		});
	}
	setBusy(true);
	// -addorupdatelist keeps what VBSP packed already (cubemaps, patched materials).
	const QStringList args{"-addorupdatelist", QDir::toNativeSeparators(bsp), QDir::toNativeSeparators(listFile), QDir::toNativeSeparators(bsp), "-game",
	                       QDir::toNativeSeparators(fromPath(modDir()))};
	log(QStringLiteral("> \"%1\" %2").arg(QDir::toNativeSeparators(bspzip), args.join(' ')));
	m_bspzip->start(bspzip, args);
}

QString ShipPanel::prepareWorkshop() {
	const auto* game = m_editor->game();
	const QString bsp = bspPath();
	if (!game || bsp.isEmpty() || !QFileInfo(bsp).isFile()) {
		log(tr("No compiled map at %1 - compile the map first (Run map).").arg(bsp), true);
		return {};
	}
	if (m_title->text().trimmed().isEmpty()) {
		log(tr("Give the Workshop item a title first."), true);
		return {};
	}
	if (m_deps.files.empty()) {
		m_deps = mapDependencies(m_editor->doc(), game->files, modDir());
		showDependencies();
	}
	const QString name = QFileInfo(bsp).completeBaseName();
	const QDir root(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("workshop/" + name));
	QDir(root.filePath("content")).removeRecursively();
	std::string error;
	if (!writeFolder(toPath(root.filePath("content")), sharePackage(toPath(bsp), m_deps, game->files), &error)) {
		log(tr("Could not write the content folder: %1").arg(qs(error)), true);
		return {};
	}
	WorkshopItem item;
	item.appId = static_cast<std::uint32_t>(m_appId->value());
	item.publishedFileId = m_publishedId->text().trimmed().toULongLong();
	item.contentFolder = toPath(root.filePath("content"));
	item.previewFile = toPath(m_preview->text().trimmed());
	item.visibility = m_visibility->currentIndex();
	item.title = m_title->text().trimmed().toStdString();
	item.description = m_description->toPlainText().toStdString();
	item.changeNote = m_changeNote->text().trimmed().toStdString();
	const QString vdf = root.filePath(name + ".vdf");
	{
		std::ofstream out(toPath(vdf), std::ios::binary | std::ios::trunc);
		out << workshopItemVdf(item);
	}
	const QString user = m_steamUser->text().trimmed();
	const QString steamcmd = m_steamcmd->text().trimmed().isEmpty() ? QStringLiteral("steamcmd") : QDir::toNativeSeparators(m_steamcmd->text().trimmed());
	const QString command = QStringLiteral("\"%1\" +login %2 +workshop_build_item \"%3\" +quit")
	                            .arg(steamcmd, user.isEmpty() ? tr("<account>") : user, QDir::toNativeSeparators(vdf));
	m_command->setText(command);
	m_command->setProperty("vdf", QDir::toNativeSeparators(vdf));
	log(tr("Workshop item prepared: %1").arg(QDir::toNativeSeparators(vdf)));
	return command;
}

} // namespace sme::app
