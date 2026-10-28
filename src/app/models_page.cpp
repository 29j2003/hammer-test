// models_page.cpp - The Models page (see models_page.h).

#include "models_page.h"

#include "asset_browser.h"
#include "editor.h"
#include "enhancements.h"
#include "material_cache.h"
#include "model_thumbnails.h"
#include "model_viewer.h"
#include "toggle_switch.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/text.h>

#include <functional>
#include <map>

namespace sme::app {

namespace {

constexpr int THUMB = 112;

/// Number of models per folder (counting subfolders) for the tree.
struct FolderNode {
	int count = 0;
	std::map<QString, FolderNode> children;   // sorted by name
};

/// "\"base\" \"Wooden.Medium\"" in the $keyvalues text: the value after a key, or empty.
QString keyValueAfter(const std::string& text, const QString& key) {
	const QRegularExpression re(QStringLiteral("\"%1\"\\s*\"([^\"]*)\"").arg(QRegularExpression::escape(key)), QRegularExpression::CaseInsensitiveOption);
	const auto match = re.match(QString::fromStdString(text));
	return match.hasMatch() ? match.captured(1) : QString();
}

} // namespace

// --- Filter --------------------------------------------------------------------------

void ModelFolderFilter::setFilter(const QString& folder, const QString& words) {
	m_folder = folder;
	m_words = words.split(' ', Qt::SkipEmptyParts);
	invalidateFilter();
}

void ModelFolderFilter::setUsedOnly(bool usedOnly) {
	m_usedOnly = usedOnly;
	invalidateFilter();
}

bool ModelFolderFilter::filterAcceptsRow(int row, const QModelIndex& parent) const {
	const QModelIndex index = sourceModel()->index(row, 0, parent);
	if (m_usedOnly && index.data(ModelListModel::UsageRole).toInt() == 0) return false;
	const QString path = index.data(Qt::UserRole).toString();
	if (!m_folder.isEmpty() && !path.startsWith(m_folder, Qt::CaseInsensitive)) return false;
	for (const auto& word : m_words) {
		if (!path.contains(word, Qt::CaseInsensitive)) return false;
	}
	return !m_thumbnails || !m_thumbnails->animationsOnly(path);
}

// --- Page ----------------------------------------------------------------------------

ModelsPage::ModelsPage(Editor* editor, AssetBrowser* assets, MaterialCache* materials, QWidget* parent)
    : QWidget(parent), m_editor(editor), m_assets(assets), m_materials(materials) {
	setObjectName("modelsPage");
	m_pool.setMaxThreadCount(1);   // one model at a time; a newer request makes the older one pointless
	auto* outer = new QHBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto* split = new QSplitter(Qt::Horizontal);
	split->setChildrenCollapsible(false);
	outer->addWidget(split);

	// Left: folders.
	auto* left = new QWidget;
	auto* leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(10, 10, 4, 10);
	auto* foldersTitle = new QLabel(tr("Folders"));
	foldersTitle->setProperty("role", "key");
	leftLayout->addWidget(foldersTitle);
	m_folders = new QTreeWidget;
	m_folders->setObjectName("modelFolders");
	m_folders->setHeaderHidden(true);
	m_folders->setColumnCount(2);
	m_folders->header()->setStretchLastSection(false);
	m_folders->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_folders->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_folders->setIndentation(14);
	m_folders->setUniformRowHeights(true);
	leftLayout->addWidget(m_folders, 1);
	split->addWidget(left);

	// Middle: search and the pictures.
	auto* middle = new QWidget;
	auto* middleLayout = new QVBoxLayout(middle);
	middleLayout->setContentsMargins(4, 10, 4, 10);
	m_search = new QLineEdit;
	m_search->setObjectName("modelSearch");
	m_search->setPlaceholderText(tr("Search models (words: props_c17 bench) ..."));
	m_search->setClearButtonEnabled(true);
	m_usedOnly = new QCheckBox(tr("Used in this map"));
	m_usedOnly->setObjectName("modelsPageUsedOnly");
	m_usedOnly->setToolTip(tr("Only the models the open map uses (the number after a name says how often)"));
	auto* searchRow = new QHBoxLayout;
	searchRow->addWidget(m_search, 1);
	searchRow->addWidget(m_usedOnly);
	middleLayout->addLayout(searchRow);
	m_model = new ModelListModel(assets->modelThumbnails(), this);
	m_filter = new ModelFolderFilter(this);
	m_filter->setSourceModel(m_model);
	m_list = new QListView;
	m_list->setObjectName("modelList");
	m_list->setViewMode(QListView::IconMode);
	m_list->setIconSize(QSize(THUMB, THUMB));
	m_list->setGridSize(QSize(THUMB + 22, THUMB + 36));
	m_list->setResizeMode(QListView::Adjust);
	m_list->setMovement(QListView::Static);
	m_list->setUniformItemSizes(true);
	m_list->setWordWrap(true);
	m_list->setDragEnabled(true);
	m_list->setDragDropMode(QAbstractItemView::DragOnly);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setLayoutMode(QListView::Batched);
	m_list->setBatchSize(200);
	m_list->setModel(m_filter);
	middleLayout->addWidget(m_list, 1);
	m_count = new QLabel;
	m_count->setProperty("role", "description");
	m_count->setWordWrap(true);
	middleLayout->addWidget(m_count);
	split->addWidget(middle);
	connect(assets->modelThumbnails(), &ModelThumbnails::thumbnailReady, m_model, &ModelListModel::thumbnailReady);
	m_filter->setThumbnails(assets->modelThumbnails());
	connect(assets->modelThumbnails(), &ModelThumbnails::thumbnailReady, this, [this](const QString& path) {
		if (m_assets->modelThumbnails()->animationsOnly(path)) applyFilter();   // nothing to place: leave it out
	});

	// Right: the model in 3D and what it is made of.
	auto* right = new QWidget;
	auto* rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(4, 10, 10, 10);
	rightLayout->setSpacing(6);
	m_title = new QLabel(tr("No model"));
	m_title->setObjectName("pageTitle");
	rightLayout->addWidget(m_title);
	m_path = new QLabel;
	m_path->setProperty("role", "description");
	m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
	rightLayout->addWidget(m_path);
	m_viewer = new ModelViewer(materials);
	m_viewer->setObjectName("modelViewer");
	rightLayout->addWidget(m_viewer, 3);

	auto* viewRow = new QHBoxLayout;
	viewRow->addWidget(new QLabel(tr("Skin")));
	m_skin = new QComboBox;
	m_skin->setObjectName("modelSkin");
	m_skin->setMinimumWidth(70);
	viewRow->addWidget(m_skin);
	viewRow->addSpacing(10);
	m_wireframe = new ToggleSwitch;
	m_wireframe->setObjectName("modelWireframe");
	m_wireframe->setToolTip(tr("Show the triangles on top"));
	viewRow->addWidget(m_wireframe);
	viewRow->addWidget(new QLabel(tr("Wireframe")));
	viewRow->addStretch(1);
	auto* reset = new QPushButton(tr("Frame"));
	reset->setToolTip(tr("Show the whole model again (or double-click the view)"));
	viewRow->addWidget(reset);
	rightLayout->addLayout(viewRow);

	m_bodyHolder = new QWidget;
	m_bodyForm = new QFormLayout(m_bodyHolder);
	m_bodyForm->setContentsMargins(0, 0, 0, 0);
	m_bodyForm->setLabelAlignment(Qt::AlignLeft);
	m_bodyForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	rightLayout->addWidget(m_bodyHolder);
	m_bodyHolder->setVisible(false);

	m_details = new QLabel;
	m_details->setObjectName("modelDetails");
	m_details->setWordWrap(true);
	m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	rightLayout->addWidget(m_details);
	auto* materialsTitle = new QLabel(tr("Materials (click to edit)"));
	materialsTitle->setProperty("role", "key");
	rightLayout->addWidget(materialsTitle);
	m_materialList = new QListWidget;
	m_materialList->setObjectName("modelMaterials");
	m_materialList->setMaximumHeight(110);
	rightLayout->addWidget(m_materialList);
	m_enhancementSwitches = new EnhancementSwitches(editor->enhancements(), right);
	rightLayout->addWidget(m_enhancementSwitches);
	m_enhancementSwitches->setModel({});

	auto* placeRow = new QHBoxLayout;
	placeRow->addWidget(new QLabel(tr("Place as")));
	m_placeAs = new QComboBox;
	m_placeAs->setObjectName("modelPlaceAs");
	m_placeAs->addItem("prop_static");
	m_placeAs->addItem("prop_physics");
	m_placeAs->addItem("prop_dynamic");
	m_placeAs->setItemData(0, tr("Part of the world: cheapest, never moves (needs a model made as a static prop)"), Qt::ToolTipRole);
	m_placeAs->setItemData(1, tr("Falls, can be picked up and broken (needs a collision model)"), Qt::ToolTipRole);
	m_placeAs->setItemData(2, tr("Can move and animate, controlled by inputs"), Qt::ToolTipRole);
	placeRow->addWidget(m_placeAs);
	placeRow->addStretch(1);
	m_copy = new QPushButton(tr("Copy path"));
	m_copy->setObjectName("modelCopy");
	placeRow->addWidget(m_copy);
	m_place = new QPushButton(tr("Place in map"));
	m_place->setObjectName("modelPlace");
	m_place->setProperty("accent", true);
	m_place->setToolTip(tr("Arms the entity tool with this model: click in the map to place it (double-click in the list does the same)"));
	placeRow->addWidget(m_place);
	rightLayout->addLayout(placeRow);
	split->addWidget(right);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 3);
	split->setStretchFactor(2, 2);
	split->setSizes({230, 640, 470});

