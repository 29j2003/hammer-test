// asset_browser.cpp - Material/model/sound/entity browser (see asset_browser.h).

#include "asset_browser.h"

#include "editor.h"
#include "material_cache.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QStringListModel>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/text.h>

namespace sme::app {

namespace {

constexpr int THUMB = 96;
constexpr int RECENT_THUMB = 48;

/// Strip of recently used materials; items can be dragged like browser items.
class RecentList : public QListWidget {
public:
	using QListWidget::QListWidget;

protected:
	QStringList mimeTypes() const override { return {"application/x-sme-material"}; }
	QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
		if (items.isEmpty()) return nullptr;
		auto* mime = new QMimeData;
		mime->setData("application/x-sme-material", items.first()->data(Qt::UserRole).toString().toUtf8());
		return mime;
	}
};

QString categoryText(const MaterialCategory& c) {
	QStringList parts;
	if (c.brush) parts << QObject::tr("brush");
	if (c.blend) parts << QObject::tr("blend");
	if (c.model) parts << QObject::tr("model");
	if (c.tool) parts << QObject::tr("tool");
	if (c.translucent) parts << QObject::tr("see-through");
	if (c.water) parts << QObject::tr("water");
	if (c.decal) parts << QObject::tr("decal/overlay");
	if (c.sky) parts << QObject::tr("sky");
	return parts.join(", ");
}

QListView* makeIconView(QWidget* parent) {
	auto* view = new QListView(parent);
	view->setViewMode(QListView::IconMode);
	view->setIconSize(QSize(THUMB, THUMB));
	view->setGridSize(QSize(THUMB + 18, THUMB + 34));
	view->setResizeMode(QListView::Adjust);
	view->setMovement(QListView::Static);
	view->setUniformItemSizes(true);
	view->setWordWrap(true);
	view->setDragEnabled(true);
	view->setDragDropMode(QAbstractItemView::DragOnly);
	view->setSelectionMode(QAbstractItemView::SingleSelection);
	view->setLayoutMode(QListView::Batched);
	view->setBatchSize(200);
	return view;
}

} // namespace

// --- Material list ------------------------------------------------------------------

MaterialListModel::MaterialListModel(MaterialCache* thumbnails, QObject* parent) : QAbstractListModel(parent), m_thumbnails(thumbnails) {}

void MaterialListModel::setItems(std::vector<Item> items) {
	beginResetModel();
	m_items = std::move(items);
	m_rows.clear();
	for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
		m_rows.insert(m_items[static_cast<std::size_t>(i)].name, i);
	}
	endResetModel();
}

