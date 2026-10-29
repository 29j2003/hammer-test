// models_page.h - The Models page of the side bar: every model of the game and its mods by
// folder (props_c17, props_junk ...) with search and preview pictures, a 3D view of the chosen
// model with its skins and body groups, what it is made of (size, triangles, materials, prop
// settings) and buttons to place it in the map or copy its path. Models can also be dragged
// from the list onto the Map button and on into a view.

#pragma once

#include "asset_menu.h"

#include <QFuture>
#include <QSortFilterProxyModel>
#include <QThreadPool>
#include <QWidget>

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <sme/assets.h>
#include <sme/studio_model.h>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTimer;
class QTreeWidgetItem;

namespace sme::app {

class ModelExportDialog;

class AssetBrowser;
class Editor;
class MaterialCache;
class ModelListModel;
class ModelViewer;

/// Models of one folder (and its subfolders) whose path holds every search word.
class ModelFolderFilter : public QSortFilterProxyModel {
public:
	using QSortFilterProxyModel::QSortFilterProxyModel;
	/// "models/props_c17/" (empty: all) and the search text (words, any order).
	void setFilter(const QString& folder, const QString& words);
	/// Only models the open map uses (ModelListModel::UsageRole).
	void setUsedOnly(bool usedOnly);
	/// Leaves out models that hold only animations (once their preview found out).
	void setThumbnails(const class ModelThumbnails* thumbnails) { m_thumbnails = thumbnails; }

protected:
	[[nodiscard]] bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
	QString m_folder;
	QStringList m_words;
	bool m_usedOnly = false;
	const class ModelThumbnails* m_thumbnails = nullptr;
};

/// The Models page (see the file comment).
class ModelsPage : public QWidget {
	Q_OBJECT

public:
	ModelsPage(Editor* editor, AssetBrowser* assets, MaterialCache* materials, QWidget* parent = nullptr);
	~ModelsPage() override;

	/// Shows this model ("models/props_c17/chair01.mdl"): selects it in the list and loads it.
	void selectModel(const QString& path);
	[[nodiscard]] QString currentModel() const { return m_current; }
	/// Information about the shown model (valid once it is loaded).
	[[nodiscard]] const ModelInfo& info() const { return m_info; }
	/// The entity class "Place in map" uses and the keyvalues it gives the prop (model, skin, body).
	[[nodiscard]] QString placeClass() const;
	[[nodiscard]] std::vector<std::pair<std::string, std::string>> placeKeys() const;

	[[nodiscard]] QLineEdit* search() const { return m_search; }
	[[nodiscard]] QCheckBox* usedOnly() const { return m_usedOnly; }
	[[nodiscard]] QTreeWidget* folders() const { return m_folders; }
	[[nodiscard]] QListView* list() const { return m_list; }
	[[nodiscard]] ModelViewer* viewer() const { return m_viewer; }
	[[nodiscard]] QComboBox* skins() const { return m_skin; }
	/// One combo box per body group with more than one option (in body group order).
	[[nodiscard]] const QList<QComboBox*>& bodyGroupBoxes() const { return m_bodyBoxes; }
	[[nodiscard]] QListWidget* materialList() const { return m_materialList; }
	/// "Export for editing ...": opens the export dialog for a model (deletes itself when closed).
	ModelExportDialog* exportModel(const QString& model);
	/// Where the right-click menus get their "Show in ..." entries from (the main window).
	void setAssetMenuFiller(AssetMenuFiller filler) { m_menuFiller = std::move(filler); }

signals:
	/// "Place in map" or a double-click: arm this class with these keyvalues on the Map page.
	void placeRequested(const QString& classname, const std::vector<std::pair<std::string, std::string>>& keys);
	/// A material of the model was clicked: open it in the material editor.
	void openMaterialRequested(const QString& material);
	/// "Import ...": bring the mapper's own files into the mod (ImportDialog).
	void importRequested();

private:
	/// What a background load produced.
	struct Loaded {
		QString path;
		int generation = 0;
		bool withInfo = false;   ///< false: only new geometry (other skin or body)
		ModelInfo info;
		std::vector<std::string> sequences;
		bool hasPhysics = false;
		std::shared_ptr<const ModelGeometry> geometry;
		QString error;
	};
	void setModels(const QStringList& models);
	void applyFilter();
	void load(bool withInfo);
	void loaded(const Loaded& result);
	void rebuildBodyGroups();
	void updateDetails();
	[[nodiscard]] int body() const;

	Editor* m_editor;
	AssetBrowser* m_assets;
	MaterialCache* m_materials;
	QLineEdit* m_search;
	QCheckBox* m_usedOnly;
	QTreeWidget* m_folders;
	QListView* m_list;
	ModelListModel* m_model;
	ModelFolderFilter* m_filter;
	QLabel* m_count;
	QLabel* m_title;
	QLabel* m_path;
	ModelViewer* m_viewer;
	QComboBox* m_skin;
	QCheckBox* m_wireframe;
	QWidget* m_bodyHolder;
	QFormLayout* m_bodyForm;
	QList<QComboBox*> m_bodyBoxes;
	QLabel* m_details;
	QListWidget* m_materialList;
	class EnhancementSwitches* m_enhancementSwitches;
	AssetMenuFiller m_menuFiller;
	QComboBox* m_placeAs;
	QPushButton* m_place;
	QPushButton* m_copy;
	QPushButton* m_export;
	QString m_folder;            ///< Folder picked in the tree ("models/props_c17/", empty = all)
	QString m_current;
	QTimer* m_usageUpdate = nullptr;   ///< Counts the model uses once after a burst of edits
	ModelInfo m_info;
	std::vector<std::string> m_sequences;
	bool m_hasPhysics = false;
	QThreadPool m_pool;
	std::shared_ptr<std::atomic<int>> m_generation = std::make_shared<std::atomic<int>>(0);
};

} // namespace sme::app