	// Wiring.
	connect(m_search, &QLineEdit::textChanged, this, &ModelsPage::applyFilter);
	connect(m_usedOnly, &QCheckBox::toggled, this, [this](bool on) {
		m_filter->setUsedOnly(on);
		applyFilter();
	});
	// How often the map uses each model: the counts behind the names and the "Used in this map" filter.
	connect(editor, &Editor::documentChanged, this, [this] {
		m_model->setUsage(modelUsageOf(m_editor->doc()));
		if (m_usedOnly->isChecked()) applyFilter();
		if (!m_current.isEmpty() && isVisible()) updateDetails();
	});
	connect(m_folders, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
		m_folder = item ? item->data(0, Qt::UserRole).toString() : QString();
		applyFilter();
	});
	connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
		if (!index.isValid()) return;
		const QString path = index.data(Qt::UserRole).toString();
		if (path == m_current) return;
		m_current = path;
		load(true);
	});
	connect(m_list, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
		if (index.isValid() && index.data(Qt::UserRole).toString() == m_current) m_place->click();
	});
	connect(m_skin, &QComboBox::currentIndexChanged, this, [this] {
		if (!m_current.isEmpty() && m_skin->count() > 0) load(false);
	});
	connect(m_wireframe, &QCheckBox::toggled, m_viewer, &ModelViewer::setWireframe);
	connect(reset, &QPushButton::clicked, m_viewer, &ModelViewer::frameModel);
	// Right-click: copy, and "Show in ..." / "Find in map" from the main window.
	m_list->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		const QModelIndex index = m_list->indexAt(pos);
		if (!index.isValid()) return;
		const QString path = index.data(Qt::UserRole).toString();
		QMenu menu;
		menu.addAction(tr("Copy path"), this, [path] { QApplication::clipboard()->setText(path); });
		if (m_menuFiller) {
			menu.addSeparator();
			m_menuFiller(&menu, {AssetKind::Model, path.toStdString()}, AssetPlace::ModelsPage);
		}
		menu.exec(m_list->viewport()->mapToGlobal(pos));
	});
	m_materialList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_materialList, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		auto* item = m_materialList->itemAt(pos);
		if (!item || !m_menuFiller) return;
		QMenu menu;
		m_menuFiller(&menu, {AssetKind::Material, item->text().toStdString()}, AssetPlace::ModelsPage);
		menu.exec(m_materialList->viewport()->mapToGlobal(pos));
	});
	connect(m_materialList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
		if (item && item->data(Qt::UserRole).toBool()) emit openMaterialRequested(item->text());
	});
	connect(m_copy, &QPushButton::clicked, this, [this] {
		if (m_current.isEmpty()) return;
		QApplication::clipboard()->setText(m_current);
		emit m_editor->message(tr("Copied %1").arg(m_current));
	});
	connect(m_place, &QPushButton::clicked, this, [this] {
		if (!m_current.isEmpty()) emit placeRequested(placeClass(), placeKeys());
	});

	connect(editor, &Editor::gameAboutToChange, this, [this] {
		++*m_generation;
		m_pool.waitForDone();   // loads read the game's files
	});
	connect(editor, &Editor::gameChanged, this, [this] { setModels({}); });
	connect(assets, &AssetBrowser::materialsIndexed, this, [this] { setModels(m_assets->modelNames()); });
	setModels(assets->modelNames());
	updateDetails();
}