int MaterialListModel::rowCount(const QModelIndex& parent) const {
	return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant MaterialListModel::data(const QModelIndex& index, int role) const {
	if (!index.isValid() || index.row() >= static_cast<int>(m_items.size())) {
		return {};
	}
	const auto& item = m_items[static_cast<std::size_t>(index.row())];
	switch (role) {
		case Qt::DisplayRole: {
			const auto slash = item.name.lastIndexOf('/');
			return slash >= 0 ? item.name.mid(slash + 1) : item.name;
		}
		case Qt::ToolTipRole:
			return QStringLiteral("<b>%1</b><br>%2<br>%3").arg(item.name, item.shader, categoryText(item.category));
		case Qt::DecorationRole: {
			// Thumbnails load lazily, only for rows the view actually paints.
			if (const auto info = const_cast<MaterialCache*>(m_thumbnails)->get(item.name)) {
				const QImage& image = info->image.isNull() ? MaterialCache::checkerboard() : info->image;
				return QPixmap::fromImage(image.scaled(THUMB, THUMB, Qt::KeepAspectRatio, Qt::SmoothTransformation));
			}
			QPixmap placeholder(THUMB, THUMB);
			placeholder.fill(QColor(45, 47, 52));
			return placeholder;
		}
		case Qt::UserRole:
			return item.name;
		default:
			return {};
	}
}

Qt::ItemFlags MaterialListModel::flags(const QModelIndex& index) const {
	return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList MaterialListModel::mimeTypes() const {
	return {"application/x-sme-material"};
}

QMimeData* MaterialListModel::mimeData(const QModelIndexList& indexes) const {
	if (indexes.isEmpty()) {
		return nullptr;
	}
	auto* mime = new QMimeData;
	const QString name = data(indexes.first(), Qt::UserRole).toString();
	mime->setData("application/x-sme-material", name.toUpper().toUtf8());
	mime->setText(name);
	return mime;
}

void MaterialListModel::thumbnailReady(const QString& name) {
	if (const auto it = m_rows.constFind(name); it != m_rows.constEnd()) {
		const auto idx = index(*it);
		emit dataChanged(idx, idx, {Qt::DecorationRole});
	}
}

void MaterialFilter::setText(const QString& text) {
	m_words = text.toLower().split(' ', Qt::SkipEmptyParts);
	invalidateFilter();
}

void MaterialFilter::setCategory(Category category) {
	m_category = category;
	invalidateFilter();
}

void MaterialFilter::setUsed(const QSet<QString>& used) {
	m_used = used;
	if (m_category == Category::UsedInMap) {
		invalidateFilter();
	}
}

bool MaterialFilter::filterAcceptsRow(int row, const QModelIndex&) const {
	const auto* model = static_cast<const MaterialListModel*>(sourceModel());
	const auto& item = model->items()[static_cast<std::size_t>(row)];
	for (const auto& word : m_words) {
		if (!item.name.contains(word)) {
			return false;
		}
	}
	const auto& c = item.category;
	switch (m_category) {
		case Category::All: return true;
		case Category::Brush: return c.brush && !c.tool;
		case Category::Blend: return c.blend;
		case Category::Model: return c.model;
		case Category::Tool: return c.tool;
		case Category::Translucent: return c.translucent;
		case Category::Water: return c.water;
		case Category::Decal: return c.decal;
		case Category::Sky: return c.sky;
		case Category::UsedInMap: return m_used.contains(item.name);
	}
	return true;
}

// --- Browser widget -------------------------------------------------------------------

AssetBrowser::AssetBrowser(Editor* editor, QWidget* parent)
	: QWidget(parent), m_editor(editor), m_thumbnails(new MaterialCache(MaterialCache::Size::Thumbnail, this)) {
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_tabs = new QTabWidget(this);
	layout->addWidget(m_tabs);

	// Materials
	{
		auto* page = new QWidget;
		auto* v = new QVBoxLayout(page);
		v->setContentsMargins(4, 4, 4, 4);
		auto* row = new QHBoxLayout;
		m_materialSearch = new QLineEdit;
		m_materialSearch->setPlaceholderText(tr("Search materials (words: concrete floor) ..."));
		m_materialSearch->setClearButtonEnabled(true);
		m_materialCategory = new QComboBox;
		m_materialCategory->addItem(tr("Brush materials"), int(MaterialFilter::Category::Brush));
		m_materialCategory->addItem(tr("Blend (displacements)"), int(MaterialFilter::Category::Blend));
		m_materialCategory->addItem(tr("Model materials"), int(MaterialFilter::Category::Model));
		m_materialCategory->addItem(tr("Tool textures"), int(MaterialFilter::Category::Tool));
		m_materialCategory->addItem(tr("See-through (glass, fences)"), int(MaterialFilter::Category::Translucent));
		m_materialCategory->addItem(tr("Water"), int(MaterialFilter::Category::Water));
		m_materialCategory->addItem(tr("Decals / overlays"), int(MaterialFilter::Category::Decal));
		m_materialCategory->addItem(tr("Skybox"), int(MaterialFilter::Category::Sky));
		m_materialCategory->addItem(tr("Used in this map"), int(MaterialFilter::Category::UsedInMap));
		m_materialCategory->addItem(tr("All"), int(MaterialFilter::Category::All));
		row->addWidget(m_materialSearch, 1);
		row->addWidget(m_materialCategory);
		v->addLayout(row);
		m_recent = new RecentList;
		m_recent->setViewMode(QListView::IconMode);
		m_recent->setFlow(QListView::LeftToRight);
		m_recent->setWrapping(false);
		m_recent->setIconSize(QSize(RECENT_THUMB, RECENT_THUMB));
		m_recent->setFixedHeight(RECENT_THUMB + 24);
		m_recent->setDragEnabled(true);
		m_recent->setDragDropMode(QAbstractItemView::DragOnly);
		m_recent->setToolTip(tr("Recently used - click to make current, double-click to apply, drag onto a face"));
		v->addWidget(m_recent);
		connect(m_recent, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { m_editor->setCurrentMaterial(item->data(Qt::UserRole).toString()); });
		connect(m_recent, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { m_editor->applyMaterialToSelection(item->data(Qt::UserRole).toString()); });
		connect(m_editor, &Editor::recentMaterialsChanged, this, &AssetBrowser::updateRecent);
		m_materialModel = new MaterialListModel(m_thumbnails, this);
		m_materialFilter = new MaterialFilter(this);
		m_materialFilter->setSourceModel(m_materialModel);
		m_materialView = makeIconView(page);
		m_materialView->setModel(m_materialFilter);
		v->addWidget(m_materialView, 1);
		m_materialInfo = new QLabel(tr("No game loaded"));
		m_materialInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
		v->addWidget(m_materialInfo);
		m_tabs->addTab(page, tr("Materials"));

		connect(m_materialSearch, &QLineEdit::textChanged, m_materialFilter, &MaterialFilter::setText);
		connect(m_materialCategory, &QComboBox::currentIndexChanged, this, [this] {
			m_materialFilter->setCategory(static_cast<MaterialFilter::Category>(m_materialCategory->currentData().toInt()));
			m_materialInfo->setText(tr("%1 of %2 materials").arg(m_materialFilter->rowCount()).arg(m_materialModel->rowCount()));
		});
		connect(m_thumbnails, &MaterialCache::materialReady, m_materialModel, &MaterialListModel::thumbnailReady);
		connect(m_thumbnails, &MaterialCache::materialReady, this, [this](const QString& name) {
			if (m_editor->recentMaterials().contains(name.toUpper())) updateRecent();
		});
		connect(m_materialView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
			showMaterialInfo(index);
			if (index.isValid()) {
				m_editor->setCurrentMaterial(index.data(Qt::UserRole).toString().toUpper());
				emit m_editor->message(tr("Current material: %1 - Shift+T applies it, Ctrl+right mouse paints it")
				                           .arg(index.data(Qt::UserRole).toString()));
			}
		});
		connect(m_materialView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
			const QString name = index.data(Qt::UserRole).toString().toUpper();
			m_editor->setCurrentMaterial(name);
			if (!m_editor->selection().empty()) {
				m_editor->applyMaterialToSelection(name);
			}
		});
	}

	// Models
	{
		auto* page = new QWidget;
		auto* v = new QVBoxLayout(page);
		v->setContentsMargins(4, 4, 4, 4);
		m_modelSearch = new QLineEdit;
		m_modelSearch->setPlaceholderText(tr("Search models ..."));
		m_modelSearch->setClearButtonEnabled(true);
		v->addWidget(m_modelSearch);
		auto* model = new QStringListModel(this);
		auto* proxy = new QSortFilterProxyModel(this);
		proxy->setSourceModel(model);
		proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
		m_modelView = new QListView;
		m_modelView->setModel(proxy);
		m_modelView->setUniformItemSizes(true);
		v->addWidget(m_modelView, 1);
		m_modelInfo = new QLabel;
		m_modelInfo->setWordWrap(true);
		m_modelInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
		v->addWidget(m_modelInfo);
		m_tabs->addTab(page, tr("Models"));
		connect(m_modelSearch, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
		connect(m_modelView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
			if (!index.isValid() || !m_editor->game()) {
				return;
			}
			const auto path = index.data().toString().toStdString();
			std::string error;
			const auto sequences = modelSequences(m_editor->game()->files, path, &error);
			QStringList names;
			for (std::size_t i = 0; i < sequences.size() && i < 40; ++i) {
				names << QString::fromStdString(sequences[i]);
			}
			m_modelInfo->setText(tr("<b>%1</b><br>%2 animations: %3%4<br><i>Double-click: use on the selected entity</i>")
			                         .arg(index.data().toString())
			                         .arg(sequences.size())
			                         .arg(names.join(", "))
			                         .arg(sequences.size() > 40 ? QStringLiteral(", ...") : QString()));
		});
		connect(m_modelView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
			const auto& selection = m_editor->selection();
			if (selection.entities.empty()) {
				QApplication::clipboard()->setText(index.data().toString());
				emit m_editor->message(tr("Model path copied (select an entity to set it directly)"));
				return;
			}
			m_editor->commit(edit::setKeyValue(m_editor->doc(), selection.entities, "model", index.data().toString().toStdString()));
		});
		connect(this, &AssetBrowser::materialsIndexed, this, [this, model] { model->setStringList(m_models); });
	}

	// Sounds
	{
		auto* page = new QWidget;
		auto* v = new QVBoxLayout(page);
		v->setContentsMargins(4, 4, 4, 4);
		m_soundSearch = new QLineEdit;
		m_soundSearch->setPlaceholderText(tr("Search sounds (sound script names and files) ..."));
		m_soundSearch->setClearButtonEnabled(true);
		v->addWidget(m_soundSearch);
		auto* model = new QStringListModel(this);
		auto* proxy = new QSortFilterProxyModel(this);
		proxy->setSourceModel(model);
		proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
		m_soundView = new QListView;
		m_soundView->setModel(proxy);
		m_soundView->setUniformItemSizes(true);
		v->addWidget(m_soundView, 1);
		auto* hint = new QLabel(tr("Double-click: copy the name (and set it on a selected ambient_generic)"));
		v->addWidget(hint);
		m_tabs->addTab(page, tr("Sounds"));
		connect(m_soundSearch, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
		connect(m_soundView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
			QApplication::clipboard()->setText(index.data().toString());
			const auto& selection = m_editor->selection();
			if (selection.entities.size() == 1) {
				if (const auto* e = m_editor->doc().findEntity(selection.entities.front()); e && e->classname() == "ambient_generic") {
					m_editor->commit(edit::setKeyValue(m_editor->doc(), selection.entities, "message", index.data().toString().toStdString()));
				}
			}
		});
		connect(this, &AssetBrowser::materialsIndexed, this, [this, model] { model->setStringList(m_sounds); });
	}

	// Entities
	{
		auto* page = new QWidget;
		auto* v = new QVBoxLayout(page);
		v->setContentsMargins(4, 4, 4, 4);
		m_entitySearch = new QLineEdit;
		m_entitySearch->setPlaceholderText(tr("Search entity classes ..."));
		m_entitySearch->setClearButtonEnabled(true);
		v->addWidget(m_entitySearch);
		m_entityTree = new QTreeWidget;
		m_entityTree->setHeaderHidden(true);
		m_entityTree->setDragEnabled(true);
		v->addWidget(m_entityTree, 1);
		m_entityInfo = new QLabel;
		m_entityInfo->setWordWrap(true);
		v->addWidget(m_entityInfo);
		m_tabs->addTab(page, tr("Entities"));
		connect(m_entitySearch, &QLineEdit::textChanged, this, [this](const QString& text) {
			for (int i = 0; i < m_entityTree->topLevelItemCount(); ++i) {
				auto* group = m_entityTree->topLevelItem(i);
				int visible = 0;
				for (int j = 0; j < group->childCount(); ++j) {
					const bool show = text.isEmpty() || group->child(j)->text(0).contains(text, Qt::CaseInsensitive);
					group->child(j)->setHidden(!show);
					visible += show ? 1 : 0;
				}
				group->setHidden(visible == 0);
				group->setExpanded(!text.isEmpty());
			}
		});
		connect(m_entityTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
			if (!item || item->childCount() > 0 || !m_editor->entities()) {
				return;
			}
			const auto* cls = m_editor->entities()->find(item->text(0).toStdString());
			if (!cls) {
				return;
			}
			m_editor->setCurrentEntityClass(item->text(0));
			m_entityInfo->setText(QStringLiteral("<b>%1</b> (%2)<br>%3")
			                          .arg(item->text(0), cls->isSolid() ? tr("brush entity: select brushes, Ctrl+T") : tr("point entity: Shift+E, click to place"),
			                               QString::fromStdString(cls->description).left(400).toHtmlEscaped()));
		});
		connect(m_entityTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
			if (item && item->childCount() == 0) {
				m_editor->setCurrentEntityClass(item->text(0));
				m_editor->setTool(Editor::Tool::Entity);
			}
		});
	}

	connect(editor, &Editor::gameAboutToChange, this, [this] {
		++m_generation;
		m_indexWatcher.waitForFinished();
		m_thumbnails->setFileSystem(nullptr);
	});
	connect(editor, &Editor::gameChanged, this, &AssetBrowser::reload);
	connect(editor, &Editor::documentChanged, this, &AssetBrowser::updateUsedMaterials);
	connect(&m_indexWatcher, &QFutureWatcherBase::finished, this, [this] {
		if (m_indexWatcher.future().resultCount() == 0 || m_indexWatcher.isCanceled()) {
			return;
		}
		auto index = m_indexWatcher.result();
		m_models = std::move(index.models);
		m_sounds = std::move(index.sounds);
		m_materialModel->setItems(std::move(index.materials));
		m_materialInfo->setText(tr("%1 of %2 materials - drag onto a face, double-click applies to the selection")
		                            .arg(m_materialFilter->rowCount())
		                            .arg(m_materialModel->rowCount()));
		updateUsedMaterials();
		emit materialsIndexed();
	});
}

