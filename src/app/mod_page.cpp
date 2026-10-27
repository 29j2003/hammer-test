// mod_page.cpp - Mod page of the side bar (see mod_page.h).

#include "mod_page.h"

#include "editor.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <sme/game.h>

namespace sme::app {

namespace {

void openInSystem(const QString& path) {
	if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QGroupBox* section(const QString& title, QWidget* content, QWidget* buttons = nullptr) {
	auto* box = new QGroupBox(title);
	auto* v = new QVBoxLayout(box);
	v->addWidget(content, 1);
	if (buttons) v->addWidget(buttons);
	return box;
}

} // namespace

ModPage::ModPage(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	setObjectName("modPage");
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(16, 12, 16, 12);
	v->setSpacing(10);
	m_title = new QLabel;
	m_title->setObjectName("pageTitle");
	m_details = new QLabel;
	m_details->setObjectName("modDetails");
	m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_details->setWordWrap(true);
	v->addWidget(m_title);
	v->addWidget(m_details);

	auto* folderButtons = new QWidget;
	auto* fb = new QHBoxLayout(folderButtons);
	fb->setContentsMargins(0, 0, 0, 0);
	const auto button = [&](QHBoxLayout* layout, const QString& text, auto&& slot) {
		auto* b = new QPushButton(text);
		connect(b, &QPushButton::clicked, this, slot);
		layout->addWidget(b);
		return b;
	};
	button(fb, tr("Open mod folder"), [this] {
		if (const auto* entry = m_editor->gameEntry()) openInSystem(fromPath(entry->gameDir));
	});
	button(fb, tr("Edit gameinfo.txt"), [this] {
		if (const auto* entry = m_editor->gameEntry()) openInSystem(fromPath(entry->gameDir / "gameinfo.txt"));
	});
	button(fb, tr("Compile and game settings ..."), [this] { emit compileSettingsRequested(); });
	button(fb, tr("New mod ..."), [this] { emit newModRequested(); });
	fb->addStretch(1);
	v->addWidget(folderButtons);

	auto* grid = new QGridLayout;
	grid->setSpacing(10);
	m_maps = new QListWidget;
	m_maps->setObjectName("modMaps");
	m_maps->setToolTip(tr("Double-click opens the map"));
	connect(m_maps, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) { emit openMapRequested(item->data(Qt::UserRole).toString()); });
	auto* mapButtons = new QWidget;
	auto* mb = new QHBoxLayout(mapButtons);
	mb->setContentsMargins(0, 0, 0, 0);
	button(mb, tr("Open"), [this] {
		if (auto* item = m_maps->currentItem()) emit openMapRequested(item->data(Qt::UserRole).toString());
	});
	button(mb, tr("Open map folder"), [this] { openInSystem(m_mapDir); });
	mb->addStretch(1);
	grid->addWidget(section(tr("Maps"), m_maps, mapButtons), 0, 0, 2, 1);

	m_mounts = new QTreeWidget;
	m_mounts->setObjectName("modMounts");
	m_mounts->setHeaderLabels({tr("Search path"), tr("Found"), tr("Folder or VPK")});
	m_mounts->setRootIsDecorated(false);
	m_mounts->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_mounts->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	grid->addWidget(section(tr("Content (search paths of gameinfo.txt, first wins)"), m_mounts), 0, 1);

	m_fgds = new QTreeWidget;
	m_fgds->setObjectName("modFgds");
	m_fgds->setHeaderLabels({tr("Entity definitions (FGD)"), tr("Folder")});
	m_fgds->setRootIsDecorated(false);
	m_fgds->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	grid->addWidget(section(tr("Entities"), m_fgds), 1, 1);
	grid->setColumnStretch(0, 1);
	grid->setColumnStretch(1, 2);
	v->addLayout(grid, 1);

	connect(editor, &Editor::gameChanged, this, &ModPage::refresh);
	refresh();
}

void ModPage::refresh() {
	m_maps->clear();
	m_mounts->clear();
	m_fgds->clear();
	const auto* game = m_editor->game();
	const auto* entry = m_editor->gameEntry();
	if (!game || !entry) {
		m_title->setText(tr("No game chosen"));
		m_details->setText(tr("Pick a game or mod at the top (Game)."));
		m_mapDir.clear();
		return;
	}
	m_title->setText(QString::fromStdString(entry->label.empty() ? entry->title : entry->label));
	QStringList lines;
	lines << tr("Folder: %1").arg(QDir::toNativeSeparators(fromPath(entry->gameDir)));
	if (!entry->appName.empty()) lines << tr("Based on: %1 (app %2)").arg(QString::fromStdString(entry->appName)).arg(entry->baseAppId);
	if (!game->info.type.empty()) lines << tr("Type: %1").arg(QString::fromStdString(game->info.type));
	QStringList traits;
	if (entry->isSourceMod) traits << tr("source mod");
	if (entry->usesMapbase) traits << QStringLiteral("Mapbase");
	if (m_editor->hasFidelity()) traits << QStringLiteral("mapbase-fidelity");
	if (!traits.isEmpty()) lines << tr("Uses: %1").arg(traits.join(QStringLiteral(", ")));
	m_details->setText(lines.join('\n'));

	// Maps: the .vmf files in the map folder (and below it).
	m_mapDir = m_editor->gameSettings().mapDir;
	QStringList maps;
	if (!m_mapDir.isEmpty() && QFileInfo(m_mapDir).isDir()) {
		QDirIterator it(m_mapDir, {QStringLiteral("*.vmf")}, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext() && maps.size() < 2000) maps << it.next();
	}
	maps.sort(Qt::CaseInsensitive);
	for (const auto& path : maps) {
		auto* item = new QListWidgetItem(QDir(m_mapDir).relativeFilePath(path));
		item->setData(Qt::UserRole, path);
		item->setToolTip(QDir::toNativeSeparators(path));
		m_maps->addItem(item);
	}
	if (maps.isEmpty()) {
		auto* item = new QListWidgetItem(tr("No maps in %1").arg(QDir::toNativeSeparators(m_mapDir)));
		item->setFlags(Qt::NoItemFlags);
		m_maps->addItem(item);
	}

	for (const auto& mount : game->files.mounts()) {
		QStringList ids;
		for (const auto& id : mount.pathIds) ids << QString::fromStdString(id);
		auto* item = new QTreeWidgetItem({ids.join('+'), mount.present ? tr("yes") : tr("no"), QDir::toNativeSeparators(fromPath(mount.path))});
		if (!mount.present) {
			item->setForeground(1, QColor(240, 120, 90));
			item->setToolTip(1, QString::fromStdString(mount.problem));
		}
		item->setToolTip(2, QString::fromStdString(mount.source));
		m_mounts->addTopLevelItem(item);
	}
	for (const auto& file : game->entities.files()) {
		m_fgds->addTopLevelItem(new QTreeWidgetItem({QString::fromStdString(file.filename().string()), QDir::toNativeSeparators(fromPath(file.parent_path()))}));
	}
	auto* count = new QTreeWidgetItem({tr("%n entity class(es)", nullptr, int(game->entities.size())), QString()});
	count->setForeground(0, QColor(150, 150, 150));
	m_fgds->addTopLevelItem(count);
}

} // namespace sme::app