ModelsPage::~ModelsPage() {
	++*m_generation;
	m_pool.waitForDone();
}

void ModelsPage::setModels(const QStringList& models) {
	m_model->setModels(models);
	// Folder tree: "models/props_c17/furniture/x.mdl" counts for props_c17 and props_c17/furniture.
	FolderNode root;
	for (const auto& path : models) {
		++root.count;
		const QStringList parts = path.split('/');
		FolderNode* node = &root;
		for (int i = 1; i + 1 < parts.size(); ++i) {   // skip "models" and the file name
			node = &node->children[parts[i].toLower()];
			++node->count;
		}
	}
	const QSignalBlocker block(m_folders);
	m_folders->clear();
	auto* all = new QTreeWidgetItem(m_folders, {tr("All models"), QString::number(root.count)});
	all->setData(0, Qt::UserRole, QString());
	const std::function<void(QTreeWidgetItem*, const FolderNode&, const QString&)> add = [&](QTreeWidgetItem* parent, const FolderNode& node, const QString& prefix) {
		for (const auto& [name, child] : node.children) {
			auto* item = parent ? new QTreeWidgetItem(parent, {name, QString::number(child.count)}) : new QTreeWidgetItem(m_folders, {name, QString::number(child.count)});
			const QString folder = prefix + name + '/';
			item->setData(0, Qt::UserRole, folder);
			item->setToolTip(0, folder);
			item->setForeground(1, QColor(130, 130, 130));
			add(item, child, folder);
		}
	};
	all->setForeground(1, QColor(130, 130, 130));
	add(nullptr, root, QStringLiteral("models/"));
	// The folder picked before stays picked if the new game has it.
	QTreeWidgetItem* current = all;
	if (!m_folder.isEmpty()) {
		for (QTreeWidgetItemIterator it(m_folders); *it; ++it) {
			if ((*it)->data(0, Qt::UserRole).toString().compare(m_folder, Qt::CaseInsensitive) == 0) current = *it;
		}
	}
	if (current == all) m_folder.clear();
	m_folders->setCurrentItem(current);
	applyFilter();
	if (models.isEmpty()) {
		m_current.clear();
		m_info = {};
		m_viewer->setModel(nullptr);
		m_viewer->setMessage(m_editor->game() ? tr("Looking for models ...") : tr("No game loaded"));
		updateDetails();
	} else if (!m_current.isEmpty() && models.contains(m_current)) {
		selectModel(m_current);
	} else if (m_filter->rowCount() > 0) {
		m_list->setCurrentIndex(m_filter->index(0, 0));   // the page never starts empty
	}
}