AssetBrowser::~AssetBrowser() {
	m_indexWatcher.waitForFinished();
}

void AssetBrowser::showMaterials() {
	m_tabs->setCurrentIndex(0);
}

std::vector<std::string> AssetBrowser::materialNames() const {
	std::vector<std::string> out;
	for (const auto& item : m_materialModel->items()) {
		out.push_back(item.name.toStdString());
	}
	return out;
}

void AssetBrowser::reload() {
	auto* game = m_editor->game();
	m_thumbnails->setFileSystem(game ? &game->files : nullptr);
	m_materialModel->setItems({});
	updateRecent();
	m_entityTree->clear();
	if (!game) {
		return;
	}
	m_materialInfo->setText(tr("Indexing materials, models and sounds ..."));

	// Entity classes grouped by prefix (npc_, prop_, func_, ...).
	QMap<QString, QTreeWidgetItem*> groups;
	for (const auto* cls : game->entities.placeable()) {
		const QString name = QString::fromStdString(cls->name);
		const auto underscore = name.indexOf('_');
		const QString group = underscore > 0 ? name.left(underscore) : tr("other");
		if (!groups.contains(group)) {
			groups.insert(group, new QTreeWidgetItem(QStringList(group)));
		}
		auto* item = new QTreeWidgetItem(groups[group], QStringList(name));
		item->setToolTip(0, QString::fromStdString(cls->description).left(300));
	}
	for (auto* group : groups) {
		group->setText(0, QStringLiteral("%1 (%2)").arg(group->text(0)).arg(group->childCount()));
		m_entityTree->addTopLevelItem(group);
	}

	// Materials, models and sounds are listed and classified in a background thread.
	const GameFileSystem* fs = &game->files;
	const int generation = ++m_generation;
	m_indexWatcher.setFuture(QtConcurrent::run([this, fs, generation] {
		AssetIndex index;
		auto& items = index.materials;
		const auto paths = fs->list("materials", ".vmt");
		items.reserve(paths.size());
		for (const auto& path : paths) {
			if (generation != m_generation) {
				return AssetIndex{};
			}
			MaterialListModel::Item item;
			item.name = QString::fromStdString(normalizeMaterialName(path));
			if (const auto material = loadMaterial(*fs, path)) {
				item.shader = QString::fromStdString(material->shader);
				item.category = classifyMaterial(*material);
				item.classified = true;
			}
			items.push_back(std::move(item));
		}
		for (const auto& model : fs->list("models", ".mdl")) {
			index.models << QString::fromStdString(model);
		}
		for (const auto& name : soundScriptNames(*fs)) {
			index.sounds << QString::fromStdString(name);
		}
		for (const char* extension : {".wav", ".mp3"}) {
			for (const auto& file : fs->list("sound", extension)) {
				index.sounds << QString::fromStdString(file.substr(6));
			}
		}
		return index;
	}));
}

