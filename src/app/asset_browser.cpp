// asset_browser.cpp - Material/model/sound/entity browser (see asset_browser.h).

#include "asset_browser.h"

#include "copy_paste.h"

#include "editor.h"
#include "material_cache.h"
#include "model_cache.h"
#include "model_thumbnails.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDrag>
#include <QIcon>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QListView>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <sme/asset_refs.h>
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

/// Icon grid whose drags show a card with the preview and name instead of the bare tile.
class AssetIconView : public QListView {
public:
	using QListView::QListView;

protected:
	void startDrag(Qt::DropActions actions) override {
		const QModelIndexList indexes = selectedIndexes();
		if (indexes.isEmpty()) return;
		QMimeData* mime = model()->mimeData(indexes);
		if (!mime) return;
		const QModelIndex& index = indexes.first();
		const QVariant decoration = index.data(Qt::DecorationRole);
		QPixmap preview = decoration.value<QPixmap>();
		if (preview.isNull()) preview = decoration.value<QIcon>().pixmap(QSize(THUMB, THUMB), devicePixelRatioF());
		auto* drag = new QDrag(this);
		drag->setMimeData(mime);
		drag->setPixmap(assetDragCard(preview, index.data(Qt::DisplayRole).toString(), index.data(Qt::ToolTipRole).toString(), devicePixelRatioF()));
		drag->setHotSpot(QPoint(-14, -14));   // the card sits beside the cursor, the drop preview stays visible
		drag->exec(actions, Qt::CopyAction);
	}
};