void ModelsPage::applyFilter() {
	m_filter->setFilter(m_folder, m_search->text());
	const int shown = m_filter->rowCount();
	const int total = m_model->rowCount();
	if (total == 0) {
		m_count->setText(m_editor->game() ? tr("Looking for models ...") : tr("No game loaded"));
	} else {
		m_count->setText(tr("%1 of %2 models - drag one onto the Map button and into a view, or double-click to place it").arg(shown).arg(total));
	}
}

void ModelsPage::selectModel(const QString& path) {
	for (int row = 0; row < m_filter->rowCount(); ++row) {
		const QModelIndex index = m_filter->index(row, 0);
		if (index.data(Qt::UserRole).toString().compare(path, Qt::CaseInsensitive) == 0) {
			m_list->setCurrentIndex(index);
			m_list->scrollTo(index);
			return;
		}
	}
	// Not in the current folder or search: show all, then pick it.
	if (!m_folder.isEmpty() || !m_search->text().isEmpty()) {
		m_search->clear();
		if (m_folders->topLevelItemCount() > 0) m_folders->setCurrentItem(m_folders->topLevelItem(0));
		for (int row = 0; row < m_filter->rowCount(); ++row) {
			const QModelIndex index = m_filter->index(row, 0);
			if (index.data(Qt::UserRole).toString().compare(path, Qt::CaseInsensitive) == 0) {
				m_list->setCurrentIndex(index);
				m_list->scrollTo(index);
				return;
			}
		}
	}
}