void AssetBrowser::updateRecent() {
	m_recent->clear();
	m_recent->setVisible(!m_editor->recentMaterials().isEmpty());
	for (const auto& name : m_editor->recentMaterials()) {
		auto* item = new QListWidgetItem(name.section('/', -1).toLower(), m_recent);
		item->setData(Qt::UserRole, name);
		item->setToolTip(name);
		QPixmap icon(RECENT_THUMB, RECENT_THUMB);
		icon.fill(QColor(45, 47, 52));
		if (const auto info = m_thumbnails->get(name)) {
			const QImage& image = info->image.isNull() ? MaterialCache::checkerboard() : info->image;
			icon = QPixmap::fromImage(image.scaled(RECENT_THUMB, RECENT_THUMB, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		}
		item->setIcon(icon);
		if (name == m_editor->currentMaterial()) {
			item->setSelected(true);
		}
	}
}

void AssetBrowser::updateUsedMaterials() {
	QSet<QString> used;
	const auto add = [&](const Entity& entity) {
		for (const auto& solid : entity.solids) {
			for (const auto& side : solid.sides) {
				used.insert(normalizedMaterialName(QString::fromStdString(side.material)));
			}
		}
	};
	add(m_editor->doc().world);
	for (const auto& entity : m_editor->doc().entities) {
		add(entity);
	}
	m_materialFilter->setUsed(used);
}

void AssetBrowser::showMaterialInfo(const QModelIndex& index) {
	if (!index.isValid()) {
		return;
	}
	const QString name = index.data(Qt::UserRole).toString();
	const auto row = m_materialFilter->mapToSource(index).row();
	const auto& item = m_materialModel->items()[static_cast<std::size_t>(row)];
	QString size;
	if (const auto info = m_thumbnails->get(name); info && !info->image.isNull()) {
		size = QStringLiteral(" - preview %1x%2").arg(info->image.width()).arg(info->image.height());
	}
	m_materialInfo->setText(QStringLiteral("<b>%1</b> - %2 - %3%4").arg(name, item.shader, categoryText(item.category), size));
}

} // namespace sme::app