QListView* makeIconView(QWidget* parent) {
	auto* view = new AssetIconView(parent);
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

QPixmap assetDragCard(const QPixmap& preview, const QString& title, const QString& subtitle, qreal devicePixelRatio) {
	const QSize size(260, 72);
	QPixmap card(size * devicePixelRatio);
	card.setDevicePixelRatio(devicePixelRatio);
	card.fill(Qt::transparent);
	QPainter painter(&card);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	painter.setPen(QPen(QColor(255, 160, 40, 210), 1.5));
	painter.setBrush(QColor(28, 30, 34, 225));
	painter.drawRoundedRect(QRectF(0.75, 0.75, size.width() - 1.5, size.height() - 1.5), 9, 9);
	const QRect thumb(8, 8, 56, 56);
	if (!preview.isNull()) {
		QPainterPath clip;
		clip.addRoundedRect(thumb, 6, 6);
		painter.save();
		painter.setClipPath(clip);
		painter.drawPixmap(thumb, preview.scaled(thumb.size() * devicePixelRatio, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		painter.restore();
	}
	const int textLeft = thumb.right() + 10;
	const int textWidth = size.width() - textLeft - 10;
	QFont font = painter.font();
	font.setBold(true);
	painter.setFont(font);
	painter.setPen(QColor(240, 240, 245));
	painter.drawText(QRect(textLeft, 12, textWidth, 22), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(font).elidedText(title, Qt::ElideRight, textWidth));
	font.setBold(false);
	font.setPointSizeF(font.pointSizeF() * 0.85);
	painter.setFont(font);
	painter.setPen(QColor(165, 170, 180));
	painter.drawText(QRect(textLeft, 36, textWidth, 20), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(font).elidedText(subtitle, Qt::ElideMiddle, textWidth));
	return card;
}

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

// --- Model list ---------------------------------------------------------------------

ModelListModel::ModelListModel(ModelThumbnails* thumbnails, QObject* parent) : QAbstractListModel(parent), m_thumbnails(thumbnails) {}

void ModelListModel::setModels(const QStringList& models) {
	beginResetModel();
	m_models = models;
	m_rows.clear();
	for (int i = 0; i < m_models.size(); ++i) {
		m_rows.insert(m_models[i], i);
	}
	endResetModel();
}

void ModelListModel::setUsage(QHash<QString, int> usage) {
	if (usage == m_usage) return;   // most changes (moving, other keys) leave the counts alone
	m_usage = std::move(usage);
	if (!m_models.isEmpty()) emit dataChanged(index(0), index(static_cast<int>(m_models.size()) - 1), {Qt::DisplayRole, Qt::ToolTipRole, UsageRole});
}

QHash<QString, int> modelUsageOf(const MapDocument& doc) {
	QHash<QString, int> out;
	for (const auto& [path, count] : modelUsage(doc)) out.insert(QString::fromStdString(path), count);
	return out;
}

void ModelSearchFilter::setWords(const QString& words) {
	m_words = words.split(' ', Qt::SkipEmptyParts);
	invalidateFilter();
}

void ModelSearchFilter::setUsedOnly(bool usedOnly) {
	m_usedOnly = usedOnly;
	invalidateFilter();
}

bool ModelSearchFilter::filterAcceptsRow(int row, const QModelIndex& parent) const {
	const QModelIndex index = sourceModel()->index(row, 0, parent);
	if (m_usedOnly && index.data(ModelListModel::UsageRole).toInt() == 0) return false;
	// Every word must appear in the path.
	const QString path = index.data(Qt::UserRole).toString();
	for (const auto& word : m_words) {
		if (!path.contains(word, Qt::CaseInsensitive)) return false;
	}
	return true;
}

int ModelListModel::rowCount(const QModelIndex& parent) const {
	return parent.isValid() ? 0 : static_cast<int>(m_models.size());
}

QVariant ModelListModel::data(const QModelIndex& index, int role) const {
	if (!index.isValid() || index.row() >= m_models.size()) {
		return {};
	}
	const QString& path = m_models[index.row()];
	const int used = m_usage.isEmpty() ? 0 : m_usage.value(path.toLower());
	switch (role) {
		case Qt::DisplayRole:   // "crate01", with how often the map uses it: "crate01 · 3×"
			return used > 0 ? QStringLiteral("%1 · %2×").arg(path.section('/', -1).chopped(4)).arg(used) : path.section('/', -1).chopped(4);
		case Qt::ToolTipRole: return used > 0 ? tr("%1\nUsed %2 times in this map").arg(path).arg(used) : path;
		case Qt::UserRole: return path;
		case UsageRole: return used;
		case Qt::DecorationRole: return m_thumbnails->get(path);   // rendered only for shown items
		default: return {};
	}
}

Qt::ItemFlags ModelListModel::flags(const QModelIndex& index) const {
	return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList ModelListModel::mimeTypes() const {
	return {"application/x-sme-model"};
}

QMimeData* ModelListModel::mimeData(const QModelIndexList& indexes) const {
	if (indexes.isEmpty()) {
		return nullptr;
	}
	auto* mime = new QMimeData;
	const QString path = data(indexes.first(), Qt::UserRole).toString();
	mime->setData("application/x-sme-model", path.toUtf8());
	mime->setText(path);
	return mime;
}

void ModelListModel::thumbnailReady(const QString& path) {
	if (const auto it = m_rows.constFind(path); it != m_rows.constEnd()) {
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
		m_materialView->setObjectName("materialList");
		addCopyShortcut(m_materialView, Qt::UserRole);   // Ctrl+C: the material's name
		m_materialView->setModel(m_materialFilter);
		m_materialView->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(m_materialView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
			const QModelIndex index = m_materialView->indexAt(pos);
			if (!index.isValid()) return;
			const QString name = index.data(Qt::UserRole).toString();
			QMenu menu;
			menu.addAction(tr("Edit material ..."), this, [this, name] { emit editMaterialRequested(name); });
			menu.addAction(tr("Copy name"), this, [name] { QApplication::clipboard()->setText(name); });
			if (m_menuFiller) {
				menu.addSeparator();
				m_menuFiller(&menu, {AssetKind::Material, name.toStdString()}, m_pageMode ? AssetPlace::MaterialsPage : AssetPlace::AssetBrowser);
			}
			menu.exec(m_materialView->viewport()->mapToGlobal(pos));
		});
		v->addWidget(m_materialView, 1);
		m_materialInfo = new QLabel(tr("No game loaded"));
		m_materialInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // long names must not widen the window
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
				emit materialPicked(index.data(Qt::UserRole).toString());
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

	// Models: previews like the materials, drag one into the 3D view to place it as a prop.
	{
		auto* page = new QWidget;
		auto* v = new QVBoxLayout(page);
		v->setContentsMargins(4, 4, 4, 4);
		m_modelSearch = new QLineEdit;
		m_modelSearch->setPlaceholderText(tr("Search models (words: props_c17 bench) ..."));
		m_modelSearch->setClearButtonEnabled(true);
		m_modelsUsedOnly = new QCheckBox(tr("Used in this map"));
		m_modelsUsedOnly->setObjectName("modelsUsedOnly");
		m_modelsUsedOnly->setToolTip(tr("Only the models the open map uses (the number after a name says how often)"));
		auto* searchRow = new QHBoxLayout;
		searchRow->addWidget(m_modelSearch, 1);
		searchRow->addWidget(m_modelsUsedOnly);
		v->addLayout(searchRow);
		m_modelThumbnails = new ModelThumbnails(editor->models(), m_thumbnails, THUMB, this);
		m_modelModel = new ModelListModel(m_modelThumbnails, this);
		m_modelFilter = new ModelSearchFilter(this);
		auto* proxy = m_modelFilter;
		proxy->setSourceModel(m_modelModel);
		m_modelView = makeIconView(page);
		addCopyShortcut(m_modelView, Qt::UserRole);      // Ctrl+C: the model's path
		m_modelView->setModel(proxy);
		connect(m_modelsUsedOnly, &QCheckBox::toggled, proxy, &ModelSearchFilter::setUsedOnly);
		v->addWidget(m_modelView, 1);
		m_modelView->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(m_modelView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
			const QModelIndex index = m_modelView->indexAt(pos);
			if (!index.isValid()) return;
			const QString path = index.data(Qt::UserRole).toString();
			QMenu menu;
			menu.addAction(tr("Copy path"), this, [path] { QApplication::clipboard()->setText(path); });
			if (m_menuFiller) {
				menu.addSeparator();
				m_menuFiller(&menu, {AssetKind::Model, path.toStdString()}, AssetPlace::AssetBrowser);
			}
			menu.exec(m_modelView->viewport()->mapToGlobal(pos));
		});
		m_modelInfo = new QLabel(tr("No game loaded"));
		m_modelInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		m_modelInfo->setWordWrap(true);
		m_modelInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
		v->addWidget(m_modelInfo);
		m_tabs->addTab(page, tr("Models"));
		connect(m_modelThumbnails, &ModelThumbnails::thumbnailReady, m_modelModel, &ModelListModel::thumbnailReady);
		connect(m_modelSearch, &QLineEdit::textChanged, proxy, &ModelSearchFilter::setWords);
		connect(m_modelView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
			if (!index.isValid() || !m_editor->game()) {
				return;
			}
			const QString path = index.data(Qt::UserRole).toString();
			std::string error;
			const auto sequences = modelSequences(m_editor->game()->files, path.toStdString(), &error);
			QStringList names;
			for (std::size_t i = 0; i < sequences.size() && i < 30; ++i) {
				names << QString::fromStdString(sequences[i]);
			}
			QString size;
			if (const auto geometry = m_editor->models()->get(path)) {
				const Vec3 s = geometry->bounds.size();
				size = tr("%1 x %2 x %3 units, %4 triangles").arg(s.x, 0, 'f', 0).arg(s.y, 0, 'f', 0).arg(s.z, 0, 'f', 0).arg(geometry->triangleCount());
			} else if (m_editor->models()->failed(path)) {
				size = tr("cannot load: %1").arg(m_editor->models()->error(path));
			}
			if (const int used = m_modelModel->usage(path); used > 0) size += tr(" - used %1× in this map").arg(used);
			m_modelInfo->setText(tr("<b>%1</b> - %2<br>%3 animations: %4%5<br><i>Drag into the 3D view to place it, double-click sets it on the selected entity</i>")
			                         .arg(path, size)
			                         .arg(sequences.size())
			                         .arg(names.join(", "))
			                         .arg(sequences.size() > 30 ? QStringLiteral(", ...") : QString()));
		});
		connect(m_modelView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
			const QString path = index.data(Qt::UserRole).toString();
			const auto& selection = m_editor->selection();
			if (selection.entities.empty()) {
				QApplication::clipboard()->setText(path);
				emit m_editor->message(tr("Model path copied - drag the model into the 3D view to place it, or select an entity to set it"));
				return;
			}
			m_editor->commit(edit::setKeyValue(m_editor->doc(), selection.entities, "model", path.toStdString()));
		});
		connect(this, &AssetBrowser::materialsIndexed, this, [this] {
			m_modelModel->setModels(m_models);
			m_modelInfo->setText(tr("%1 models - drag one into the 3D view to place it").arg(m_models.size()));
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
		m_particles = std::move(index.particles);
		m_soundscapes = std::move(index.soundscapes);
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

void AssetBrowser::showModels() {
	m_tabs->setCurrentIndex(1);
}

void AssetBrowser::setPageMode(bool page) {
	m_pageMode = page;
	m_tabs->tabBar()->setVisible(!page);
	updateRecent();   // the page has its own list of what was edited
	if (page) showMaterials();
}

bool AssetBrowser::selectMaterial(const QString& name) {
	const QString wanted = QString::fromStdString(normalizedMaterial(name.toStdString()));
	const auto find = [&]() -> QModelIndex {
		for (int row = 0; row < m_materialFilter->rowCount(); ++row) {
			const QModelIndex index = m_materialFilter->index(row, 0);
			if (index.data(Qt::UserRole).toString().compare(wanted, Qt::CaseInsensitive) == 0) return index;
		}
		return {};
	};
	m_materialSearch->clear();
	QModelIndex index = find();
	if (!index.isValid()) {
		// Hidden by the category: show all.
		m_materialCategory->setCurrentIndex(m_materialCategory->findData(int(MaterialFilter::Category::All)));
		index = find();
	}
	if (!index.isValid()) return false;
	showMaterials();
	m_materialView->setCurrentIndex(index);
	m_materialView->scrollTo(index, QAbstractItemView::PositionAtCenter);
	return true;
}

bool AssetBrowser::selectModel(const QString& path) {
	m_modelSearch->clear();
	auto* model = m_modelView->model();
	QString wanted = path;
	wanted.replace('\\', '/');
	for (int row = 0; row < model->rowCount(); ++row) {
		const QModelIndex index = model->index(row, 0);
		if (index.data(Qt::UserRole).toString().compare(wanted, Qt::CaseInsensitive) == 0) {
			showModels();
			m_modelView->setCurrentIndex(index);
			m_modelView->scrollTo(index, QAbstractItemView::PositionAtCenter);
			return true;
		}
	}
	return false;
}

QString AssetBrowser::shownMaterial() const {
	const QModelIndex current = m_materialView->currentIndex();
	if (current.isValid()) return current.data(Qt::UserRole).toString();
	return m_materialFilter->rowCount() > 0 ? m_materialFilter->index(0, 0).data(Qt::UserRole).toString() : QString();
}

QStringList AssetBrowser::particleNames() const {
	QStringList names;
	for (const auto& system : m_particles) {
		names << QString::fromStdString(system.name);
	}
	return names;
}

std::vector<std::string> AssetBrowser::materialNames() const {
	std::vector<std::string> out;
	for (const auto& item : m_materialModel->items()) {
		out.push_back(item.name.toStdString());
	}
	return out;
}

void AssetBrowser::materialChanged(const QString& name) {
	m_thumbnails->reload(name);
	const auto& items = m_materialModel->items();
	const bool known = std::any_of(items.begin(), items.end(), [&](const auto& item) { return item.name == name; });
	if (!known) {
		reload();   // a new material: list it
	}
}

void AssetBrowser::reload() {
	auto* game = m_editor->game();
	m_thumbnails->setFileSystem(game ? &game->files : nullptr);
	m_modelThumbnails->clear();
	m_modelModel->setModels({});
	m_materialModel->setItems({});
	updateRecent();
	if (!game) {
		return;
	}
	m_materialInfo->setText(tr("Indexing materials, models and sounds ..."));

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
		for (const char* extension : {".wav", ".mp3", ".ogg"}) {
			for (const auto& file : fs->list("sound", extension)) {
				index.sounds << QString::fromStdString(file.substr(6));
			}
		}
		if (generation == m_generation) {
			index.particles = particleSystems(*fs);
			for (const auto& scape : soundscapes(*fs)) index.soundscapes << QString::fromStdString(scape.name);
			index.soundscapes.sort(Qt::CaseInsensitive);
		}
		return index;
	}));
}

void AssetBrowser::updateRecent() {
	m_recent->clear();
	m_recent->setVisible(!m_pageMode && !m_editor->recentMaterials().isEmpty());
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
	m_modelModel->setUsage(modelUsageOf(m_editor->doc()));
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