int ModelsPage::body() const {
	std::vector<int> choices;
	int box = 0;
	for (const auto& group : m_info.bodyGroups) {
		if (group.options.size() > 1 && box < m_bodyBoxes.size()) {
			choices.push_back(std::max(0, m_bodyBoxes[box++]->currentIndex()));
		} else {
			choices.push_back(0);
		}
	}
	return m_info.bodyValue(choices);
}

void ModelsPage::load(bool withInfo) {
	auto* game = m_editor->game();
	if (!game || m_current.isEmpty()) return;
	const int generation = ++*m_generation;
	const QString path = m_current;
	const int skin = withInfo ? 0 : std::max(0, m_skin->currentIndex());
	const int bodyValue = withInfo ? 0 : body();
	const GameFileSystem* fs = &game->files;
	if (withInfo) {
		m_title->setText(path.section('/', -1));
		m_path->setText(path);
		m_viewer->setMessage(tr("Loading %1 ...").arg(path.section('/', -1)));
		m_viewer->setModel(nullptr);
	}
	const auto counter = m_generation;
	m_pool.start([this, counter, generation, path, skin, bodyValue, withInfo, fs] {
		if (*counter != generation) return;
		Loaded result;
		result.path = path;
		result.generation = generation;
		result.withInfo = withInfo;
		const std::string file = path.toStdString();
		if (withInfo) {
			if (const auto data = fs->read(file)) {
				std::string error;
				if (auto info = readModelInfo(*data, &error)) result.info = std::move(*info);
				else result.error = QString::fromStdString(error);
			}
			result.sequences = modelSequences(*fs, file);
			result.hasPhysics = fs->exists(file.substr(0, file.size() - 4) + ".phy");
		}
		std::string error;
		if (auto geometry = loadModelGeometry(*fs, file, skin, &error, bodyValue)) {
			result.geometry = std::make_shared<const ModelGeometry>(std::move(*geometry));
		} else if (result.error.isEmpty()) {
			result.error = QString::fromStdString(error);
		}
		if (*counter != generation) return;
		QMetaObject::invokeMethod(this, [this, result] { loaded(result); }, Qt::QueuedConnection);
	});
}

void ModelsPage::loaded(const Loaded& result) {
	if (result.generation != *m_generation || result.path != m_current) return;
	if (result.withInfo) {
		m_info = result.info;
		m_sequences = result.sequences;
		m_hasPhysics = result.hasPhysics;
		{
			const QSignalBlocker block(m_skin);
			m_skin->clear();
			for (int i = 0; i < std::max(1, m_info.skinCount); ++i) m_skin->addItem(QString::number(i));
			m_skin->setEnabled(m_skin->count() > 1);
		}
		rebuildBodyGroups();
		// The class the model is made for (same rule as dropping it into the map).
		m_placeAs->setCurrentText(QString::fromStdString(recommendedPropClass({m_info.staticProp, m_hasPhysics})));
	}
	if (result.geometry) {
		m_viewer->setModel(result.geometry, result.withInfo);
	} else {
		m_viewer->setModel(nullptr);
		m_viewer->setMessage(tr("Cannot load %1: %2").arg(result.path.section('/', -1), result.error));
	}
	updateDetails();
}

void ModelsPage::rebuildBodyGroups() {
	// The old boxes go with their rows; new ones are added to the form before anything is shown.
	while (m_bodyForm->rowCount() > 0) m_bodyForm->removeRow(0);
	m_bodyBoxes.clear();
	for (const auto& group : m_info.bodyGroups) {
		if (group.options.size() < 2) continue;
		auto* box = new QComboBox;
		box->setObjectName(QStringLiteral("bodygroup:%1").arg(QString::fromStdString(group.name)));
		for (const auto& option : group.options) {
			box->addItem(option.empty() || iequals(option, "blank") ? tr("(nothing)") : QString::fromStdString(option));
		}
		m_bodyForm->addRow(QString::fromStdString(group.name), box);
		m_bodyBoxes << box;
		connect(box, &QComboBox::currentIndexChanged, this, [this] { load(false); });
	}
	m_bodyHolder->setVisible(!m_bodyBoxes.isEmpty());
}

