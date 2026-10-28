// asset_browser.h - Browser for materials (with category filters and thumbnails) and models
// (with previews). Items can be dragged into the 3D view: materials onto faces, models
// become props. Sounds have their own page and dock (sound_browser.h), entities sit at the entity tool.

#pragma once

#include <QAbstractListModel>
#include <QPixmap>
#include <QFutureWatcher>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QWidget>

#include <atomic>

#include <sme/assets.h>

#include "asset_menu.h"

class QLabel;
class QLineEdit;
class QListView;
class QComboBox;
class QTabWidget;
class QCheckBox;
class QListWidget;

namespace sme::app {

class Editor;
class MaterialCache;

/// The picture that follows the mouse while an asset is dragged: a rounded, semi-transparent card
/// with the preview, the name and a smaller second line (e.g. the path).
[[nodiscard]] QPixmap assetDragCard(const QPixmap& preview, const QString& title, const QString& subtitle, qreal devicePixelRatio);

/// All materials of the game with their categories (filled in the background).
class MaterialListModel : public QAbstractListModel {
	Q_OBJECT

public:
	/// One material of the list (`classified`: its category was read from the VMT in the background).
	struct Item {
		QString name;                 ///< "concrete/wall01"
		QString shader;
		MaterialCategory category;
		bool classified = false;
	};

	MaterialListModel(MaterialCache* thumbnails, QObject* parent = nullptr);
	/// Replaces the whole list (after indexing).
	void setItems(std::vector<Item> items);
	[[nodiscard]] const std::vector<Item>& items() const { return m_items; }
	/// Qt list model interface: rows, name and thumbnail per row, drag and drop into the views.
	[[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
	[[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
	[[nodiscard]] QStringList mimeTypes() const override;
	[[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
	/// A thumbnail arrived: repaints that row.
	void thumbnailReady(const QString& name);

private:
	MaterialCache* m_thumbnails;
	std::vector<Item> m_items;
	QHash<QString, int> m_rows;
};

/// Filters the material list by search text, category and "used in this map".
class MaterialFilter : public QSortFilterProxyModel {
	Q_OBJECT

public:
	enum class Category { All, Brush, Blend, Model, Tool, Translucent, Water, Decal, Sky, UsedInMap };
	using QSortFilterProxyModel::QSortFilterProxyModel;
	/// Search text, category and the materials the map uses (for the "Used in this map" category).
	void setText(const QString& text);
	void setCategory(Category category);
	void setUsed(const QSet<QString>& used);

protected:
	[[nodiscard]] bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
	QStringList m_words;
	Category m_category = Category::Brush;
	QSet<QString> m_used;
};

class ModelThumbnails;

/// All models of the game with preview pictures (rendered when shown).
class ModelListModel : public QAbstractListModel {
	Q_OBJECT

public:
	ModelListModel(ModelThumbnails* thumbnails, QObject* parent = nullptr);
	/// Replaces the whole list (after indexing).
	void setModels(const QStringList& models);
	/// Qt list model interface: rows, name and preview per row, drag and drop into the views.
	[[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
	[[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
	[[nodiscard]] QStringList mimeTypes() const override;
	[[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
	/// A preview arrived: repaints that row.
	void thumbnailReady(const QString& path);

private:
	ModelThumbnails* m_thumbnails;
	QStringList m_models;
	QHash<QString, int> m_rows;
};

/// Result of the background indexing of a game's assets.
struct AssetIndex {
	std::vector<MaterialListModel::Item> materials;
	QStringList models;
	QStringList sounds;
	std::vector<ParticleSystem> particles;
	QStringList soundscapes;   ///< Names for env_soundscape's "soundscape" key
};

/// The Asset Browser dock: materials and models with search, filters and thumbnails.
class AssetBrowser : public QWidget {
	Q_OBJECT

public:
	AssetBrowser(Editor* editor, QWidget* parent = nullptr);
	~AssetBrowser() override;

	/// All material names ("concrete/wall01") once indexed (used for "did you mean").
	[[nodiscard]] std::vector<std::string> materialNames() const;
	[[nodiscard]] QStringList modelNames() const { return m_models; }
	[[nodiscard]] QStringList soundNames() const { return m_sounds; }
	[[nodiscard]] QStringList soundscapeNames() const { return m_soundscapes; }
	[[nodiscard]] QStringList particleNames() const;
	[[nodiscard]] const std::vector<ParticleSystem>& particles() const { return m_particles; }
	void showMaterials();
	void showModels();
	/// On the Materials page: only the material list (no Models tab), larger previews.
	void setPageMode(bool page);
	/// Shows the material ("concrete/wall01", any case) in the list: clears the search, switches to
	/// "All" if the filter hides it, selects and scrolls to it. False when the game has none.
	bool selectMaterial(const QString& name);
	/// Same for a model ("models/props_c17/chair01.mdl").
	bool selectModel(const QString& path);
	/// Where the right-click menus get their "Show in ..." entries from (the main window).
	void setAssetMenuFiller(AssetMenuFiller filler) { m_menuFiller = std::move(filler); }
	/// The selected material, else the first one listed ("" before the list is ready).
	[[nodiscard]] QString shownMaterial() const;
	/// A material was edited or created: new thumbnail, and listed if it is new.
	void materialChanged(const QString& name);
	[[nodiscard]] ModelThumbnails* modelThumbnails() const { return m_modelThumbnails; }
	[[nodiscard]] QListView* modelView() const { return m_modelView; }

signals:
	/// Background indexing finished (the name lists are filled).
	void materialsIndexed();
	/// "Edit material ..." in the material list's menu.
	void editMaterialRequested(const QString& name);
	/// A material was clicked in the list (lowercase name as listed, "concrete/wall01").
	void materialPicked(const QString& name);

private:
	void reload();
	void updateUsedMaterials();
	void showMaterialInfo(const QModelIndex& index);
	void updateRecent();

	Editor* m_editor;
	MaterialCache* m_thumbnails;
	QTabWidget* m_tabs;

	MaterialListModel* m_materialModel;
	MaterialFilter* m_materialFilter;
	QListView* m_materialView;
	QLineEdit* m_materialSearch;
	QComboBox* m_materialCategory;
	QLabel* m_materialInfo;
	QListWidget* m_recent;
	QFutureWatcher<AssetIndex> m_indexWatcher;
	std::atomic<int> m_generation{0};

	bool m_pageMode = false;   ///< Shown on the Materials page
	AssetMenuFiller m_menuFiller;
	QStringList m_models, m_sounds, m_soundscapes;
	std::vector<ParticleSystem> m_particles;
	ModelThumbnails* m_modelThumbnails;
	ModelListModel* m_modelModel;
	QListView* m_modelView;
	QLineEdit* m_modelSearch;
	QLabel* m_modelInfo;
};

} // namespace sme::app