void ModelsPage::updateDetails() {
	m_materialList->clear();
	m_enhancementSwitches->setModel(m_current);
	const auto geometry = m_viewer->model();
	const bool have = !m_current.isEmpty() && geometry;
	m_place->setEnabled(!m_current.isEmpty());
	m_copy->setEnabled(!m_current.isEmpty());
	m_placeAs->setEnabled(!m_current.isEmpty());
	if (m_current.isEmpty()) {
		m_title->setText(tr("No model"));
		m_path->setText(m_model->rowCount() > 0 ? tr("Pick a model in the list") : QString());
		m_details->clear();
		return;
	}
	QStringList rows;
	const auto row = [&rows](const QString& name, const QString& value) {
		rows << QStringLiteral("<tr><td style='color:#9a9a9a; padding-right:12px'>%1</td><td>%2</td></tr>").arg(name, value);
	};
	if (have) {
		const Vec3 s = geometry->bounds.empty() ? Vec3{} : geometry->bounds.size();
		row(tr("Size"), tr("%1 x %2 x %3 units").arg(s.x, 0, 'f', 0).arg(s.y, 0, 'f', 0).arg(s.z, 0, 'f', 0));
		row(tr("Triangles"), tr("%L1 (%L2 vertices)").arg(geometry->triangleCount()).arg(geometry->vertices.size()));
	}
	const int used = m_model->usage(m_current);
	row(tr("In this map"), used > 0 ? tr("used %1×").arg(used) : tr("not used"));
	row(tr("Skins"), QString::number(std::max(1, m_info.skinCount)));
	int groups = 0;
	for (const auto& group : m_info.bodyGroups) groups += group.options.size() > 1 ? 1 : 0;
	if (groups > 0) row(tr("Body groups"), QString::number(groups));
	QStringList sequences;
	for (std::size_t i = 0; i < m_sequences.size() && i < 8; ++i) sequences << QString::fromStdString(m_sequences[i]).toHtmlEscaped();
	if (m_sequences.size() > 8) sequences << QStringLiteral("...");
	row(tr("Animations"), m_sequences.empty() ? tr("none") : QStringLiteral("%1: %2").arg(m_sequences.size()).arg(sequences.join(", ")));
	if (!m_info.surfaceProp.empty()) row(tr("Surface"), QString::fromStdString(m_info.surfaceProp).toHtmlEscaped());
	row(tr("Static prop"), m_info.staticProp ? tr("yes - can be a prop_static") : tr("no - use prop_dynamic or prop_physics"));
	QString physics = m_hasPhysics ? tr("collision model") : tr("no collision model");
	if (m_info.mass > 0) physics += tr(", %1 kg").arg(double(m_info.mass), 0, 'f', 1);
	const QString breakable = keyValueAfter(m_info.keyValues, "base");
	if (!breakable.isEmpty()) physics += tr(", prop data %1").arg(breakable.toHtmlEscaped());
	row(tr("Physics"), physics);
	m_details->setText(QStringLiteral("<table>%1</table>").arg(rows.join(QString())));

	// Materials of the shown skin; missing ones are marked (they show as a checkerboard in game).
	if (have) {
		QStringList seen;
		for (const auto& mesh : geometry->meshes) {
			const QString name = QString::fromStdString(mesh.material);
			if (seen.contains(name, Qt::CaseInsensitive)) continue;
			seen << name;
			auto* item = new QListWidgetItem(name, m_materialList);
			const bool exists = m_editor->game() && m_editor->game()->files.exists("materials/" + mesh.material + ".vmt");
			item->setData(Qt::UserRole, exists);
			if (!exists) {
				item->setForeground(QColor(230, 120, 90));
				item->setToolTip(tr("Missing: materials/%1.vmt").arg(name));
			} else {
				item->setToolTip(tr("Open in the material editor"));
			}
		}
	}
}

QString ModelsPage::placeClass() const {
	return m_placeAs->currentText();
}

std::vector<std::pair<std::string, std::string>> ModelsPage::placeKeys() const {
	std::vector<std::pair<std::string, std::string>> keys{{"model", m_current.toStdString()}};
	if (m_skin->currentIndex() > 0) keys.emplace_back("skin", std::to_string(m_skin->currentIndex()));
	// prop_static has no body groups in the map file; the others take the body number.
	const int bodyValue = body();
	if (bodyValue > 0 && placeClass() != "prop_static") keys.emplace_back("body", std::to_string(bodyValue));
	return keys;
}

} // namespace sme::app
